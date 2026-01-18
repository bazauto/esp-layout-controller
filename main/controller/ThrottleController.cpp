#include "ThrottleController.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char* TAG = "ThrottleController";
static const char* NVS_NAMESPACE = "jmri_config";
static const char* NVS_KEY_SPEED_STEPS = "speed_steps";

ThrottleController::ThrottleController(WiThrottleClient* wiThrottleClient)
    : m_wiThrottleClient(wiThrottleClient)
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
    
    Throttle* throttle = m_throttles[throttleId].get();
    Knob* knob = m_knobs[knobId].get();
    
    ESP_LOGI(TAG, "Knob %d touched on throttle %d (throttle state=%d, knob state=%d)",
             knobId, throttleId, (int)throttle->getState(), (int)knob->getState());
    
    // Check if knob is currently controlling a different throttle
    if (knob->getState() != Knob::State::IDLE) {
        int currentThrottleId = knob->getAssignedThrottleId();
        if (currentThrottleId != throttleId && currentThrottleId >= 0) {
            // Check if target throttle has no knob assigned and has a loco
            if (throttle->getState() == Throttle::State::ALLOCATED_NO_KNOB) {
                // Move knob to this throttle
                Throttle* oldThrottle = m_throttles[currentThrottleId].get();
                oldThrottle->unassignKnob();
                
                throttle->assignKnob(knobId);
                // Knob stays in CONTROLLING state, just changes target
                
                ESP_LOGI(TAG, "Moved knob %d from throttle %d to throttle %d",
                         knobId, currentThrottleId, throttleId);
                updateUI();
                return;
            }
        }
    }
    
    // Assign IDLE knob to UNALLOCATED throttle for loco selection
    if (throttle->getState() == Throttle::State::UNALLOCATED && 
        knob->getState() == Knob::State::IDLE) {
        
        throttle->assignKnob(knobId);
        knob->assignToThrottle(throttleId);
        
        ESP_LOGI(TAG, "Knob %d assigned to throttle %d for loco selection", knobId, throttleId);
        updateUI();
        return;
    }
    
    // Assign IDLE knob to ALLOCATED_NO_KNOB throttle for control
    if (throttle->getState() == Throttle::State::ALLOCATED_NO_KNOB &&
        knob->getState() == Knob::State::IDLE) {
        
        throttle->assignKnob(knobId);
        knob->assignToThrottle(throttleId);
        knob->startControlling();  // Go straight to CONTROLLING
        
        ESP_LOGI(TAG, "Knob %d assigned to throttle %d for control (already has loco)", knobId, throttleId);
        updateUI();
        return;
    }
    
    ESP_LOGW(TAG, "Knob assignment not allowed in current states");
}

void ThrottleController::onKnobRotation(int knobId, int delta)
{
    if (knobId < 0 || knobId >= NUM_KNOBS) return;
    
    Knob* knob = m_knobs[knobId].get();
    
    if (knob->getState() == Knob::State::SELECTING) {
        // Scroll through roster
        size_t rosterSize = getRosterSize();
        knob->handleRotation(delta, rosterSize);
        
        ESP_LOGD(TAG, "Knob %d roster index: %d / %d", knobId, knob->getRosterIndex(), rosterSize);
        updateUI();
        
    } else if (knob->getState() == Knob::State::CONTROLLING) {
        // Control speed
        int throttleId = knob->getAssignedThrottleId();
        if (throttleId >= 0) {
            Throttle* throttle = m_throttles[throttleId].get();
            int currentSpeed = throttle->getCurrentSpeed();
            
            // Get configured speed steps per click
            int stepsPerClick = getSpeedStepsPerClick();
            int newSpeed = currentSpeed + (delta * stepsPerClick);
            
            // Clamp to 0-126
            if (newSpeed < 0) newSpeed = 0;
            if (newSpeed > 126) newSpeed = 126;
            
            // Optimistic update (JMRI doesn't always send speed notifications)
            throttle->setSpeed(newSpeed);
            
            // Send command to WiThrottle
            sendSpeedCommand(throttleId, newSpeed);
            
            ESP_LOGI(TAG, "Knob %d changed throttle %d speed: %d -> %d (steps: %d, optimistic + polling)",
                     knobId, throttleId, currentSpeed, newSpeed, stepsPerClick);
            
            // Update UI immediately for responsive feel
            updateUI();
        }
    }
}

void ThrottleController::onKnobPress(int knobId)
{
    if (knobId < 0 || knobId >= NUM_KNOBS) return;
    
    Knob* knob = m_knobs[knobId].get();
    
    if (knob->getState() == Knob::State::SELECTING) {
        // Acquire the selected loco
        int throttleId = knob->getAssignedThrottleId();
        int rosterIndex = knob->getRosterIndex();
        
        const WiThrottleClient::Locomotive* rosterLoco = getLocoAtRosterIndex(rosterIndex);
        if (rosterLoco && throttleId >= 0) {
            // Convert roster entry to our Locomotive model
            auto loco = createLocomotiveFromRoster(*rosterLoco);
            
            // Send acquire command to WiThrottle
            bool isLongAddress = (rosterLoco->addressType == 'L');
            m_wiThrottleClient->acquireLocomotive('0' + throttleId, rosterLoco->address, 
                                                   isLongAddress);
            
            // Update models
            Throttle* throttle = m_throttles[throttleId].get();
            throttle->assignLocomotive(std::move(loco));
            knob->startControlling();
            
            ESP_LOGI(TAG, "Knob %d acquired loco '%s' (#%d) on throttle %d",
                     knobId, rosterLoco->name.c_str(), rosterLoco->address, throttleId);
            updateUI();
        }
        
    } else if (knob->getState() == Knob::State::CONTROLLING) {
        // Emergency stop
        int throttleId = knob->getAssignedThrottleId();
        if (throttleId >= 0) {
            sendSpeedCommand(throttleId, 0);
            ESP_LOGI(TAG, "Knob %d emergency stop on throttle %d", knobId, throttleId);
        }
    }
}

void ThrottleController::onThrottleRelease(int throttleId)
{
    if (throttleId < 0 || throttleId >= NUM_THROTTLES) return;
    
    Throttle* throttle = m_throttles[throttleId].get();
    
    // Release loco in WiThrottle
    m_wiThrottleClient->releaseLocomotive('0' + throttleId);
    
    // Release knob if assigned
    int knobId = throttle->getAssignedKnob();
    if (knobId >= 0 && knobId < NUM_KNOBS) {
        m_knobs[knobId]->release();
    }
    
    // Release throttle
    throttle->releaseLocomotive();
    
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
    return m_wiThrottleClient->getRoster().size();
}

const WiThrottleClient::Locomotive* ThrottleController::getLocoAtRosterIndex(int index) const
{
    const auto& roster = m_wiThrottleClient->getRoster();
    if (index >= 0 && index < (int)roster.size()) {
        return &roster[index];
    }
    return nullptr;
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

void ThrottleController::sendSpeedCommand(int throttleId, int speed)
{
    // '0' + throttleId gives '0', '1', '2', or '3' - used to convert to char
    m_wiThrottleClient->setSpeed('0' + throttleId, speed);
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
