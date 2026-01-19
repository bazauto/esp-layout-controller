#pragma once

#include "lvgl.h"

/**
 * @brief Virtual encoder UI for testing throttle interactions without hardware
 * 
 * Creates on-screen buttons to simulate:
 * - Knob rotation (CW/CCW)
 * - Knob button press
 * - Knob selection (Left/Right)
 * 
 * This allows testing the complete throttle control flow before
 * physical rotary encoder hardware arrives.
 */
class VirtualEncoderPanel {
public:
    VirtualEncoderPanel();
    ~VirtualEncoderPanel() = default;
    
    // Delete copy/move
    VirtualEncoderPanel(const VirtualEncoderPanel&) = delete;
    VirtualEncoderPanel& operator=(const VirtualEncoderPanel&) = delete;
    
    /**
     * @brief Create the virtual encoder panel
     * @param parent Parent LVGL object
     * @param rotationCallback Callback for rotation events (knobId, delta)
     * @param pressCallback Callback for button press events (knobId)
     * @param userData User data passed to callbacks
     * @return Panel container object
     */
    lv_obj_t* create(lv_obj_t* parent,
                     void (*rotationCallback)(void*, int, int),
                     void (*pressCallback)(void*, int),
                     void* userData);
    
    /**
     * @brief Get the panel container
     */
    lv_obj_t* getPanel() const { return m_panel; }
    
    /**
     * @brief Set which knob is currently selected (0 or 1)
     */
    void setActiveKnob(int knobId);
    
private:
    static void onKnobSelectClicked(lv_event_t* e);
    static void onRotateCWClicked(lv_event_t* e);
    static void onRotateCCWClicked(lv_event_t* e);
    static void onPressClicked(lv_event_t* e);
    
    void updateKnobButtons();
    
    lv_obj_t* m_panel;
    lv_obj_t* m_knobSelectButtons[2];  // Left/Right knob selection
    lv_obj_t* m_rotateCWButton;
    lv_obj_t* m_rotateCCWButton;
    lv_obj_t* m_pressButton;
    lv_obj_t* m_statusLabel;
    
    int m_activeKnob;  // 0 or 1
    
    void (*m_rotationCallback)(void*, int, int);
    void (*m_pressCallback)(void*, int);
    void* m_userData;
};
