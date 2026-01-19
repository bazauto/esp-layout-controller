#include "PowerStatusBar.h"
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
    , m_jmriClient(nullptr)
{
}

lv_obj_t* PowerStatusBar::create(lv_obj_t* parent, JmriJsonClient* jmriClient)
{
    m_jmriClient = jmriClient;

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
    lv_obj_set_style_text_color(m_connectionStatusLabel, lv_color_hex(0xFF0000), 0);
    lv_obj_center(m_connectionStatusLabel);

    if (m_jmriClient) {
        updateTrackPowerButton(m_jmriClient->getPower());
        updateConnectionStatus(m_jmriClient->getState());

        m_jmriClient->setPowerStateCallback([this](const std::string& powerName, JmriJsonClient::PowerState state) {
            (void)powerName;
            if (lvgl_port_lock(-1)) {
                updateTrackPowerButton(state);
                lvgl_port_unlock();
            }
        });

        m_jmriClient->setConnectionStateCallback([this](JmriJsonClient::ConnectionState state) {
            if (lvgl_port_lock(-1)) {
                updateConnectionStatus(state);
                lvgl_port_unlock();
            }
        });
    }

    ESP_LOGI(TAG, "Power/status bar created");
    return m_container;
}

void PowerStatusBar::onTrackPowerClicked(lv_event_t* e)
{
    auto* bar = static_cast<PowerStatusBar*>(lv_event_get_user_data(e));
    if (!bar || !bar->m_jmriClient || !bar->m_jmriClient->isConnected()) {
        ESP_LOGW(TAG, "Not connected to JMRI server");
        return;
    }

    auto currentState = bar->m_jmriClient->getPower();
    bool newState = (currentState != JmriJsonClient::PowerState::ON);

    ESP_LOGI(TAG, "Toggling track power: %s", newState ? "ON" : "OFF");
    bar->m_jmriClient->setPower(newState);
}

void PowerStatusBar::updateTrackPowerButton(JmriJsonClient::PowerState state)
{
    if (!m_trackPowerButton) return;

    lv_obj_t* label = lv_obj_get_child(m_trackPowerButton, 0);
    if (!label) return;

    uint32_t color;
    const char* stateText;

    switch (state) {
        case JmriJsonClient::PowerState::ON:
            color = 0x00AA00;
            stateText = "Power";
            break;
        case JmriJsonClient::PowerState::OFF:
            color = 0xAA0000;
            stateText = "Power";
            break;
        default:
            color = 0x888888;
            stateText = "Power";
            break;
    }

    lv_obj_set_style_bg_color(m_trackPowerButton, lv_color_hex(color), 0);
    lv_label_set_text(label, stateText);
}

void PowerStatusBar::updateConnectionStatus(JmriJsonClient::ConnectionState state)
{
    if (!m_connectionStatusLabel) return;

    const char* icon;
    const char* text;
    uint32_t color;

    switch (state) {
        case JmriJsonClient::ConnectionState::CONNECTED:
            icon = LV_SYMBOL_OK;
            text = " Connected";
            color = 0x00AA00;
            break;
        case JmriJsonClient::ConnectionState::CONNECTING:
            icon = LV_SYMBOL_REFRESH;
            text = " Connecting";
            color = 0xFFAA00;
            break;
        case JmriJsonClient::ConnectionState::FAILED:
            icon = LV_SYMBOL_WARNING;
            text = " Failed";
            color = 0xFF0000;
            break;
        case JmriJsonClient::ConnectionState::DISCONNECTED:
        default:
            icon = LV_SYMBOL_CLOSE;
            text = " Disconnected";
            color = 0x888888;
            break;
    }

    std::string statusText = std::string(icon) + text;
    lv_label_set_text(m_connectionStatusLabel, statusText.c_str());
    lv_obj_set_style_text_color(m_connectionStatusLabel, lv_color_hex(color), 0);
}
