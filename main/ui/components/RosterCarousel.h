#pragma once

#include "lvgl.h"
#include "controller/ThrottleController.h"

/**
 * @brief Roster carousel widget for loco selection
 *
 * Displays a single large loco name with smaller ID and position,
 * plus left/right arrows to indicate more entries.
 */
class RosterCarousel {
public:
    RosterCarousel();
    ~RosterCarousel() = default;

    // Delete copy/move
    RosterCarousel(const RosterCarousel&) = delete;
    RosterCarousel& operator=(const RosterCarousel&) = delete;

    /**
     * @brief Create the carousel panel
     * @param parent Parent LVGL object
     * @return Panel container object
     */
    lv_obj_t* create(lv_obj_t* parent);

    /**
     * @brief Update carousel based on current selection
     * @param controller Throttle controller for roster data
     */
    void update(ThrottleController* controller);

private:
    lv_obj_t* m_panel;
    lv_obj_t* m_currentLabel;
    lv_obj_t* m_idLabel;
    lv_obj_t* m_positionLabel;
    lv_obj_t* m_leftArrow;
    lv_obj_t* m_rightArrow;

    int m_lastRosterIndex;
};
