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
    
private:
    // LVGL objects
    lv_obj_t* m_container;
    lv_obj_t* m_meter;
    lv_meter_scale_t* m_scaleId;
    lv_meter_indicator_t* m_needle;
    lv_obj_t* m_valueLabel;
    lv_obj_t* m_unitLabel;
    
    // State
    int32_t m_min;
    int32_t m_max;
    int32_t m_value;
    float m_scale;
    bool m_animRunning;
    
    // Constants
    static constexpr lv_coord_t BASE_SIZE = 200;
    
    // Animation callback
    static void animationCallback(void* var, int32_t value);
};
