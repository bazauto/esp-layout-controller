#pragma once

#include "lvgl.h"
#include <cstdint>

/**
 * @brief Throttle meter widget - displays speed with a circular gauge
 * 
 * Features:
 * - Circular meter with needle indicator
 * - Color-coded zones (red, blue, green)
 * - Numeric value display
 * - Configurable range and scale
 * - Optional animation support
 */
class ThrottleMeter {
public:
    /**
     * @brief Create a throttle meter widget
     * @param parent Parent LVGL object
     * @param scale Scale multiplier (1.0 = default size)
     */
    ThrottleMeter(lv_obj_t* parent, float scale = 1.0f);
    ~ThrottleMeter();
    
    // Delete copy/move constructors
    ThrottleMeter(const ThrottleMeter&) = delete;
    ThrottleMeter& operator=(const ThrottleMeter&) = delete;
    
    /**
     * @brief Set the current value
     * @param value Value to display (clamped to min/max range)
     */
    void setValue(int32_t value);
    
    /**
     * @brief Get the current value
     * @return Current value
     */
    int32_t getValue() const { return m_value; }
    
    /**
     * @brief Set the display scale
     * @param scale Scale multiplier (1.0 = 200px base size)
     */
    void setScale(float scale);
    
    /**
     * @brief Set the value range
     * @param min Minimum value
     * @param max Maximum value
     */
    void setRange(int32_t min, int32_t max);
    
    /**
     * @brief Set the unit text displayed next to the value
     * @param unit Unit string (e.g., "mph", "km/h")
     */
    void setUnit(const char* unit);
    
    /**
     * @brief Start automatic animation cycling between min and max
     * @param timeMs Forward animation duration
     * @param playbackMs Return animation duration
     */
    void startAnimation(uint32_t timeMs, uint32_t playbackMs);
    
    /**
     * @brief Stop animation
     */
    void stopAnimation();
    
    /**
     * @brief Get the container object
     * @return LVGL container object
     */
    lv_obj_t* getContainer() const { return m_container; }
    
    /**
     * @brief Set locomotive information
     * @param name Locomotive name
     * @param address Locomotive address
     */
    void setLocomotive(const char* name, int address);
    
    /**
     * @brief Clear locomotive information
     */
    void clearLocomotive();
    
    /**
     * @brief Set which knob is assigned (0=left, 1=right, -1=none)
     * @param knobId Knob ID or -1 for none
     */
    void setAssignedKnob(int knobId);
    
    /**
     * @brief Set knob indicator availability
     * @param knobId Knob ID (0 or 1)
     * @param available True if knob can be assigned, false if disabled
     */
    void setKnobAvailable(int knobId, bool available);
    
    /**
     * @brief Set knob indicator touch callback
     * @param callback Callback function
     * @param userData User data passed to callback
     */
    void setKnobTouchCallback(lv_event_cb_t callback, void* userData);
    
    /**
     * @brief Set functions button callback
     * @param callback Callback function
     * @param userData User data passed to callback
     */
    void setFunctionsCallback(lv_event_cb_t callback, void* userData);
    
    /**
     * @brief Set release button callback
     * @param callback Callback function
     * @param userData User data passed to callback
     */
    void setReleaseCallback(lv_event_cb_t callback, void* userData);

private:
    void createKnobIndicators();
    void createButtons();
    void updateKnobIndicators();
    
    // LVGL objects
    lv_obj_t* m_container;
    lv_obj_t* m_meter;
    lv_meter_scale_t* m_scaleId;
    lv_meter_indicator_t* m_needle;
    lv_obj_t* m_valueLabel;
    lv_obj_t* m_unitLabel;
    lv_obj_t* m_locoLabel;           // Loco name and address
    lv_obj_t* m_knobIndicators[2];   // L and R indicators
    lv_obj_t* m_functionsButton;
    lv_obj_t* m_releaseButton;
    
    // State
    int32_t m_min;
    int32_t m_max;
    int32_t m_value;
    float m_scale;
    bool m_animRunning;
    int m_assignedKnob;              // -1, 0, or 1
    bool m_knobAvailable[2];         // Availability for each knob
    void* m_userData;                 // User data for callbacks
    
    // Constants
    static constexpr lv_coord_t BASE_SIZE = 200;
    
    // Animation callback
    static void animationCallback(void* var, int32_t value);
};
