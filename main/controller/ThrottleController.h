#pragma once

#include "Knob.h"
#include "Throttle.h"
#include "ThrottleBackend.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <memory>
#include <vector>

/**
 * @brief Controller for managing throttle and knob interactions
 * 
 * Coordinates between:
 * - 4 Throttle models (state, loco assignments)
 * - 2 Knob models (state, assignments)
 * - A ThrottleBackend (network communication, whichever transport is in use)
 * - UI (ThrottleMeter widgets)
 *
 * Depends on the ThrottleBackend port, never on a concrete client, so neither
 * transport's wire format reaches this layer.
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
     * @param backend Transport used to drive locos. Not owned, and must
     *                outlive this controller.
     */
    explicit ThrottleController(ThrottleBackend* backend);
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
    
#if CONFIG_THROTTLE_TESTS
    /**
     * @brief Get throttle model (test-only — bypasses mutex)
     * @param throttleId Throttle ID (0-3)
     * @return Throttle pointer or nullptr
     */
    Throttle* getThrottle(int throttleId);

    /**
     * @brief Get knob model (test-only — bypasses mutex)
     * @param knobId Knob ID (0-1)
     * @return Knob pointer or nullptr
     */
    Knob* getKnob(int knobId);
#endif

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
     * @brief Whether the active transport's link is up.
     *
     * The UI gates the knobs on this. It must come from the active backend and
     * never from a concrete client, or selecting a transport the UI does not
     * know about leaves every knob dead.
     */
    bool isConnected() const;

    /**
     * @brief Set a function on a throttle's locomotive.
     *
     * Goes through the port, so a function press reaches whichever transport is
     * actually in use.
     */
    esp_err_t setFunction(int throttleId, int functionNumber, bool state);

    /**
     * @brief Get current roster size
     */
    size_t getRosterSize() const;
    
    /**
     * @brief Get loco at roster index
     */
    bool getLocoAtRosterIndex(int index, ThrottleBackend::RosterEntry& outEntry) const;
    
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
    int getSpeedStepsPerClick();

    /**
     * @brief Reload speed-steps setting from NVS
     * Call after changing the setting in the config screen.
     */
    void reloadSpeedStepsFromNvs();

private:
    bool lockState(TickType_t timeout) const;
    void unlockState() const;

    void updateUI();
    void sendSpeedCommand(int throttleId, int speed);
    void sendDirectionCommand(int throttleId, bool forward);
    /** Used whenever both change at once — see ThrottleBackend for why. */
    void sendSpeedAndDirectionCommand(int throttleId, int speed, bool forward);
    std::unique_ptr<Locomotive> createLocomotiveFromRoster(const ThrottleBackend::RosterEntry& rosterEntry);

    // Backend callback handlers
    void onThrottleStateChanged(const ThrottleBackend::ThrottleUpdate& update);

    void onFunctionLabelsReceived(int throttleId, const std::vector<std::string>& labels);
    
    // Polling for state synchronization
    void pollThrottleStates();
    static void pollingTaskFunc(void* arg);
    void startPollingTimer();
    void stopPollingTimer();
    
    ThrottleBackend* m_backend;
    std::vector<std::unique_ptr<Throttle>> m_throttles;
    std::vector<std::unique_ptr<Knob>> m_knobs;

    mutable SemaphoreHandle_t m_stateMutex;
    
    void (*m_uiUpdateCallback)(void*);
    void* m_uiUpdateUserData;
    
    TaskHandle_t m_pollingTask;
    bool m_pollingRunning;
    int m_cachedSpeedSteps;
};
