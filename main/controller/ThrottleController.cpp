#include "ThrottleController.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char* TAG = "ThrottleController";
static const char* NVS_NAMESPACE = "jmri";
static const char* NVS_KEY_SPEED_STEPS = "speed_steps";

// The port validates throttle ids against its own bound, so a divergence here
// would let an id this controller thinks is valid be refused by every backend.
static_assert(ThrottleController::NUM_THROTTLES == ThrottleBackend::MAX_THROTTLES,
              "ThrottleController and ThrottleBackend disagree on the throttle count");

ThrottleController::ThrottleController(ThrottleBackend* backend)
    : m_backend(backend)
    , m_stateMutex(nullptr)
    , m_uiUpdateCallback(nullptr)
    , m_uiUpdateUserData(nullptr)
    , m_trackPowerCallback(nullptr)
    , m_trackPowerUserData(nullptr)
    , m_pollingTask(nullptr)
    , m_pollingRunning(false)
    , m_cachedSpeedSteps(4)
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
    if (m_backend) {
        m_backend->setThrottleStateCallback(
            [this](const ThrottleBackend::ThrottleUpdate& update) {
                this->onThrottleStateChanged(update);
            }
        );
        // Repaint on link up/down. The UI gates the knobs on connection state,
        // so without this the screen keeps whatever it drew at startup.
        m_backend->setConnectionStateCallback(
            [this](ThrottleBackend::ConnectionState) {
                this->updateUI();
            }
        );
        m_backend->setTrackPowerCallback(
            [this](ThrottleBackend::TrackPower state) {
                if (m_trackPowerCallback) {
                    m_trackPowerCallback(m_trackPowerUserData, state);
                }
            }
        );
        if (m_backend->providesFunctionLabels()) {
            m_backend->setFunctionLabelsCallback(
                [this](int throttleId, const std::vector<std::string>& labels) {
                    this->onFunctionLabelsReceived(throttleId, labels);
                }
            );
        }
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
    
    reloadSpeedStepsFromNvs();
    
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
        if (shouldSendDirection) {
            // One movement, not two. Sending the new speed against the old
            // direction first would command the loco faster the way it was
            // already going, and only then reverse it.
            sendSpeedAndDirectionCommand(throttleId, newSpeed, newDirection);
        } else {
            sendSpeedCommand(throttleId, newSpeed);
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

        ThrottleBackend::RosterEntry rosterLoco;
        bool hasRosterEntry = getLocoAtRosterIndex(rosterIndex, rosterLoco);

        if (hasRosterEntry && throttleId >= 0) {
            // Convert roster entry to our Locomotive model
            auto loco = createLocomotiveFromRoster(rosterLoco);

            // Update models
            Throttle* throttle = m_throttles[throttleId].get();
            throttle->assignLocomotive(std::move(loco));
            knob->startControlling();

            unlockState();

            // Bookkeeping on a transport that addresses locos directly; a real
            // session handshake on one that does not.
            m_backend->acquireLocomotive(throttleId, rosterLoco.address,
                                         rosterLoco.longAddress);

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

    // The local models are already released above, so a missing backend costs
    // the remote release, not the UI's view of it.
    if (m_backend) {
        m_backend->releaseLocomotive(throttleId);
    }

    ESP_LOGI(TAG, "Released throttle %d", throttleId);
    updateUI();
}

void ThrottleController::onThrottleFunctions(int throttleId)
{
    if (throttleId < 0 || throttleId >= NUM_THROTTLES) return;
    
    ESP_LOGI(TAG, "Functions button pressed for throttle %d", throttleId);
}

#if CONFIG_THROTTLE_TESTS
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
#endif

bool ThrottleController::supportsTrackPower() const
{
    return m_backend && m_backend->supportsTrackPower();
}

ThrottleBackend::TrackPower ThrottleController::getTrackPower() const
{
    if (!m_backend) {
        return ThrottleBackend::TrackPower::UNKNOWN;
    }
    return m_backend->getTrackPower();
}

void ThrottleController::setTrackPowerCallback(
    void (*callback)(void*, ThrottleBackend::TrackPower), void* userData)
{
    m_trackPowerCallback = callback;
    m_trackPowerUserData = userData;
}

void ThrottleController::trackPowerTaskFunc(void* arg)
{
    auto* request = static_cast<TrackPowerRequest*>(arg);
    if (request && request->controller && request->controller->m_backend) {
        request->controller->m_backend->setTrackPower(request->on);
    }
    delete request;
    vTaskDelete(nullptr);
}

void ThrottleController::requestTrackPower(bool on)
{
    if (!m_backend || !m_backend->supportsTrackPower()) {
        ESP_LOGW(TAG, "Track power not supported by the active transport");
        return;
    }

    // On its own task: the orchestrator's power command is a blocking HTTP
    // round trip, and this is called straight from an LVGL button handler.
    // 4 KB covers the HTTP client.
    auto* request = new TrackPowerRequest{this, on};
    if (xTaskCreate(trackPowerTaskFunc, "track_power", 4096, request, 5, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create track power task");
        delete request;
    }
}

bool ThrottleController::isConnected() const
{
    return m_backend && m_backend->isConnected();
}

esp_err_t ThrottleController::setFunction(int throttleId, int functionNumber, bool state)
{
    if (!m_backend) {
        return ESP_ERR_INVALID_STATE;
    }
    if (throttleId < 0 || throttleId >= NUM_THROTTLES) {
        return ESP_ERR_INVALID_ARG;
    }
    return m_backend->setFunction(throttleId, functionNumber, state);
}

size_t ThrottleController::getRosterSize() const
{
    if (!m_backend || !m_backend->providesRoster()) {
        return 0;
    }
    return m_backend->getRosterSize();
}

bool ThrottleController::getLocoAtRosterIndex(int index, ThrottleBackend::RosterEntry& outEntry) const
{
    if (!m_backend || !m_backend->providesRoster()) {
        return false;
    }
    return m_backend->getRosterEntry(index, outEntry);
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

    if (outSnapshot.active && m_backend && m_backend->providesRoster()) {
        ThrottleBackend::RosterEntry entry;
        if (m_backend->getRosterEntry(outSnapshot.rosterIndex, entry)) {
            outSnapshot.hasRosterEntry = true;
            outSnapshot.rosterName = entry.name;
            outSnapshot.rosterAddress = entry.address;
        }
    }

    unlockState();
    return true;
}

bool ThrottleController::getFunctionsSnapshot(int throttleId, std::vector<Function>& outFunctions) const
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

    outFunctions = throttle->getFunctions();
    unlockState();
    return true;
}

bool ThrottleController::getFunctionState(int throttleId, int functionNumber, bool& outState) const
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

    for (const auto& func : throttle->getFunctions()) {
        if (func.number == functionNumber) {
            outState = func.state;
            unlockState();
            return true;
        }
    }

    unlockState();
    return false;
}

void ThrottleController::sendSpeedCommand(int throttleId, int speed)
{
    if (!m_backend) {
        return;
    }
    m_backend->setSpeed(throttleId, speed);
}

void ThrottleController::sendDirectionCommand(int throttleId, bool forward)
{
    if (!m_backend) {
        return;
    }
    m_backend->setDirection(throttleId, forward);
}

void ThrottleController::sendSpeedAndDirectionCommand(int throttleId, int speed, bool forward)
{
    if (!m_backend) {
        return;
    }
    m_backend->setSpeedAndDirection(throttleId, speed, forward);
}

std::unique_ptr<Locomotive> ThrottleController::createLocomotiveFromRoster(const ThrottleBackend::RosterEntry& rosterEntry)
{
    Locomotive::AddressType addressType = rosterEntry.longAddress
        ? Locomotive::AddressType::LONG
        : Locomotive::AddressType::SHORT;

    return std::make_unique<Locomotive>(rosterEntry.name, rosterEntry.address, addressType);
}

void ThrottleController::onThrottleStateChanged(const ThrottleBackend::ThrottleUpdate& update)
{
    const int throttleId = update.throttleId;

    if (throttleId < 0 || throttleId >= NUM_THROTTLES) {
        ESP_LOGW(TAG, "Invalid throttle ID in update: %d", throttleId);
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

void ThrottleController::onFunctionLabelsReceived(int throttleId, const std::vector<std::string>& labels)
{
    if (throttleId < 0 || throttleId >= NUM_THROTTLES) {
        ESP_LOGW(TAG, "Invalid throttle ID for function labels: %d", throttleId);
        return;
    }

    if (!lockState(pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "Failed to lock state for function labels");
        return;
    }

    Throttle* throttle = m_throttles[throttleId].get();
    if (!throttle) {
        unlockState();
        return;
    }

    std::vector<Function> existing = throttle->getFunctions();
    throttle->clearFunctions();
    for (size_t i = 0; i < labels.size(); ++i) {
        bool state = false;
        for (const auto& func : existing) {
            if (func.number == static_cast<int>(i)) {
                state = func.state;
                break;
            }
        }
        Function function(static_cast<int>(i), labels[i], state);
        throttle->addFunction(function);
    }

    unlockState();
    updateUI();
}

void ThrottleController::pollThrottleStates()
{
    if (!m_backend || !m_backend->isConnected()) {
        return;
    }

    // Snapshot which throttles are allocated while holding the mutex
    bool needsPoll[NUM_THROTTLES] = {};
    if (lockState(pdMS_TO_TICKS(50))) {
        for (int i = 0; i < NUM_THROTTLES; i++) {
            auto state = m_throttles[i]->getState();
            needsPoll[i] = (state == Throttle::State::ALLOCATED_WITH_KNOB ||
                            state == Throttle::State::ALLOCATED_NO_KNOB);
        }
        unlockState();
    } else {
        ESP_LOGW(TAG, "Failed to lock state for polling snapshot");
        return;
    }
    
    // Issue network queries outside the lock
    for (int i = 0; i < NUM_THROTTLES; i++) {
        if (needsPoll[i]) {
            m_backend->refreshThrottleState(i);
            ESP_LOGD(TAG, "Polling throttle %d state", i);
        }
    }
}

void ThrottleController::pollingTaskFunc(void* arg)
{
    ThrottleController* controller = static_cast<ThrottleController*>(arg);
    while (controller->m_pollingRunning) {
        controller->pollThrottleStates();
        // Sleep 10 seconds between polls; check flag every 500 ms for fast shutdown
        for (int i = 0; i < 20 && controller->m_pollingRunning; ++i) {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
    vTaskDelete(nullptr);
}

void ThrottleController::startPollingTimer()
{
    if (m_pollingTask != nullptr) {
        ESP_LOGW(TAG, "Polling task already started");
        return;
    }

    // A transport that pushes state changes needs no poller at all. Skipping
    // the task rather than letting it spin saves its 4 KB stack outright.
    if (!m_backend || !m_backend->requiresPolling()) {
        ESP_LOGI(TAG, "Backend pushes throttle state; no polling task started");
        return;
    }

    m_pollingRunning = true;
    BaseType_t ret = xTaskCreate(pollingTaskFunc, "throttle_poll", 4096, this, 3, &m_pollingTask);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create polling task");
        m_pollingRunning = false;
        return;
    }
    
    ESP_LOGI(TAG, "Started throttle state polling task (10 second interval)");
}

void ThrottleController::stopPollingTimer()
{
    if (m_pollingTask != nullptr) {
        m_pollingRunning = false;
        // The task will exit on its own within ~500 ms
        // Wait briefly for it to finish
        vTaskDelay(pdMS_TO_TICKS(600));
        m_pollingTask = nullptr;
        ESP_LOGI(TAG, "Stopped throttle state polling");
    }
}

int ThrottleController::getSpeedStepsPerClick()
{
    return m_cachedSpeedSteps;
}

void ThrottleController::reloadSpeedStepsFromNvs()
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
    
    m_cachedSpeedSteps = (int)speedSteps;
    ESP_LOGI(TAG, "Speed steps per click: %d", m_cachedSpeedSteps);
}
