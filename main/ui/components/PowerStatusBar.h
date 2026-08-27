#pragma once

#include "lvgl.h"
#include "communication/ThrottleBackend.h"

class ThrottleController;

/**
 * @brief Track power button + connection status bar
 *
 * Driven by the active transport through `ThrottleController`, never by a
 * concrete client. It used to read `JmriJsonClient` directly, which meant that
 * under the orchestrator transport the button did nothing and the label read
 * "Disconnected" while the layout was in fact connected.
 */
class PowerStatusBar {
public:
    PowerStatusBar();
    ~PowerStatusBar();

    // Delete copy/move
    PowerStatusBar(const PowerStatusBar&) = delete;
    PowerStatusBar& operator=(const PowerStatusBar&) = delete;

    /**
     * @brief Create the status bar
     * @param parent Parent LVGL object
     * @param throttleController Controller owning the active transport (not owned)
     * @return Container object
     */
    lv_obj_t* create(lv_obj_t* parent, ThrottleController* throttleController);

    /** Repaint from current transport state. Must hold the LVGL lock. */
    void refresh();

private:
    static void onTrackPowerClicked(lv_event_t* e);
    static void onTrackPowerChanged(void* userData, ThrottleBackend::TrackPower state);

    void updateTrackPowerButton(ThrottleBackend::TrackPower state);
    void updateConnectionStatus(bool connected);

    lv_obj_t* m_container;
    lv_obj_t* m_trackPowerButton;
    lv_obj_t* m_connectionStatusLabel;

    ThrottleController* m_throttleController;
};
