#pragma once

#include "lvgl.h"
#include "ThrottleMeter.h"
#include "../model/Throttle.h"
#include "../communication/WiThrottleClient.h"
#include "../communication/JmriJsonClient.h"
#include <array>
#include <memory>

/**
 * @brief Main application screen with throttle controls
 * 
 * The main screen displays:
 * - 4 throttle meters in a 2x2 grid (left half)
 * - Track power controls (right side) - uses JMRI JSON API
 * - Settings button to access WiFi configuration
 * 
 * This replaces the legacy test_throttle_screen.c with a proper C++ implementation.
 */
class MainScreen {
public:
    MainScreen();
    ~MainScreen();
    
    // Delete copy/move constructors
    MainScreen(const MainScreen&) = delete;
    MainScreen& operator=(const MainScreen&) = delete;
    
    /**
     * @brief Create and show the main screen
     * @param wiThrottleClient Optional WiThrottle client for DCC control
     * @param jmriClient Optional JMRI JSON client for power control
     * @return The LVGL screen object
     */
    lv_obj_t* create(WiThrottleClient* wiThrottleClient = nullptr, JmriJsonClient* jmriClient = nullptr);
    
    /**
     * @brief Update throttle displays with current state
     * @param throttleId Throttle ID (0-3)
     */
    void updateThrottle(int throttleId);
    
    /**
     * @brief Update all throttle displays
     */
    void updateAllThrottles();
    
    /**
     * @brief Get the throttle model by ID
     * @param throttleId Throttle ID (0-3)
     * @return Pointer to throttle or nullptr if invalid ID
     */
    Throttle* getThrottle(int throttleId);

private:
    void createTrackPowerControls(lv_obj_t* parent);
    void updateTrackPowerButton(lv_obj_t* button, JmriJsonClient::PowerState state);
    
    // Event handlers
    static void onSettingsButtonClicked(lv_event_t* e);
    static void onJmriButtonClicked(lv_event_t* e);
    static void onTrackPowerClicked(lv_event_t* e);
    static void onJmriPowerChanged(void* userData, const std::string& powerName, JmriJsonClient::PowerState state);
    static void onJmriConnectionChanged(void* userData, JmriJsonClient::ConnectionState state);
    
    // Test control event handlers
    static void onAcquireButtonClicked(lv_event_t* e);
    static void onSpeedButtonClicked(lv_event_t* e);
    static void onForwardButtonClicked(lv_event_t* e);
    static void onReverseButtonClicked(lv_event_t* e);
    static void onF0ButtonClicked(lv_event_t* e);
    static void onReleaseButtonClicked(lv_event_t* e);
    
    void updateConnectionStatus();
    
    // LVGL UI components
    lv_obj_t* m_screen;
    lv_obj_t* m_leftPanel;
    lv_obj_t* m_rightPanel;
    lv_obj_t* m_settingsButton;
    lv_obj_t* m_trackPowerButton;
    lv_obj_t* m_connectionStatusLabel;
    
    // Throttle meters (C++ widgets)
    std::array<std::unique_ptr<ThrottleMeter>, 4> m_throttleMeters;
    
    // Throttle models
    std::array<Throttle, 4> m_throttles;
    
    // Client references (not owned)
    WiThrottleClient* m_wiThrottleClient;
    JmriJsonClient* m_jmriClient;
    
    // Event handlers (moved to private section above)
    
    // UI builders
    void createLeftPanel();
    void createRightPanel();
    void createSettingsButton();
    void createThrottleMeters();
};
