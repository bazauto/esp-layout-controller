#pragma once

#include "lvgl.h"
#include "communication/JmriJsonClient.h"

/**
 * @brief Track power button + connection status bar
 */
class PowerStatusBar {
public:
    PowerStatusBar();
    ~PowerStatusBar() = default;

    // Delete copy/move
    PowerStatusBar(const PowerStatusBar&) = delete;
    PowerStatusBar& operator=(const PowerStatusBar&) = delete;

    /**
     * @brief Create the status bar
     * @param parent Parent LVGL object
     * @param jmriClient JMRI JSON client (not owned)
     * @return Container object
     */
    lv_obj_t* create(lv_obj_t* parent, JmriJsonClient* jmriClient);

private:
    static void onTrackPowerClicked(lv_event_t* e);

    void updateTrackPowerButton(JmriJsonClient::PowerState state);
    void updateConnectionStatus(JmriJsonClient::ConnectionState state);

    lv_obj_t* m_container;
    lv_obj_t* m_trackPowerButton;
    lv_obj_t* m_connectionStatusLabel;

    JmriJsonClient* m_jmriClient;
};
