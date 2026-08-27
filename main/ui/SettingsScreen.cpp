#include "SettingsScreen.h"
#include "UiTheme.h"

#include <cstdio>
#include <cstdlib>

#include "../communication/JmriJsonClient.h"
#include "../communication/WiThrottleClient.h"
#include "../controller/ThrottleController.h"
#include "../controller/WiFiController.h"
#include "../hardware/RotaryEncoderHal.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "lvgl_port.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "wrappers/jmri_config_wrapper.h"
#include "wrappers/main_screen_wrapper.h"
#include "wrappers/orchestrator_config_wrapper.h"

static const char* TAG = "SettingsScreen";

namespace {
// Speed steps stays in the `jmri` namespace even though it is a device setting
// rather than a JMRI one: it predates the split, and moving it would strand the
// value already saved on every device in the field for no functional gain.
constexpr const char* NVS_NAMESPACE = "jmri";
constexpr const char* NVS_KEY_SPEED_STEPS = "speed_steps";
}  // namespace

SettingsScreen::SettingsScreen(ThrottleController* throttleController,
                               WiFiController* wifiController,
                               RotaryEncoderHal* encoderHal,
                               JmriJsonClient* jsonClient,
                               WiThrottleClient* wiThrottleClient)
    : m_screen(nullptr)
    , m_transportDropdown(nullptr)
    , m_transportNoteLabel(nullptr)
    , m_speedStepsInput(nullptr)
    , m_keyboard(nullptr)
    , m_statusTimer(nullptr)
    , m_statusSoftwareValue(nullptr)
    , m_statusHardwareValue(nullptr)
    , m_statusWifiValue(nullptr)
    , m_statusTransportValue(nullptr)
    , m_statusLinkValue(nullptr)
    , m_statusJsonValue(nullptr)
    , m_statusEncoder1Value(nullptr)
    , m_statusEncoder2Value(nullptr)
    , m_throttleController(throttleController)
    , m_wifiController(wifiController)
    , m_encoderHal(encoderHal)
    , m_jsonClient(jsonClient)
    , m_wiThrottleClient(wiThrottleClient)
    , m_shownTransport(ThrottleTransport::WITHROTTLE)
{
}

SettingsScreen::~SettingsScreen()
{
    stopStatusTimer();
}

void SettingsScreen::clearUiPointers()
{
    m_screen = nullptr;
    m_transportDropdown = nullptr;
    m_transportNoteLabel = nullptr;
    m_speedStepsInput = nullptr;
    m_keyboard = nullptr;
    m_statusSoftwareValue = nullptr;
    m_statusHardwareValue = nullptr;
    m_statusWifiValue = nullptr;
    m_statusTransportValue = nullptr;
    m_statusLinkValue = nullptr;
    m_statusJsonValue = nullptr;
    m_statusEncoder1Value = nullptr;
    m_statusEncoder2Value = nullptr;
}

lv_obj_t* SettingsScreen::create()
{
    // Which transport is selected decides which status rows exist, so it is
    // read once here and the rows built from it.
    m_shownTransport = TransportSettings::load().transport;

    m_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(m_screen, UiTheme::colour(UiTheme::SURFACE_SCREEN), 0);

    const int buttonAreaHeight = BUTTON_HEIGHT + 2 * PADDING;

    lv_obj_t* content = lv_obj_create(m_screen);
    lv_obj_set_size(content, SCREEN_WIDTH, SCREEN_HEIGHT - buttonAreaHeight);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(content, 6, 0);
    lv_obj_set_style_pad_row(content, 6, 0);

    lv_obj_t* buttonContainer = lv_obj_create(m_screen);
    lv_obj_set_size(buttonContainer, SCREEN_WIDTH, buttonAreaHeight);
    lv_obj_align(buttonContainer, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(buttonContainer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(buttonContainer, 0, 0);

    createTitle(content);
    createTransportSection(content);
    createDeviceSection(content);
    createSystemStatusSection(content);
    createButtonSection(buttonContainer);
    createKeyboard();

    loadSettings();

    // Polled rather than registered on any client's connection callback: those
    // are single slots owned by the active backend, and taking one here would
    // break knob gating for the rest of the session.
    m_statusTimer = lv_timer_create(statusTimerCb, 500, this);

    updateStatus();
    lv_scr_load(m_screen);
    return m_screen;
}

void SettingsScreen::createTitle(lv_obj_t* parent)
{
    lv_obj_t* title = lv_label_create(parent);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
}

void SettingsScreen::createTransportSection(lv_obj_t* parent)
{
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, 0);

    lv_obj_t* label = lv_label_create(row);
    lv_label_set_text(label, "Transport:");

    // A choice, not two independent enables: exactly one transport drives locos
    // at a time, and only the selected one's network stack is brought up.
    m_transportDropdown = lv_dropdown_create(row);
    lv_dropdown_set_options(m_transportDropdown, "WiThrottle (JMRI)\nLayout Orchestrator");
    lv_obj_set_width(m_transportDropdown, 230);
    lv_obj_add_event_cb(m_transportDropdown, onTransportChanged, LV_EVENT_VALUE_CHANGED, this);

    // Both config screens are always reachable, so a transport can be set up
    // before it is switched to -- the dropdown refuses an unconfigured one.
    auto addButton = [&](const char* text, lv_event_cb_t handler) {
        lv_obj_t* button = lv_btn_create(row);
        lv_obj_set_size(button, 160, 40);
        lv_obj_set_style_bg_color(button, UiTheme::colour(UiTheme::BUTTON_PRIMARY), 0);
        lv_obj_add_event_cb(button, handler, LV_EVENT_CLICKED, this);
        lv_obj_t* buttonLabel = lv_label_create(button);
        lv_label_set_text(buttonLabel, text);
        lv_obj_center(buttonLabel);
    };

    addButton("JMRI...", onJmriSettingsClicked);
    addButton("Orchestrator...", onOrchestratorSettingsClicked);

    // Standing advice, not just feedback after the fact: the operator needs to
    // know a restart is coming *before* they change the dropdown, not after.
    // onTransportChanged replaces this text once something actually happens.
    m_transportNoteLabel = lv_label_create(parent);
    lv_label_set_text(m_transportNoteLabel,
                      "Changing the transport takes effect after a restart.");
    lv_obj_set_style_text_color(m_transportNoteLabel, UiTheme::colour(UiTheme::TEXT_MUTED), 0);
    lv_obj_set_width(m_transportNoteLabel, LV_PCT(100));
}

void SettingsScreen::createDeviceSection(lv_obj_t* parent)
{
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, 0);

    lv_obj_t* label = lv_label_create(row);
    lv_label_set_text(label, "Speed steps per click:");

    // A property of the encoder, not of either transport, which is why it lives
    // here rather than on a transport's own screen.
    m_speedStepsInput = lv_textarea_create(row);
    lv_textarea_set_one_line(m_speedStepsInput, true);
    lv_textarea_set_accepted_chars(m_speedStepsInput, "0123456789");
    lv_textarea_set_max_length(m_speedStepsInput, 2);
    lv_obj_set_width(m_speedStepsInput, 90);
    lv_obj_add_event_cb(m_speedStepsInput, onTextAreaFocused, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(m_speedStepsInput, onTextAreaDefocused, LV_EVENT_DEFOCUSED, this);
}

void SettingsScreen::addStatusRow(lv_obj_t* parent, const char* label, lv_obj_t** valueLabel)
{
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t* labelObj = lv_label_create(row);
    lv_label_set_text(labelObj, label);

    *valueLabel = lv_label_create(row);
    lv_label_set_text(*valueLabel, "-");
}

void SettingsScreen::createSystemStatusSection(lv_obj_t* parent)
{
    lv_obj_t* header = lv_label_create(parent);
    lv_label_set_text(header, "System Status");
    lv_obj_set_style_text_font(header, &lv_font_montserrat_20, 0);

    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_remove_style_all(container);
    lv_obj_set_width(container, LV_PCT(100));
    lv_obj_set_height(container, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_style_pad_row(container, 2, 0);

    addStatusRow(container, "Software", &m_statusSoftwareValue);
    addStatusRow(container, "Hardware", &m_statusHardwareValue);
    addStatusRow(container, "WiFi", &m_statusWifiValue);
    addStatusRow(container, "Transport", &m_statusTransportValue);

    // Only the selected transport's links are listed. Showing rows for a stack
    // the device is not bringing up would report "Disconnected" for something
    // that was never meant to connect.
    if (m_shownTransport == ThrottleTransport::ORCHESTRATOR) {
        addStatusRow(container, "Control plane", &m_statusLinkValue);
    } else {
        addStatusRow(container, "WiThrottle", &m_statusLinkValue);
        addStatusRow(container, "JMRI JSON", &m_statusJsonValue);
    }

    addStatusRow(container, "Encoder 1", &m_statusEncoder1Value);
    addStatusRow(container, "Encoder 2", &m_statusEncoder2Value);
}

void SettingsScreen::createButtonSection(lv_obj_t* parent)
{
    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_remove_style_all(container);
    lv_obj_set_size(container, LV_PCT(100), BUTTON_HEIGHT);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t* backButton = lv_btn_create(container);
    lv_obj_set_size(backButton, 200, BUTTON_HEIGHT);
    lv_obj_add_event_cb(backButton, onBackClicked, LV_EVENT_CLICKED, this);
    lv_obj_t* label = lv_label_create(backButton);
    lv_label_set_text(label, "Back");
    lv_obj_center(label);
}

void SettingsScreen::createKeyboard()
{
    m_keyboard = lv_keyboard_create(m_screen);
    lv_obj_set_size(m_keyboard, SCREEN_WIDTH, SCREEN_HEIGHT / 2);
    lv_obj_align(m_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(m_keyboard, LV_OBJ_FLAG_HIDDEN);
}

void SettingsScreen::showKeyboard(lv_obj_t* textarea)
{
    if (m_keyboard && textarea) {
        lv_keyboard_set_textarea(m_keyboard, textarea);
        lv_obj_clear_flag(m_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

void SettingsScreen::hideKeyboard()
{
    if (m_keyboard) {
        lv_obj_add_flag(m_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

void SettingsScreen::loadSettings()
{
    if (m_transportDropdown) {
        lv_dropdown_set_selected(
            m_transportDropdown,
            m_shownTransport == ThrottleTransport::ORCHESTRATOR ? 1 : 0);
    }

    if (m_speedStepsInput) {
        int32_t speedSteps = 4;
        nvs_handle_t handle;
        if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
            nvs_get_i32(handle, NVS_KEY_SPEED_STEPS, &speedSteps);
            nvs_close(handle);
        }
        char text[8];
        snprintf(text, sizeof(text), "%d", static_cast<int>(speedSteps));
        lv_textarea_set_text(m_speedStepsInput, text);
    }
}

void SettingsScreen::saveSpeedSteps()
{
    if (!m_speedStepsInput) {
        return;
    }

    const char* text = lv_textarea_get_text(m_speedStepsInput);
    int speedSteps = text ? atoi(text) : 4;
    if (speedSteps < 1) speedSteps = 1;
    if (speedSteps > 20) speedSteps = 20;

    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS to save speed steps");
        return;
    }
    nvs_set_i32(handle, NVS_KEY_SPEED_STEPS, speedSteps);
    nvs_commit(handle);
    nvs_close(handle);

    if (m_throttleController) {
        m_throttleController->reloadSpeedStepsFromNvs();
    }
    ESP_LOGI(TAG, "Speed steps saved: %d", speedSteps);
}

void SettingsScreen::updateStatus()
{
    if (m_statusSoftwareValue) {
        const esp_app_desc_t* appDesc = esp_app_get_description();
        lv_label_set_text(m_statusSoftwareValue, appDesc ? appDesc->version : "unknown");
    }

    if (m_statusHardwareValue) {
        esp_chip_info_t chipInfo{};
        esp_chip_info(&chipInfo);
        char hwLabel[32];
        snprintf(hwLabel, sizeof(hwLabel), "ESP32-S3 rev %d", chipInfo.revision);
        lv_label_set_text(m_statusHardwareValue, hwLabel);
    }

    if (m_statusWifiValue) {
        lv_label_set_text(m_statusWifiValue,
                          (m_wifiController && m_wifiController->isConnected())
                              ? "Connected" : "Disconnected");
    }

    if (m_statusTransportValue) {
        lv_label_set_text(m_statusTransportValue,
                          m_shownTransport == ThrottleTransport::ORCHESTRATOR
                              ? "Layout Orchestrator" : "WiThrottle (JMRI)");
    }

    if (m_statusLinkValue) {
        if (m_shownTransport == ThrottleTransport::ORCHESTRATOR) {
            // Read through the controller, so this reports the link the device
            // is actually driving locos over.
            const bool connected = m_throttleController && m_throttleController->isConnected();
            lv_label_set_text(m_statusLinkValue, connected ? "Connected" : "Disconnected");
            lv_obj_set_style_text_color(
                m_statusLinkValue,
                UiTheme::colour(connected ? UiTheme::TEXT_OK : UiTheme::TEXT_MUTED), 0);
        } else if (m_wiThrottleClient) {
            const char* text = "Disconnected";
            switch (m_wiThrottleClient->getState()) {
                case WiThrottleClient::ConnectionState::CONNECTING: text = "Connecting..."; break;
                case WiThrottleClient::ConnectionState::CONNECTED:  text = "Connected";     break;
                case WiThrottleClient::ConnectionState::FAILED:     text = "Failed";        break;
                default: break;
            }
            lv_label_set_text(m_statusLinkValue, text);
        }
    }

    if (m_statusJsonValue && m_jsonClient) {
        const char* text = "Disconnected";
        switch (m_jsonClient->getState()) {
            case JmriJsonClient::ConnectionState::CONNECTING: text = "Connecting..."; break;
            case JmriJsonClient::ConnectionState::CONNECTED:  text = "Connected";     break;
            case JmriJsonClient::ConnectionState::FAILED:     text = "Failed";        break;
            default: break;
        }
        lv_label_set_text(m_statusJsonValue, text);
    }

    lv_obj_t* const encoderLabels[] = { m_statusEncoder1Value, m_statusEncoder2Value };
    for (int i = 0; i < 2; ++i) {
        if (!encoderLabels[i]) {
            continue;
        }
        if (!m_encoderHal) {
            lv_label_set_text(encoderLabels[i], "Unavailable");
            continue;
        }
        const auto status = m_encoderHal->getStatus(i);
        char text[24];
        snprintf(text, sizeof(text), "0x%02X %s", status.address,
                 status.present ? "present" : "missing");
        lv_label_set_text(encoderLabels[i], text);
    }
}

void SettingsScreen::statusTimerCb(lv_timer_t* timer)
{
    // Runs on the LVGL task, so no lock is needed.
    auto* self = static_cast<SettingsScreen*>(timer->user_data);
    if (self) {
        self->updateStatus();
    }
}

void SettingsScreen::stopStatusTimer()
{
    if (m_statusTimer) {
        lv_timer_del(m_statusTimer);
        m_statusTimer = nullptr;
    }
}

// --- Events ----------------------------------------------------------------

void SettingsScreen::onTextAreaFocused(lv_event_t* e)
{
    auto* self = static_cast<SettingsScreen*>(lv_event_get_user_data(e));
    if (self) {
        self->showKeyboard(static_cast<lv_obj_t*>(lv_event_get_target(e)));
    }
}

void SettingsScreen::onTextAreaDefocused(lv_event_t* e)
{
    auto* self = static_cast<SettingsScreen*>(lv_event_get_user_data(e));
    if (self) {
        self->hideKeyboard();
        self->saveSpeedSteps();
    }
}

void SettingsScreen::onTransportChanged(lv_event_t* e)
{
    auto* self = static_cast<SettingsScreen*>(lv_event_get_user_data(e));
    if (!self || !self->m_transportDropdown) {
        return;
    }

    const uint16_t selected = lv_dropdown_get_selected(self->m_transportDropdown);
    const ThrottleTransport chosen = (selected == 1) ? ThrottleTransport::ORCHESTRATOR
                                                     : ThrottleTransport::WITHROTTLE;

    // Read-modify-write so the orchestrator's own settings survive.
    TransportSettings settings = TransportSettings::load();

    if (chosen == ThrottleTransport::ORCHESTRATOR && !settings.isOrchestratorConfigured()) {
        // Refuse rather than save a selection that would boot into a dead
        // transport with every knob disabled and nothing explaining why.
        lv_dropdown_set_selected(self->m_transportDropdown, 0);
        if (self->m_transportNoteLabel) {
            lv_label_set_text(self->m_transportNoteLabel,
                              "Set the orchestrator host and operator credential first "
                              "(Orchestrator...).");
            lv_obj_set_style_text_color(self->m_transportNoteLabel,
                                        UiTheme::colour(UiTheme::TEXT_ERROR), 0);
        }
        return;
    }

    settings.transport = chosen;
    settings.save();

    if (self->m_transportNoteLabel) {
        // The backend is chosen once, during initialise(), because swapping it
        // under a live ThrottleController would strand locos mid-command.
        lv_label_set_text(self->m_transportNoteLabel,
                          "Saved. Restart the device for the transport change to take effect.");
        lv_obj_set_style_text_color(self->m_transportNoteLabel,
                                    UiTheme::colour(UiTheme::TEXT_WARNING), 0);
    }
}

void SettingsScreen::leaveFor(void (*navigate)())
{
    hideKeyboard();
    saveSpeedSteps();
    stopStatusTimer();

    navigate();

    if (m_screen) {
        lv_obj_del_async(m_screen);
        clearUiPointers();
    }
}

void SettingsScreen::onJmriSettingsClicked(lv_event_t* e)
{
    auto* self = static_cast<SettingsScreen*>(lv_event_get_user_data(e));
    if (self) {
        self->leaveFor(show_jmri_config_screen);
    }
}

void SettingsScreen::onOrchestratorSettingsClicked(lv_event_t* e)
{
    auto* self = static_cast<SettingsScreen*>(lv_event_get_user_data(e));
    if (self) {
        self->leaveFor(show_orchestrator_config_screen);
    }
}

void SettingsScreen::onBackClicked(lv_event_t* e)
{
    auto* self = static_cast<SettingsScreen*>(lv_event_get_user_data(e));
    if (self) {
        self->leaveFor(show_main_screen);
    }
}
