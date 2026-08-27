#include "OrchestratorConfigScreen.h"

#include "../communication/OrchestratorClient.h"
#include "../controller/AppController.h"
#include "../controller/WiFiController.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl_port.h"
#include "wrappers/jmri_config_wrapper.h"

static const char* TAG = "OrchConfigScreen";

namespace {
constexpr int SCREEN_WIDTH = 800;
constexpr int SCREEN_HEIGHT = 480;
}  // namespace

OrchestratorConfigScreen::OrchestratorConfigScreen(OrchestratorClient* client,
                                                   WiFiController* wifiController)
    : m_screen(nullptr)
    , m_hostInput(nullptr)
    , m_portInput(nullptr)
    , m_usernameInput(nullptr)
    , m_passwordInput(nullptr)
    , m_statusValue(nullptr)
    , m_connectButton(nullptr)
    , m_keyboard(nullptr)
    , m_statusTimer(nullptr)
    , m_connectInProgress(false)
    , m_client(client)
    , m_wifiController(wifiController)
{
}

OrchestratorConfigScreen::~OrchestratorConfigScreen()
{
    stopStatusTimer();
}

void OrchestratorConfigScreen::statusTimerCb(lv_timer_t* timer)
{
    // Runs on the LVGL task, so no lock is needed here.
    // LVGL 8.4 has no lv_timer_get_user_data; the field is read directly.
    auto* self = static_cast<OrchestratorConfigScreen*>(timer->user_data);
    if (self) {
        self->updateStatus();
    }
}

void OrchestratorConfigScreen::stopStatusTimer()
{
    if (m_statusTimer) {
        lv_timer_del(m_statusTimer);
        m_statusTimer = nullptr;
    }
}

void OrchestratorConfigScreen::clearUiPointers()
{
    m_screen = nullptr;
    m_hostInput = nullptr;
    m_portInput = nullptr;
    m_usernameInput = nullptr;
    m_passwordInput = nullptr;
    m_statusValue = nullptr;
    m_connectButton = nullptr;
    m_keyboard = nullptr;
}

lv_obj_t* OrchestratorConfigScreen::create()
{
    m_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(m_screen, lv_color_hex(0x1a1a1a), 0);
    lv_scr_load(m_screen);

    lv_obj_t* content = lv_obj_create(m_screen);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, PADDING);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(content, PADDING, 0);
    lv_obj_set_style_pad_row(content, 6, 0);

    createHeader(content);
    createFields(content);
    createButtons(content);
    createKeyboard();

    loadSettings();

    // Polled on an LVGL timer rather than by registering on the client's
    // connection callback. That callback is a single slot which the active
    // ThrottleBackend owns -- the knob gating depends on it -- and taking it
    // here would silently break knob enablement for the rest of the session.
    // A twice-a-second poll of a status label costs nothing and steals nothing.
    m_statusTimer = lv_timer_create(statusTimerCb, 500, this);

    updateStatus();
    return m_screen;
}

void OrchestratorConfigScreen::createHeader(lv_obj_t* parent)
{
    lv_obj_t* title = lv_label_create(parent);
    lv_label_set_text(title, "Layout Orchestrator");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);

    lv_obj_t* subtitle = lv_label_create(parent);
    lv_label_set_text(subtitle, "WebSocket control plane. Select it as the transport on the settings screen.");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x999999), 0);

    lv_obj_t* statusRow = lv_obj_create(parent);
    lv_obj_remove_style_all(statusRow);
    lv_obj_set_size(statusRow, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(statusRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(statusRow, 8, 0);

    lv_obj_t* statusLabel = lv_label_create(statusRow);
    lv_label_set_text(statusLabel, "Status:");
    lv_obj_set_style_text_color(statusLabel, lv_color_hex(0xcccccc), 0);

    m_statusValue = lv_label_create(statusRow);
    lv_label_set_text(m_statusValue, "disconnected");
    lv_obj_set_style_text_color(m_statusValue, lv_color_hex(0xcccccc), 0);
}

void OrchestratorConfigScreen::createFields(lv_obj_t* parent)
{
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(row, 20, 0);

    lv_obj_t* left = lv_obj_create(row);
    lv_obj_remove_style_all(left);
    lv_obj_set_size(left, LV_PCT(48), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left, 4, 0);

    lv_obj_t* right = lv_obj_create(row);
    lv_obj_remove_style_all(right);
    lv_obj_set_size(right, LV_PCT(48), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right, 4, 0);

    auto addLabel = [](lv_obj_t* parentObj, const char* text) {
        lv_obj_t* label = lv_label_create(parentObj);
        lv_label_set_text(label, text);
        lv_obj_set_style_text_color(label, lv_color_hex(0xcccccc), 0);
        lv_obj_set_width(label, LV_PCT(100));
    };

    addLabel(left, "Host:");
    m_hostInput = lv_textarea_create(left);
    lv_textarea_set_one_line(m_hostInput, true);
    lv_textarea_set_placeholder_text(m_hostInput, "172.18.10.240");
    lv_obj_set_width(m_hostInput, LV_PCT(100));
    lv_obj_add_event_cb(m_hostInput, onTextAreaFocused, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(m_hostInput, onTextAreaDefocused, LV_EVENT_DEFOCUSED, this);

    addLabel(left, "Port:");
    m_portInput = lv_textarea_create(left);
    lv_textarea_set_one_line(m_portInput, true);
    lv_textarea_set_placeholder_text(m_portInput, "3000");
    lv_textarea_set_accepted_chars(m_portInput, "0123456789");
    lv_textarea_set_max_length(m_portInput, 5);
    lv_obj_set_width(m_portInput, LV_PCT(100));
    lv_obj_add_event_cb(m_portInput, onTextAreaFocused, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(m_portInput, onTextAreaDefocused, LV_EVENT_DEFOCUSED, this);

    addLabel(right, "Operator username:");
    m_usernameInput = lv_textarea_create(right);
    lv_textarea_set_one_line(m_usernameInput, true);
    lv_textarea_set_placeholder_text(m_usernameInput, "throttle");
    lv_obj_set_width(m_usernameInput, LV_PCT(100));
    lv_obj_add_event_cb(m_usernameInput, onTextAreaFocused, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(m_usernameInput, onTextAreaDefocused, LV_EVENT_DEFOCUSED, this);

    addLabel(right, "Password:");
    m_passwordInput = lv_textarea_create(right);
    lv_textarea_set_one_line(m_passwordInput, true);
    // Masked on screen. Still plaintext in NVS -- the accepted F-18 risk.
    lv_textarea_set_password_mode(m_passwordInput, true);
    lv_obj_set_width(m_passwordInput, LV_PCT(100));
    lv_obj_add_event_cb(m_passwordInput, onTextAreaFocused, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(m_passwordInput, onTextAreaDefocused, LV_EVENT_DEFOCUSED, this);
}

void OrchestratorConfigScreen::createButtons(lv_obj_t* parent)
{
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_set_style_pad_top(row, 8, 0);

    auto addButton = [&](const char* text, lv_event_cb_t handler, uint32_t colour) {
        lv_obj_t* button = lv_btn_create(row);
        lv_obj_set_size(button, 180, BUTTON_HEIGHT);
        lv_obj_set_style_bg_color(button, lv_color_hex(colour), 0);
        lv_obj_add_event_cb(button, handler, LV_EVENT_CLICKED, this);
        lv_obj_t* label = lv_label_create(button);
        lv_label_set_text(label, text);
        lv_obj_center(label);
        return button;
    };

    addButton("Save", onSaveClicked, 0x2d6a4f);
    m_connectButton = addButton("Save & Connect", onConnectClicked, 0x1d4e89);
    addButton("Back", onBackClicked, 0x555555);
}

void OrchestratorConfigScreen::createKeyboard()
{
    m_keyboard = lv_keyboard_create(m_screen);
    lv_obj_set_size(m_keyboard, SCREEN_WIDTH, SCREEN_HEIGHT / 2);
    lv_obj_align(m_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(m_keyboard, LV_OBJ_FLAG_HIDDEN);
}

void OrchestratorConfigScreen::showKeyboard(lv_obj_t* textarea)
{
    if (m_keyboard && textarea) {
        lv_keyboard_set_textarea(m_keyboard, textarea);
        lv_obj_clear_flag(m_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

void OrchestratorConfigScreen::hideKeyboard()
{
    if (m_keyboard) {
        lv_obj_add_flag(m_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

std::string OrchestratorConfigScreen::textOf(lv_obj_t* textarea) const
{
    if (!textarea) {
        return std::string();
    }
    const char* text = lv_textarea_get_text(textarea);
    return text ? std::string(text) : std::string();
}

void OrchestratorConfigScreen::loadSettings()
{
    const TransportSettings settings = TransportSettings::load();

    if (m_hostInput) {
        lv_textarea_set_text(m_hostInput, settings.host.c_str());
    }
    if (m_portInput) {
        lv_textarea_set_text(m_portInput, std::to_string(settings.port).c_str());
    }
    if (m_usernameInput) {
        lv_textarea_set_text(m_usernameInput, settings.username.c_str());
    }
    if (m_passwordInput) {
        lv_textarea_set_text(m_passwordInput, settings.password.c_str());
    }
}

void OrchestratorConfigScreen::saveSettings()
{
    // Read-modify-write: the transport *choice* is owned by the settings
    // screen, so loading first stops saving here from silently reverting it.
    TransportSettings settings = TransportSettings::load();

    settings.host = textOf(m_hostInput);
    settings.username = textOf(m_usernameInput);
    settings.password = textOf(m_passwordInput);

    const std::string portText = textOf(m_portInput);
    if (!portText.empty()) {
        const long port = strtol(portText.c_str(), nullptr, 10);
        if (port > 0 && port <= 65535) {
            settings.port = static_cast<uint16_t>(port);
        }
    }

    settings.save();
}

void OrchestratorConfigScreen::updateStatus()
{
    if (!m_statusValue) {
        return;
    }

    if (!m_client) {
        lv_label_set_text(m_statusValue, "not the selected transport");
        lv_obj_set_style_text_color(m_statusValue, lv_color_hex(0x999999), 0);
        return;
    }

    const OrchestratorClient::ConnectionState state = m_client->getState();
    lv_label_set_text(m_statusValue, OrchestratorClient::stateName(state));

    uint32_t colour = 0xcccccc;
    switch (state) {
        case OrchestratorClient::ConnectionState::CONNECTED:      colour = 0x40c057; break;
        case OrchestratorClient::ConnectionState::AUTHENTICATING:
        case OrchestratorClient::ConnectionState::CONNECTING:     colour = 0xfab005; break;
        case OrchestratorClient::ConnectionState::FAILED:         colour = 0xe03131; break;
        default:                                                  colour = 0x999999; break;
    }
    lv_obj_set_style_text_color(m_statusValue, lv_color_hex(colour), 0);
}

// --- Events ----------------------------------------------------------------

void OrchestratorConfigScreen::onTextAreaFocused(lv_event_t* e)
{
    auto* self = static_cast<OrchestratorConfigScreen*>(lv_event_get_user_data(e));
    if (self) {
        self->showKeyboard(static_cast<lv_obj_t*>(lv_event_get_target(e)));
    }
}

void OrchestratorConfigScreen::onTextAreaDefocused(lv_event_t* e)
{
    auto* self = static_cast<OrchestratorConfigScreen*>(lv_event_get_user_data(e));
    if (self) {
        self->hideKeyboard();
    }
}

void OrchestratorConfigScreen::onSaveClicked(lv_event_t* e)
{
    auto* self = static_cast<OrchestratorConfigScreen*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }
    self->hideKeyboard();
    self->saveSettings();
    ESP_LOGI(TAG, "Orchestrator settings saved");
}

void OrchestratorConfigScreen::connectTask(void* arg)
{
    auto* self = static_cast<OrchestratorConfigScreen*>(arg);

    if (self->m_client) {
        const TransportSettings settings = TransportSettings::load();
        esp_err_t err = self->m_client->connect(settings.host, settings.port,
                                                settings.username, settings.password);
        if (err == ESP_OK) {
            self->m_client->refreshRoster();
        }
    }

    self->m_connectInProgress = false;

    // The status timer picks the result up; nothing to paint from this task.
    vTaskDelete(nullptr);
}

void OrchestratorConfigScreen::onConnectClicked(lv_event_t* e)
{
    auto* self = static_cast<OrchestratorConfigScreen*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

    self->hideKeyboard();
    self->saveSettings();

    if (!self->m_client) {
        ESP_LOGW(TAG, "Orchestrator is not the selected transport; nothing to connect");
        return;
    }

    if (self->m_connectInProgress.exchange(true)) {
        ESP_LOGW(TAG, "Connect already in progress");
        return;
    }

    // Off the LVGL task: the login is a blocking HTTP round trip and the roster
    // is two more. Blocking here would freeze every throttle at once (F-05).
    if (xTaskCreate(connectTask, "orch_ui_conn", 6144, self, 5, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create connect task");
        self->m_connectInProgress = false;
    }
}

void OrchestratorConfigScreen::onBackClicked(lv_event_t* e)
{
    auto* self = static_cast<OrchestratorConfigScreen*>(lv_event_get_user_data(e));
    if (!self) {
        return;
    }

    self->hideKeyboard();

    // Stop the poll before the widgets go, or it paints a deleted label.
    self->stopStatusTimer();

    show_jmri_config_screen();

    if (self->m_screen) {
        lv_obj_del_async(self->m_screen);
        self->clearUiPointers();
    }
}
