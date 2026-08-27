#include "PowerStatusBar.h"
#include "../UiTheme.h"

#include <string>

#include "controller/ThrottleController.h"
#include "esp_log.h"
#include "lvgl_port.h"

extern "C" {
    bool lvgl_port_lock(int timeout_ms);
    void lvgl_port_unlock(void);
}

static const char* TAG = "PowerStatusBar";

PowerStatusBar::PowerStatusBar()
    : m_container(nullptr)
    , m_trackPowerButton(nullptr)
    , m_connectionStatusLabel(nullptr)
    , m_throttleController(nullptr)
{
}

PowerStatusBar::~PowerStatusBar()
{
    if (m_throttleController) {
        m_throttleController->setTrackPowerCallback(nullptr, nullptr);
    }
}

lv_obj_t* PowerStatusBar::create(lv_obj_t* parent, ThrottleController* throttleController)
{
    m_throttleController = throttleController;

    m_container = lv_obj_create(parent);
    lv_obj_set_size(m_container, LV_PCT(90), 50);
    lv_obj_align(m_container, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_flex_flow(m_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(m_container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(m_container, 5, 0);
    lv_obj_set_style_pad_column(m_container, 10, 0);

    m_trackPowerButton = lv_btn_create(m_container);
    lv_obj_set_size(m_trackPowerButton, 160, 40);
    lv_obj_t* powerLabel = lv_label_create(m_trackPowerButton);
    lv_label_set_text(powerLabel, "Track Power");
    lv_obj_center(powerLabel);
    lv_obj_add_event_cb(m_trackPowerButton, onTrackPowerClicked, LV_EVENT_CLICKED, this);

    m_connectionStatusLabel = lv_label_create(m_container);
    lv_label_set_text(m_connectionStatusLabel, LV_SYMBOL_CLOSE " Disconnected");
    lv_obj_set_style_text_color(m_connectionStatusLabel, UiTheme::colour(UiTheme::TEXT_ERROR), 0);
    lv_obj_center(m_connectionStatusLabel);

    if (m_throttleController) {
        // A transport with no power control gets no button at all, rather than
        // a dead one the operator can press and wonder about.
        if (!m_throttleController->supportsTrackPower()) {
            lv_obj_add_flag(m_trackPowerButton, LV_OBJ_FLAG_HIDDEN);
        }

        m_throttleController->setTrackPowerCallback(onTrackPowerChanged, this);
        refresh();
    }

    ESP_LOGI(TAG, "Power/status bar created");
    return m_container;
}

void PowerStatusBar::refresh()
{
    if (!m_throttleController) {
        return;
    }
    updateTrackPowerButton(m_throttleController->getTrackPower());
    updateConnectionStatus(m_throttleController->isConnected());
}

void PowerStatusBar::onTrackPowerChanged(void* userData, ThrottleBackend::TrackPower state)
{
    auto* bar = static_cast<PowerStatusBar*>(userData);
    if (!bar) {
        return;
    }
    // Arrives on a transport task, so the LVGL lock is required. 200 ms and
    // skip on contention -- a missed repaint is corrected by the next event.
    if (lvgl_port_lock(200)) {
        bar->updateTrackPowerButton(state);
        lvgl_port_unlock();
    }
}

void PowerStatusBar::onTrackPowerClicked(lv_event_t* e)
{
    auto* bar = static_cast<PowerStatusBar*>(lv_event_get_user_data(e));
    if (!bar || !bar->m_throttleController) {
        return;
    }

    if (!bar->m_throttleController->isConnected()) {
        ESP_LOGW(TAG, "Not connected; ignoring track power press");
        return;
    }

    // UNKNOWN turns power on: the useful thing to do when nobody has said what
    // the rails are doing is to energise them, and the operator can press
    // again to turn it off once the state is known.
    const ThrottleBackend::TrackPower current = bar->m_throttleController->getTrackPower();
    const bool newState = (current != ThrottleBackend::TrackPower::ON);

    ESP_LOGI(TAG, "Toggling track power: %s", newState ? "ON" : "OFF");

    // Returns immediately; the write happens on its own task because the
    // orchestrator's power command is a blocking HTTP round trip and this is
    // an LVGL event handler (F-05). The button repaints when the layout says
    // it changed, not when we asked.
    bar->m_throttleController->requestTrackPower(newState);
}

void PowerStatusBar::updateTrackPowerButton(ThrottleBackend::TrackPower state)
{
    if (!m_trackPowerButton) return;

    lv_obj_t* label = lv_obj_get_child(m_trackPowerButton, 0);
    if (!label) return;

    uint32_t color;
    const char* stateText;

    switch (state) {
        case ThrottleBackend::TrackPower::ON:
            color = UiTheme::STATE_ACTIVE;
            stateText = "Power On";
            break;
        case ThrottleBackend::TrackPower::OFF:
            color = UiTheme::STATE_FAULT;
            stateText = "Power Off";
            break;
        default:
            // Not "off": nothing has told us yet, and showing it as off would
            // claim the rails are dead when nobody knows.
            color = UiTheme::STATE_INACTIVE;
            stateText = "Power ?";
            break;
    }

    lv_obj_set_style_bg_color(m_trackPowerButton, lv_color_hex(color), 0);
    lv_label_set_text(label, stateText);
}

void PowerStatusBar::updateConnectionStatus(bool connected)
{
    if (!m_connectionStatusLabel) return;

    const char* icon = connected ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE;
    const char* text = connected ? " Connected" : " Disconnected";
    const uint32_t color = connected ? UiTheme::TEXT_OK : UiTheme::TEXT_MUTED;

    const std::string statusText = std::string(icon) + text;
    lv_label_set_text(m_connectionStatusLabel, statusText.c_str());
    lv_obj_set_style_text_color(m_connectionStatusLabel, lv_color_hex(color), 0);
}
