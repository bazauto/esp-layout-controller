#include "ThrottleController.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char* TAG = "ThrottleController";
static const char* NVS_NAMESPACE = "jmri_config";
static const char* NVS_KEY_SPEED_STEPS = "speed_steps";

ThrottleController::ThrottleController(WiThrottleClient* wiThrottleClient)
    : m_wiThrottleClient(wiThrottleClient)
    , m_stateMutex(nullptr)
    , m_uiUpdateCallback(nullptr)
    , m_uiUpdateUserData(nullptr)
    , m_pollingTimer(nullptr)
{
    // Create throttles
    for (int i = 0; i < NUM_THROTTLES; i++) {
        m_throttles.push_back(std::make_unique<Throttle>(i));
    }
    
    // Create knobs
    for (int i = 0; i < NUM_KNOBS; i++) {
        m_knobs.push_back(std::make_unique<Knob>(i));
    }
    
    // Register throttle state change callback
    if (m_wiThrottleClient) {
        m_wiThrottleClient->setThrottleStateCallback(
            [this](const WiThrottleClient::ThrottleUpdate& update) {
                this->onThrottleStateChanged(update);
            }
        );
    }

    m_stateMutex = xSemaphoreCreateMutex();
    if (!m_stateMutex) {
        ESP_LOGE(TAG, "Failed to create ThrottleController state mutex");
    }
}

ThrottleController::~ThrottleController()
{
    stopPollingTimer();
    if (m_stateMutex) {
        vSemaphoreDelete(m_stateMutex);
        m_stateMutex = nullptr;
    }
}

void ThrottleController::initialize()
{
    ESP_LOGI(TAG, "ThrottleController initialized with %d throttles and %d knobs",
             NUM_THROTTLES, NUM_KNOBS);
    
    // Start polling timer for state synchronization
    startPollingTimer();
}

void ThrottleController::onKnobIndicatorTouched(int throttleId, int knobId)
{
    if (throttleId < 0 || throttleId >= NUM_THROTTLES) return;
    if (knobId < 0 || knobId >= NUM_KNOBS) return;

    if (!lockState(pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "Failed to lock state for knob indicator touch");
        return;
    }

    Throttle* throttle = m_throttles[throttleId].get();
    Knob* knob = m_knobs[knobId].get();
    bool shouldUpdate = false;

    ESP_LOGI(TAG, "Knob %d touched on throttle %d (throttle state=%d, knob state=%d)",
             knobId, throttleId, (int)throttle->getState(), (int)knob->getState());

    // Check if knob is currently controlling/selecting a different throttle
    if (knob->getState() != Knob::State::IDLE) {
        int currentThrottleId = knob->getAssignedThrottleId();
        if (currentThrottleId != throttleId && currentThrottleId >= 0) {
            Throttle* oldThrottle = m_throttles[currentThrottleId].get();

            if (throttle->getState() == Throttle::State::ALLOCATED_NO_KNOB) {
                // Move knob to allocated throttle (keep controlling)
                oldThrottle->unassignKnob();
                throttle->assignKnob(knobId);
                knob->reassignToThrottle(throttleId, Knob::State::CONTROLLING, false);

                ESP_LOGI(TAG, "Moved knob %d from throttle %d to throttle %d (control)",
                         knobId, currentThrottleId, throttleId);
                shouldUpdate = true;
            } else if (throttle->getState() == Throttle::State::UNALLOCATED) {
                // Move knob to unallocated throttle for roster selection
                oldThrottle->unassignKnob();
                throttle->assignKnob(knobId);
                knob->reassignToThrottle(throttleId, Knob::State::SELECTING, true);

                ESP_LOGI(TAG, "Moved knob %d from throttle %d to throttle %d (selecting)",
                         knobId, currentThrottleId, throttleId);
                shouldUpdate = true;
            }
        }
    }

    // Assign IDLE knob to UNALLOCATED throttle for loco selection
    if (!shouldUpdate && throttle->getState() == Throttle::State::UNALLOCATED &&
        knob->getState() == Knob::State::IDLE) {

        throttle->assignKnob(knobId);
        knob->assignToThrottle(throttleId);

        ESP_LOGI(TAG, "Knob %d assigned to throttle %d for loco selection", knobId, throttleId);
        shouldUpdate = true;
    }

    // Assign IDLE knob to ALLOCATED_NO_KNOB throttle for control
    if (!shouldUpdate && throttle->getState() == Throttle::State::ALLOCATED_NO_KNOB &&
        knob->getState() == Knob::State::IDLE) {

        throttle->assignKnob(knobId);
        knob->assignToThrottle(throttleId);
        knob->startControlling();  // Go straight to CONTROLLING

        ESP_LOGI(TAG, "Knob %d assigned to throttle %d for control (already has loco)", knobId, throttleId);
        shouldUpdate = true;
    }

    unlockState();

    if (shouldUpdate) {
        updateUI();
    } else {
        ESP_LOGW(TAG, "Knob assignment not allowed in current states");
    }
}

void ThrottleController::onKnobRotation(int knobId, int delta)
{
    if (knobId < 0 || knobId >= NUM_KNOBS) return;

    if (!lockState(pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "Failed to lock state for knob rotation");
        return;
    }

    Knob* knob = m_knobs[knobId].get();
    bool shouldUpdate = false;
    bool shouldSendSpeed = false;
    bool shouldSendDirection = false;
    int throttleId = -1;
    int newSpeed = 0;
    int currentSpeed = 0;
    int stepsPerClick = 0;
    bool currentDirection = true;
    bool newDirection = true;

    if (knob->getState() == Knob::State::SELECTING) {
        // Scroll through roster
        size_t rosterSize = getRosterSize();
        knob->handleRotation(delta, rosterSize);

        ESP_LOGD(TAG, "Knob %d roster index: %d / %d", knobId, knob->getRosterIndex(), rosterSize);
        shouldUpdate = true;

    } else if (knob->getState() == Knob::State::CONTROLLING) {
        // Control speed
        throttleId = knob->getAssignedThrottleId();
        if (throttleId >= 0) {
            Throttle* throttle = m_throttles[throttleId].get();
            currentSpeed = throttle->getCurrentSpeed();
            currentDirection = throttle->getDirection();

            // Get configured speed steps per click
            stepsPerClick = getSpeedStepsPerClick();

            // Signed speed: forward is positive, reverse is negative
            int signedSpeed = currentDirection ? currentSpeed : -currentSpeed;
            int newSignedSpeed = signedSpeed + (delta * stepsPerClick);

            // Clamp to -126..126
            if (newSignedSpeed > 126) newSignedSpeed = 126;
            if (newSignedSpeed < -126) newSignedSpeed = -126;

            // Determine new direction (only flip when crossing below 0)
            if (newSignedSpeed > 0) {
                newDirection = true;
            } else if (newSignedSpeed < 0) {
                newDirection = false;
            } else {
                newDirection = currentDirection;
            }

            newSpeed = newSignedSpeed >= 0 ? newSignedSpeed : -newSignedSpeed;

            // Optimistic update (JMRI doesn't always send speed notifications)
            throttle->setSpeed(newSpeed);
            throttle->setDirection(newDirection);
            shouldSendSpeed = true;
            shouldSendDirection = (newDirection != currentDirection);
            shouldUpdate = true;
        }
    }

    unlockState();

    if (shouldSendSpeed && throttleId >= 0) {
        // Send command to WiThrottle
        sendSpeedCommand(throttleId, newSpeed);

        if (shouldSendDirection) {
            sendDirectionCommand(throttleId, newDirection);
        }

    ESP_LOGI(TAG, "Knob %d changed throttle %d speed: %d -> %d (dir: %s -> %s, steps: %d, optimistic + polling)",
         knobId,
         throttleId,
         currentSpeed,
         newSpeed,
         currentDirection ? "forward" : "reverse",
         newDirection ? "forward" : "reverse",
         stepsPerClick);
    }

    if (shouldUpdate) {
        // Update UI immediately for responsive feel
        updateUI();
    }
}

void ThrottleController::onKnobPress(int knobId)
{
    if (knobId < 0 || knobId >= NUM_KNOBS) return;

    if (!lockState(pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "Failed to lock state for knob press");
        return;
    }

    Knob* knob = m_knobs[knobId].get();

    if (knob->getState() == Knob::State::SELECTING) {
        // Acquire the selected loco
        int throttleId = knob->getAssignedThrottleId();
        int rosterIndex = knob->getRosterIndex();

        WiThrottleClient::Locomotive rosterLoco;
        bool hasRosterEntry = getLocoAtRosterIndex(rosterIndex, rosterLoco);

        if (hasRosterEntry && throttleId >= 0) {
            // Convert roster entry to our Locomotive model
            auto loco = createLocomotiveFromRoster(rosterLoco);

            // Update models
            Throttle* throttle = m_throttles[throttleId].get();
            throttle->assignLocomotive(std::move(loco));
            knob->startControlling();

            unlockState();

            // Send acquire command to WiThrottle
            bool isLongAddress = (rosterLoco.addressType == 'L');
            m_wiThrottleClient->acquireLocomotive('0' + throttleId, rosterLoco.address,
                                                   isLongAddress);

            ESP_LOGI(TAG, "Knob %d acquired loco '%s' (#%d) on throttle %d",
                     knobId, rosterLoco.name.c_str(), rosterLoco.address, throttleId);
            updateUI();
            return;
        }
    } else if (knob->getState() == Knob::State::CONTROLLING) {
        // Normal stop (set speed to 0 with optimistic UI update)
        int throttleId = knob->getAssignedThrottleId();
        if (throttleId >= 0) {
            Throttle* throttle = m_throttles[throttleId].get();
            if (throttle) {
                throttle->setSpeed(0);
            }
        }

        unlockState();

        if (throttleId >= 0) {
            sendSpeedCommand(throttleId, 0);
            ESP_LOGI(TAG, "Knob %d stop on throttle %d", knobId, throttleId);
            updateUI();
        }
        return;
    }

    unlockState();
}

void ThrottleController::onThrottleRelease(int throttleId)
{
    if (throttleId < 0 || throttleId >= NUM_THROTTLES) return;

    if (!lockState(pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "Failed to lock state for throttle release");
        return;
    }

    Throttle* throttle = m_throttles[throttleId].get();
    int knobId = throttle->getAssignedKnob();

    // Release knob if assigned
    if (knobId >= 0 && knobId < NUM_KNOBS) {
        m_knobs[knobId]->release();
    }

    // Release throttle
    throttle->releaseLocomotive();

    unlockState();

    // Release loco in WiThrottle
    m_wiThrottleClient->releaseLocomotive('0' + throttleId);

    ESP_LOGI(TAG, "Released throttle %d", throttleId);
    updateUI();
}

void ThrottleController::onThrottleFunctions(int throttleId)
{
    if (throttleId < 0 || throttleId >= NUM_THROTTLES) return;
    
    ESP_LOGI(TAG, "Functions button pressed for throttle %d (not yet implemented)", throttleId);
    // TODO: Phase 5 - Open function panel overlay
}

Throttle* ThrottleController::getThrottle(int throttleId)
{
    if (throttleId >= 0 && throttleId < NUM_THROTTLES) {
        return m_throttles[throttleId].get();
    }
    return nullptr;
}

Knob* ThrottleController::getKnob(int knobId)
{
    if (knobId >= 0 && knobId < NUM_KNOBS) {
        return m_knobs[knobId].get();
    }
    return nullptr;
}

size_t ThrottleController::getRosterSize() const
{
    if (!m_wiThrottleClient) {
        return 0;
    }
    return m_wiThrottleClient->getRosterSize();
}

bool ThrottleController::getLocoAtRosterIndex(int index, WiThrottleClient::Locomotive& outEntry) const
{
    if (!m_wiThrottleClient) {
        return false;
    }
    return m_wiThrottleClient->getRosterEntry(index, outEntry);
}

void ThrottleController::setUIUpdateCallback(void (*callback)(void*), void* userData)
{
    m_uiUpdateCallback = callback;
    m_uiUpdateUserData = userData;
}

void ThrottleController::updateUI()
{
    if (m_uiUpdateCallback) {
        m_uiUpdateCallback(m_uiUpdateUserData);
    }
}

bool ThrottleController::lockState(TickType_t timeout) const
{
    if (!m_stateMutex) {
        return true;
    }
    return xSemaphoreTake(m_stateMutex, timeout) == pdTRUE;
}

void ThrottleController::unlockState() const
{
    if (m_stateMutex) {
        xSemaphoreGive(m_stateMutex);
    }
}

bool ThrottleController::getThrottleSnapshot(int throttleId, ThrottleSnapshot& outSnapshot) const
{
    if (throttleId < 0 || throttleId >= NUM_THROTTLES) {
        return false;
    }
    if (!lockState(pdMS_TO_TICKS(50))) {
        return false;
    }

    const Throttle* throttle = m_throttles[throttleId].get();
    if (!throttle) {
        unlockState();
        return false;
    }

    outSnapshot.throttleId = throttleId;
    outSnapshot.state = throttle->getState();
    outSnapshot.assignedKnob = throttle->getAssignedKnob();
    outSnapshot.currentSpeed = throttle->getCurrentSpeed();
    outSnapshot.direction = throttle->getDirection();
    outSnapshot.hasLocomotive = throttle->hasLocomotive();
    if (outSnapshot.hasLocomotive) {
        const Locomotive* loco = throttle->getLocomotive();
        if (loco) {
            outSnapshot.locoName = loco->getName();
            outSnapshot.locoAddress = loco->getAddress();
        }
    } else {
        outSnapshot.locoName.clear();
        outSnapshot.locoAddress = 0;
    }

    unlockState();
    return true;
}

bool ThrottleController::getRosterSelectionSnapshot(RosterSelectionSnapshot& outSnapshot) const
{
    outSnapshot = RosterSelectionSnapshot{};

    if (!lockState(pdMS_TO_TICKS(50))) {
        return false;
    }

    for (int i = 0; i < NUM_KNOBS; i++) {
        Knob* knob = m_knobs[i].get();
        if (knob && knob->getState() == Knob::State::SELECTING) {
            outSnapshot.active = true;
            outSnapshot.knobId = i;
            outSnapshot.throttleId = knob->getAssignedThrottleId();
            outSnapshot.rosterIndex = knob->getRosterIndex();
            break;
        }
    }

    if (outSnapshot.active && m_wiThrottleClient) {
        WiThrottleClient::Locomotive entry;
        if (m_wiThrottleClient->getRosterEntry(outSnapshot.rosterIndex, entry)) {
            outSnapshot.hasRosterEntry = true;
            outSnapshot.rosterName = entry.name;
            outSnapshot.rosterAddress = entry.address;
        }
    }

    unlockState();
    return true;
}

void ThrottleController::sendSpeedCommand(int throttleId, int speed)
{
    // '0' + throttleId gives '0', '1', '2', or '3' - used to convert to char
    m_wiThrottleClient->setSpeed('0' + throttleId, speed);
}

void ThrottleController::sendDirectionCommand(int throttleId, bool forward)
{
    // '0' + throttleId gives '0', '1', '2', or '3' - used to convert to char
    m_wiThrottleClient->setDirection('0' + throttleId, forward);
}

std::unique_ptr<Locomotive> ThrottleController::createLocomotiveFromRoster(const WiThrottleClient::Locomotive& rosterEntry)
{
    // Convert WiThrottle address type to our enum
    Locomotive::AddressType addressType = (rosterEntry.addressType == 'L') 
        ? Locomotive::AddressType::LONG 
        : Locomotive::AddressType::SHORT;
    
    return std::make_unique<Locomotive>(rosterEntry.name, rosterEntry.address, addressType);
}

void ThrottleController::onThrottleStateChanged(const WiThrottleClient::ThrottleUpdate& update)
{
    // Convert char throttleId '0'-'3' to int 0-3
    int throttleId = update.throttleId - '0';
    
    if (throttleId < 0 || throttleId >= NUM_THROTTLES) {
        ESP_LOGW(TAG, "Invalid throttle ID in update: %c", update.throttleId);
        return;
    }
    
    if (!lockState(pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "Failed to lock state for throttle update");
        return;
    }

    Throttle* throttle = m_throttles[throttleId].get();

    // Update speed if present
    if (update.speed >= 0) {
        throttle->setSpeed(update.speed);
        ESP_LOGI(TAG, "Throttle %d speed updated: %d", throttleId, update.speed);
    }

    // Update direction if present
    if (update.direction >= 0) {
        throttle->setDirection(update.direction == 1);
        ESP_LOGI(TAG, "Throttle %d direction updated: %s", throttleId, update.direction ? "forward" : "reverse");
    }

    // Update function if present
    if (update.function >= 0) {
        throttle->setFunctionState(update.function, update.functionState);
        ESP_LOGI(TAG, "Throttle %d function %d: %s", throttleId, update.function, update.functionState ? "on" : "off");
    }

    unlockState();

    // Update UI to reflect changes
    updateUI();
}

void ThrottleController::pollThrottleStates()
{
    if (!m_wiThrottleClient || !m_wiThrottleClient->isConnected()) {
        return;
    }
    
    // Poll speed and direction for all allocated throttles
    for (int i = 0; i < NUM_THROTTLES; i++) {
        Throttle* throttle = m_throttles[i].get();
        
        if (throttle->getState() == Throttle::State::ALLOCATED_WITH_KNOB ||
            throttle->getState() == Throttle::State::ALLOCATED_NO_KNOB) {
            
            // Query speed and direction
            char throttleId = '0' + i;
            m_wiThrottleClient->querySpeed(throttleId);
            m_wiThrottleClient->queryDirection(throttleId);
            
            ESP_LOGD(TAG, "Polling throttle %d state", i);
        }
    }
}

void ThrottleController::pollingTimerCallback(void* arg)
{
    ThrottleController* controller = static_cast<ThrottleController*>(arg);
    if (controller) {
        controller->pollThrottleStates();
    }
}

void ThrottleController::startPollingTimer()
{
    if (m_pollingTimer != nullptr) {
        ESP_LOGW(TAG, "Polling timer already started");
        return;
    }
    
    // Create timer that fires every 10 seconds
    esp_timer_create_args_t timerArgs = {
        .callback = pollingTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "throttle_poll",
        .skip_unhandled_events = false
    };
    
    esp_err_t err = esp_timer_create(&timerArgs, &m_pollingTimer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create polling timer: %s", esp_err_to_name(err));
        return;
    }
    
    // Start periodic timer (10 seconds = 10,000,000 microseconds)
    err = esp_timer_start_periodic(m_pollingTimer, 10 * 1000000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start polling timer: %s", esp_err_to_name(err));
        esp_timer_delete(m_pollingTimer);
        m_pollingTimer = nullptr;
        return;
    }
    
    ESP_LOGI(TAG, "Started throttle state polling (10 second interval)");
}

void ThrottleController::stopPollingTimer()
{
    if (m_pollingTimer != nullptr) {
        esp_timer_stop(m_pollingTimer);
        esp_timer_delete(m_pollingTimer);
        m_pollingTimer = nullptr;
        ESP_LOGI(TAG, "Stopped throttle state polling");
    }
}

int ThrottleController::getSpeedStepsPerClick()
{
    nvs_handle_t handle;
    int32_t speedSteps = 4; // default
    
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        nvs_get_i32(handle, NVS_KEY_SPEED_STEPS, &speedSteps);
        nvs_close(handle);
    }
    
    // Clamp to reasonable range
    if (speedSteps < 1) speedSteps = 1;
    if (speedSteps > 20) speedSteps = 20;
    
    return (int)speedSteps;
}
