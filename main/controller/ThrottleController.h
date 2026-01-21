#pragma once

#include "Knob.h"
#include "Throttle.h"
#include "WiThrottleClient.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <memory>
#include <vector>

/**
 * @brief Controller for managing throttle and knob interactions
 * 
 * Coordinates between:
 * - 4 Throttle models (state, loco assignments)
 * - 2 Knob models (state, assignments)
 * - WiThrottle client (network communication)
 * - UI (ThrottleMeter widgets)
 */
class ThrottleController
{
public:
    static constexpr int NUM_THROTTLES = 4;
    static constexpr int NUM_KNOBS = 2;

    struct ThrottleSnapshot {
        int throttleId;
        Throttle::State state;
        int assignedKnob;
        int currentSpeed;
        bool direction;
        bool hasLocomotive;
        std::string locoName;
        int locoAddress;
    };

    struct RosterSelectionSnapshot {
        bool active = false;
        int throttleId = -1;
        int knobId = -1;
        int rosterIndex = 0;
        bool hasRosterEntry = false;
        std::string rosterName;
        int rosterAddress = 0;
    };
    
    /**
     * @brief Constructor
     * @param wiThrottleClient WiThrottle client for network communication
     */
    explicit ThrottleController(WiThrottleClient* wiThrottleClient);
    ~ThrottleController();
    
    /**
     * @brief Initialize controller
     */
    void initialize();
    
    /**
     * @brief Handle knob indicator touch on a throttle
     * @param throttleId Throttle ID (0-3)
     * @param knobId Knob ID (0-1)
     */
    void onKnobIndicatorTouched(int throttleId, int knobId);
    
    /**
     * @brief Handle knob rotation
     * @param knobId Knob ID (0-1)
     * @param delta Rotation delta (positive=CW, negative=CCW)
     */
    void onKnobRotation(int knobId, int delta);
    
    /**
     * @brief Handle knob button press
     * @param knobId Knob ID (0-1)
     */
    void onKnobPress(int knobId);
    
    /**
     * @brief Handle throttle release button
     * @param throttleId Throttle ID (0-3)
     */
    void onThrottleRelease(int throttleId);
    
    /**
     * @brief Handle throttle functions button
     * @param throttleId Throttle ID (0-3)
     */
    void onThrottleFunctions(int throttleId);
    
    /**
     * @brief Get throttle model
     * @param throttleId Throttle ID (0-3)
     * @return Throttle pointer or nullptr
     */
    Throttle* getThrottle(int throttleId);

    /**
     * @brief Get a thread-safe snapshot of a throttle's state
     * @return true if snapshot was captured
     */
    bool getThrottleSnapshot(int throttleId, ThrottleSnapshot& outSnapshot) const;

    /**
     * @brief Get current roster selection (if any knob is selecting)
     * @return true if snapshot was captured
     */
    bool getRosterSelectionSnapshot(RosterSelectionSnapshot& outSnapshot) const;

    /**
     * @brief Get a snapshot of function data for a throttle
     */
    bool getFunctionsSnapshot(int throttleId, std::vector<Function>& outFunctions) const;

    /**
     * @brief Get a specific function state
     */
    bool getFunctionState(int throttleId, int functionNumber, bool& outState) const;
    
    /**
     * @brief Get knob model
     * @param knobId Knob ID (0-1)
     * @return Knob pointer or nullptr
     */
    Knob* getKnob(int knobId);
    
    /**
     * @brief Get current roster size
     */
    size_t getRosterSize() const;
    
    /**
     * @brief Get loco at roster index
     */
    bool getLocoAtRosterIndex(int index, WiThrottleClient::Locomotive& outEntry) const;
    
    /**
     * @brief Set UI update callback
     * @param callback Function to call when UI needs updating
     * @param userData User data to pass to callback
     */
    void setUIUpdateCallback(void (*callback)(void*), void* userData);
    
    /**
     * @brief Get configured speed steps per knob click from NVS
     * @return Speed steps (default 4 if not configured)
     */
    static int getSpeedStepsPerClick();

private:
    bool lockState(TickType_t timeout) const;
    void unlockState() const;

    void updateUI();
    void sendSpeedCommand(int throttleId, int speed);
    void sendDirectionCommand(int throttleId, bool forward);
    std::unique_ptr<Locomotive> createLocomotiveFromRoster(const WiThrottleClient::Locomotive& rosterEntry);
    
    // WiThrottle callback handlers
    void onThrottleStateChanged(const WiThrottleClient::ThrottleUpdate& update);
    static void throttleStateCallbackWrapper(void* userData, const WiThrottleClient::ThrottleUpdate& update);

    void onFunctionLabelsReceived(char throttleId, const std::vector<std::string>& labels);
    
    // Polling for state synchronization
    void pollThrottleStates();
    static void pollingTimerCallback(void* arg);
    void startPollingTimer();
    void stopPollingTimer();
    
    WiThrottleClient* m_wiThrottleClient;
    std::vector<std::unique_ptr<Throttle>> m_throttles;
    std::vector<std::unique_ptr<Knob>> m_knobs;

    mutable SemaphoreHandle_t m_stateMutex;
    
    void (*m_uiUpdateCallback)(void*);
    void* m_uiUpdateUserData;
    
    esp_timer_handle_t m_pollingTimer;
};
