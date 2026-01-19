#pragma once

#include "lvgl.h"
#include "components/ThrottleMeter.h"
#include "components/VirtualEncoderPanel.h"
#include "components/RosterCarousel.h"
#include "components/PowerStatusBar.h"
#include "../model/Throttle.h"
#include "../controller/ThrottleController.h"
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
     * @param wiThrottleClient WiThrottle client for DCC control
     * @param jmriClient JMRI JSON client for power control
     * @param throttleController Throttle controller (owned by application layer)
     * @return The LVGL screen object
     */
    lv_obj_t* create(WiThrottleClient* wiThrottleClient, JmriJsonClient* jmriClient, ThrottleController* throttleController);
    
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
    void createRosterPanel(lv_obj_t* parent);
    
    // Event handlers
    static void onSettingsButtonClicked(lv_event_t* e);
    static void onJmriButtonClicked(lv_event_t* e);
    
    // Test control event handlers
    static void onAcquireButtonClicked(lv_event_t* e);
    static void onSpeedButtonClicked(lv_event_t* e);
    static void onForwardButtonClicked(lv_event_t* e);
    static void onReverseButtonClicked(lv_event_t* e);
    static void onF0ButtonClicked(lv_event_t* e);
    static void onOldReleaseButtonClicked(lv_event_t* e);  // Old test button
    
    
    // LVGL UI components
    lv_obj_t* m_screen;
    lv_obj_t* m_leftPanel;
    lv_obj_t* m_rightPanel;
    lv_obj_t* m_settingsButton;
    std::unique_ptr<PowerStatusBar> m_powerStatusBar;
    std::unique_ptr<RosterCarousel> m_rosterCarousel;
    
    // Throttle meters (C++ widgets)
    std::array<std::unique_ptr<ThrottleMeter>, 4> m_throttleMeters;
    
    // Virtual encoder panel for testing
    std::unique_ptr<VirtualEncoderPanel> m_virtualEncoderPanel;
    
    // Throttle controller (not owned - managed at application layer)
    ThrottleController* m_throttleController;
    
    // Client references (not owned)
    WiThrottleClient* m_wiThrottleClient;
    JmriJsonClient* m_jmriClient;
    
    // Throttle UI event handlers
    static void onKnobIndicatorTouched(lv_event_t* e);
    static void onFunctionsButtonClicked(lv_event_t* e);
    static void onReleaseButtonClicked(lv_event_t* e);
    static void onUIUpdateNeeded(void* userData);
    
    // Virtual encoder callbacks
    static void onVirtualEncoderRotation(void* userData, int knobId, int delta);
    static void onVirtualEncoderPress(void* userData, int knobId);
    
    // Event handlers (moved to private section above)
    
    // UI builders
    void createLeftPanel();
    void createRightPanel();
    void createSettingsButton();
    void createThrottleMeters();
};
