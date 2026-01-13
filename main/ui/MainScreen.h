#pragma once

#include "lvgl.h"
#include "ThrottleMeter.h"
#include "../model/Throttle.h"
#include <array>
#include <memory>

/**
 * @brief Main application screen with throttle controls
 * 
 * The main screen displays:
 * - 4 throttle meters in a 2x2 grid (left half)
 * - Right half reserved for future use (loco details, function buttons)
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
     * @return The LVGL screen object
     */
    lv_obj_t* create();
    
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
    // LVGL UI components
    lv_obj_t* m_screen;
    lv_obj_t* m_leftPanel;
    lv_obj_t* m_rightPanel;
    lv_obj_t* m_settingsButton;
    
    // Throttle meters (C++ widgets)
    std::array<std::unique_ptr<ThrottleMeter>, 4> m_throttleMeters;
    
    // Throttle models
    std::array<Throttle, 4> m_throttles;
    
    // Event handlers
    static void onSettingsButtonClicked(lv_event_t* e);
    
    // UI builders
    void createLeftPanel();
    void createRightPanel();
    void createSettingsButton();
    void createThrottleMeters();
};
