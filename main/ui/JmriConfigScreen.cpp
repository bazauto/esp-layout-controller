#include "JmriConfigScreen.h"
#include "wrappers/main_screen_wrapper.h"
#include "esp_log.h"
#include "../controller/WiFiController.h"
#include "../hardware/RotaryEncoderHal.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "lvgl_port.h"
#include <cstring>

extern "C" {
    bool lvgl_port_lock(int timeout_ms);
    void lvgl_port_unlock(void);
}

static const char* TAG = "JmriConfigScreen";

// NVS keys for JMRI settings
static const char* NVS_NAMESPACE = "jmri";
static const char* NVS_KEY_SERVER_IP = "server_ip";
static const char* NVS_KEY_WITHROTTLE_PORT = "wt_port";
static const char* NVS_KEY_POWER_MANAGER = "power_mgr";
static const char* NVS_KEY_SPEED_STEPS = "speed_steps";

JmriConfigScreen::JmriConfigScreen(JmriJsonClient& jsonClient,
                                   WiThrottleClient& wiThrottleClient,
                                   WiFiController* wifiController,
                                   RotaryEncoderHal* encoderHal)
    : m_screen(nullptr)
    , m_serverIpInput(nullptr)
    , m_wiThrottlePortInput(nullptr)
    , m_powerManagerInput(nullptr)
        , m_speedStepsInput(nullptr)
    , m_statusWifiValue(nullptr)
    , m_statusWiThrottleValue(nullptr)
    , m_statusJsonValue(nullptr)
    , m_statusEncoder1Value(nullptr)
    , m_statusEncoder2Value(nullptr)
    , m_statusSoftwareValue(nullptr)
    , m_statusHardwareValue(nullptr)
    , m_connectButton(nullptr)
    , m_disconnectButton(nullptr)
    , m_backButton(nullptr)
    , m_keyboard(nullptr)
    , m_keyboardLabel(nullptr)
    , m_jsonClient(jsonClient)
    , m_wiThrottleClient(wiThrottleClient)
    , m_wifiController(wifiController)
    , m_encoderHal(encoderHal)
{
}

JmriConfigScreen::~JmriConfigScreen()
{
    // Don't delete LVGL objects - LVGL manages screen lifecycle
}

lv_obj_t* JmriConfigScreen::create()
{
    // Create screen
    m_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(m_screen, lv_color_hex(0x000000), 0);
    
    // Button container height
    const int buttonAreaHeight = BUTTON_HEIGHT + 2 * PADDING;
    
    // Create scrollable content container
    lv_obj_t* scrollContainer = lv_obj_create(m_screen);
    lv_obj_set_size(scrollContainer, SCREEN_WIDTH, SCREEN_HEIGHT - buttonAreaHeight);
    lv_obj_align(scrollContainer, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(scrollContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scrollContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(scrollContainer, 6, 0);
    lv_obj_set_style_pad_row(scrollContainer, 6, 0);
    lv_obj_clear_flag(scrollContainer, LV_OBJ_FLAG_SCROLLABLE);
    
    // Create fixed button container
    lv_obj_t* buttonContainer = lv_obj_create(m_screen);
    lv_obj_set_size(buttonContainer, SCREEN_WIDTH, buttonAreaHeight);
    lv_obj_align(buttonContainer, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(buttonContainer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(buttonContainer, 0, 0);
    
    // Create sections
    createStatusSection(scrollContainer);
    createConfigSection(scrollContainer);
    createSystemStatusSection(scrollContainer);
    createButtonSection(buttonContainer);
    createKeyboard();

    m_jsonClient.setConnectionStateCallback([this](JmriJsonClient::ConnectionState) {
        if (lvgl_port_lock(100)) {
            updateStatus();
            lvgl_port_unlock();
        }
    });

    m_wiThrottleClient.setConnectionStateCallback([this](WiThrottleClient::ConnectionState) {
        if (lvgl_port_lock(100)) {
            updateStatus();
            lvgl_port_unlock();
        }
    });
    
    // Load saved settings
    loadSettings();
    
    // Update status
    updateStatus();
    
    // Load the screen
    lv_scr_load(m_screen);
    
    return m_screen;
}

void JmriConfigScreen::createStatusSection(lv_obj_t* parent)
{
    // Title
    lv_obj_t* titleLabel = lv_label_create(parent);
    lv_label_set_text(titleLabel, "JMRI Server Configuration");
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_20, 0);
}

void JmriConfigScreen::createSystemStatusSection(lv_obj_t* parent)
{
    lv_obj_t* header = lv_label_create(parent);
    lv_label_set_text(header, "System Status");
    lv_obj_set_style_text_font(header, &lv_font_montserrat_20, 0);

    lv_obj_t* statusContainer = lv_obj_create(parent);
    lv_obj_remove_style_all(statusContainer);
    lv_obj_set_width(statusContainer, LV_PCT(100));
    lv_obj_set_height(statusContainer, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(statusContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(statusContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(statusContainer, 0, 0);
    lv_obj_set_style_pad_row(statusContainer, 2, 0);

    addStatusRow(statusContainer, "Software", &m_statusSoftwareValue);
    addStatusRow(statusContainer, "Hardware", &m_statusHardwareValue);
    addStatusRow(statusContainer, "WiFi", &m_statusWifiValue);
    addStatusRow(statusContainer, "WiThrottle", &m_statusWiThrottleValue);
    addStatusRow(statusContainer, "JMRI JSON", &m_statusJsonValue);
    addStatusRow(statusContainer, "Encoder 1", &m_statusEncoder1Value);
    addStatusRow(statusContainer, "Encoder 2", &m_statusEncoder2Value);
}

void JmriConfigScreen::addStatusRow(lv_obj_t* parent, const char* label, lv_obj_t** valueLabel)
{
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* labelObj = lv_label_create(row);
    lv_label_set_text(labelObj, label);

    *valueLabel = lv_label_create(row);
    lv_label_set_text(*valueLabel, "-");
}

void JmriConfigScreen::createConfigSection(lv_obj_t* parent)
{
    // Create a two-column container
    lv_obj_t* configContainer = lv_obj_create(parent);
    lv_obj_remove_style_all(configContainer);
    lv_obj_set_size(configContainer, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(configContainer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(configContainer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_bottom(configContainer, 50, 0);
    lv_obj_set_style_pad_column(configContainer, 20, 0);
    
    // Left column (Server settings)
    lv_obj_t* leftColumn = lv_obj_create(configContainer);
    lv_obj_remove_style_all(leftColumn);
    lv_obj_set_size(leftColumn, LV_PCT(48), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(leftColumn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(leftColumn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(leftColumn, 5, 0);
    
    // Server IP label
    lv_obj_t* ipLabel = lv_label_create(leftColumn);
    lv_label_set_text(ipLabel, "Server IP Address:");
    lv_obj_set_width(ipLabel, LV_PCT(100));
    
    // Server IP input
    m_serverIpInput = lv_textarea_create(leftColumn);
    lv_textarea_set_one_line(m_serverIpInput, true);
    lv_textarea_set_placeholder_text(m_serverIpInput, "192.168.1.100");
    lv_obj_set_width(m_serverIpInput, LV_PCT(100));
    lv_obj_add_event_cb(m_serverIpInput, onTextAreaFocused, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(m_serverIpInput, onTextAreaDefocused, LV_EVENT_DEFOCUSED, this);
    
    // WiThrottle Port label
    lv_obj_t* wtPortLabel = lv_label_create(leftColumn);
    lv_label_set_text(wtPortLabel, "WiThrottle Port:");
    lv_obj_set_width(wtPortLabel, LV_PCT(100));
    
    // WiThrottle Port input
    m_wiThrottlePortInput = lv_textarea_create(leftColumn);
    lv_textarea_set_one_line(m_wiThrottlePortInput, true);
    lv_textarea_set_placeholder_text(m_wiThrottlePortInput, "12090");
    lv_textarea_set_text(m_wiThrottlePortInput, "12090");
    lv_obj_set_width(m_wiThrottlePortInput, LV_PCT(100));
    lv_textarea_set_accepted_chars(m_wiThrottlePortInput, "0123456789");
    lv_textarea_set_max_length(m_wiThrottlePortInput, 5);
    lv_obj_add_event_cb(m_wiThrottlePortInput, onTextAreaFocused, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(m_wiThrottlePortInput, onTextAreaDefocused, LV_EVENT_DEFOCUSED, this);
    
    // Right column (Power settings)
    lv_obj_t* rightColumn = lv_obj_create(configContainer);
    lv_obj_remove_style_all(rightColumn);
    lv_obj_set_size(rightColumn, LV_PCT(48), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(rightColumn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rightColumn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(rightColumn, 5, 0);
    
    // Power Manager Name label
    lv_obj_t* powerLabel = lv_label_create(rightColumn);
    lv_label_set_text(powerLabel, "Power Manager Name:");
    lv_obj_set_width(powerLabel, LV_PCT(100));
    
    // Power Manager Name input
    m_powerManagerInput = lv_textarea_create(rightColumn);
    lv_textarea_set_one_line(m_powerManagerInput, true);
    lv_textarea_set_placeholder_text(m_powerManagerInput, "DCC++");
    lv_textarea_set_text(m_powerManagerInput, "DCC++");
    lv_obj_set_width(m_powerManagerInput, LV_PCT(100));
    lv_obj_add_event_cb(m_powerManagerInput, onTextAreaFocused, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(m_powerManagerInput, onTextAreaDefocused, LV_EVENT_DEFOCUSED, this);
    
    // Speed Steps label
    lv_obj_t* speedStepsLabel = lv_label_create(rightColumn);
    lv_label_set_text(speedStepsLabel, "Speed Steps per Click:");
    lv_obj_set_width(speedStepsLabel, LV_PCT(100));
    
    // Speed Steps input
    m_speedStepsInput = lv_textarea_create(rightColumn);
    lv_textarea_set_one_line(m_speedStepsInput, true);
    lv_textarea_set_placeholder_text(m_speedStepsInput, "4");
    lv_textarea_set_text(m_speedStepsInput, "4");
    lv_obj_set_width(m_speedStepsInput, LV_PCT(100));
    lv_textarea_set_accepted_chars(m_speedStepsInput, "0123456789");
    lv_textarea_set_max_length(m_speedStepsInput, 2);
    lv_obj_add_event_cb(m_speedStepsInput, onTextAreaFocused, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(m_speedStepsInput, onTextAreaDefocused, LV_EVENT_DEFOCUSED, this);

    // Notes removed to make space for status summary row
}

void JmriConfigScreen::createButtonSection(lv_obj_t* parent)
{
    // Button row
    lv_obj_t* buttonContainer = lv_obj_create(parent);
    lv_obj_remove_style_all(buttonContainer);
    lv_obj_set_size(buttonContainer, LV_PCT(100), BUTTON_HEIGHT);
    lv_obj_set_flex_flow(buttonContainer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttonContainer, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    const int buttonWidth = 200;
    
    // Connect button (green)
    m_connectButton = lv_btn_create(buttonContainer);
    lv_obj_set_size(m_connectButton, buttonWidth, BUTTON_HEIGHT);
    lv_obj_t* connectLabel = lv_label_create(m_connectButton);
    lv_label_set_text(connectLabel, "Connect");
    lv_obj_center(connectLabel);
    lv_obj_add_event_cb(m_connectButton, onConnectButtonClicked, LV_EVENT_CLICKED, this);
    lv_obj_set_style_bg_color(m_connectButton, lv_color_hex(0x00AA00), 0);
    
    // Disconnect button (red)
    m_disconnectButton = lv_btn_create(buttonContainer);
    lv_obj_set_size(m_disconnectButton, buttonWidth, BUTTON_HEIGHT);
    lv_obj_t* disconnectLabel = lv_label_create(m_disconnectButton);
    lv_label_set_text(disconnectLabel, "Disconnect");
    lv_obj_center(disconnectLabel);
    lv_obj_add_event_cb(m_disconnectButton, onDisconnectButtonClicked, LV_EVENT_CLICKED, this);
    lv_obj_set_style_bg_color(m_disconnectButton, lv_color_hex(0xAA0000), 0);
    
    // Back button
    m_backButton = lv_btn_create(buttonContainer);
    lv_obj_set_size(m_backButton, buttonWidth, BUTTON_HEIGHT);
    lv_obj_t* backLabel = lv_label_create(m_backButton);
    lv_label_set_text(backLabel, "Back");
    lv_obj_center(backLabel);
    lv_obj_add_event_cb(m_backButton, onBackButtonClicked, LV_EVENT_CLICKED, this);

}

void JmriConfigScreen::createKeyboard()
{
    // Create keyboard
    m_keyboard = lv_keyboard_create(m_screen);
    lv_obj_set_size(m_keyboard, SCREEN_WIDTH, SCREEN_HEIGHT / 2);
    lv_obj_align(m_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    
    // Create label above keyboard
    m_keyboardLabel = lv_label_create(m_screen);
    lv_label_set_text(m_keyboardLabel, "");
    lv_obj_set_style_text_font(m_keyboardLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(m_keyboardLabel, lv_color_white(), 0);
    lv_obj_set_style_bg_color(m_keyboardLabel, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(m_keyboardLabel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(m_keyboardLabel, 10, 0);
    lv_obj_set_style_pad_ver(m_keyboardLabel, 8, 0);
    lv_obj_set_style_radius(m_keyboardLabel, 5, 0);
    lv_obj_align(m_keyboardLabel, LV_ALIGN_BOTTOM_MID, 0, -(SCREEN_HEIGHT / 2) - 35);
    
    // Hide initially
    lv_obj_add_flag(m_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(m_keyboardLabel, LV_OBJ_FLAG_HIDDEN);
}

void JmriConfigScreen::showKeyboard(lv_obj_t* textarea)
{
    if (m_keyboard && textarea) {
        lv_keyboard_set_textarea(m_keyboard, textarea);
        
        // Update label
        if (textarea == m_serverIpInput) {
            lv_label_set_text(m_keyboardLabel, "Editing: Server IP Address");
        } else if (textarea == m_wiThrottlePortInput) {
            lv_label_set_text(m_keyboardLabel, "Editing: WiThrottle Port");
        } else if (textarea == m_powerManagerInput) {
            lv_label_set_text(m_keyboardLabel, "Editing: Power Manager Name");
        }
        
        lv_obj_clear_flag(m_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(m_keyboardLabel, LV_OBJ_FLAG_HIDDEN);
    }
}

void JmriConfigScreen::hideKeyboard()
{
    if (m_keyboard) {
        lv_obj_add_flag(m_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(m_keyboardLabel, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(m_keyboard, nullptr);
    }
}

void JmriConfigScreen::updateStatus()
{
    auto jsonState = m_jsonClient.getState();
    switch (jsonState) {
        case JmriJsonClient::ConnectionState::CONNECTED:
            lv_obj_clear_state(m_disconnectButton, LV_STATE_DISABLED);
            lv_obj_add_state(m_connectButton, LV_STATE_DISABLED);
            break;
        case JmriJsonClient::ConnectionState::CONNECTING:
            lv_obj_add_state(m_disconnectButton, LV_STATE_DISABLED);
            lv_obj_add_state(m_connectButton, LV_STATE_DISABLED);
            break;
        case JmriJsonClient::ConnectionState::FAILED:
            lv_obj_add_state(m_disconnectButton, LV_STATE_DISABLED);
            lv_obj_clear_state(m_connectButton, LV_STATE_DISABLED);
            break;
        default:
            lv_obj_add_state(m_disconnectButton, LV_STATE_DISABLED);
            lv_obj_clear_state(m_connectButton, LV_STATE_DISABLED);
            break;
    }

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
                          (m_wifiController && m_wifiController->isConnected()) ? "Connected" : "Disconnected");
    }

    if (m_statusWiThrottleValue) {
        lv_label_set_text(m_statusWiThrottleValue, m_wiThrottleClient.isConnected() ? "Connected" : "Disconnected");
    }

    if (m_statusJsonValue) {
        lv_label_set_text(m_statusJsonValue, m_jsonClient.isConnected() ? "Connected" : "Disconnected");
    }

    if (m_statusEncoder1Value) {
        if (m_encoderHal) {
            auto status = m_encoderHal->getStatus(0);
            char text[24];
            snprintf(text, sizeof(text), "0x%02X %s", status.address, status.present ? "present" : "missing");
            lv_label_set_text(m_statusEncoder1Value, text);
        } else {
            lv_label_set_text(m_statusEncoder1Value, "Unavailable");
        }
    }

    if (m_statusEncoder2Value) {
        if (m_encoderHal) {
            auto status = m_encoderHal->getStatus(1);
            char text[24];
            snprintf(text, sizeof(text), "0x%02X %s", status.address, status.present ? "present" : "missing");
            lv_label_set_text(m_statusEncoder2Value, text);
        } else {
            lv_label_set_text(m_statusEncoder2Value, "Unavailable");
        }
    }
}

void JmriConfigScreen::connectToJmri()
{
    std::string serverIp = getServerIpText();
    std::string wtPortStr = getWiThrottlePortText();
    std::string powerMgr = getPowerManagerText();
    
    if (serverIp.empty()) {
        ESP_LOGW(TAG, "Server IP is empty");
        return;
    }
    
    // Parse WiThrottle port
    uint16_t wtPort = wtPortStr.empty() ? 12090 : std::atoi(wtPortStr.c_str());
    
    // Set power manager name (use default if empty)
    if (powerMgr.empty()) {
        powerMgr = "DCC++";
    }
    m_jsonClient.setConfiguredPowerName(powerMgr);
    
    ESP_LOGI(TAG, "Connecting to JMRI server: %s (WiThrottle:%d, Power:%s)", 
             serverIp.c_str(), wtPort, powerMgr.c_str());
    
    // Save settings
    saveSettings();
    
    // Connect WiThrottle first - it will auto-discover JSON port
    esp_err_t err = m_wiThrottleClient.connect(serverIp, wtPort);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect WiThrottle client");
    }
    
    // Set callback to connect JSON when port is discovered
    m_wiThrottleClient.setWebPortCallback([this, serverIp](uint16_t jsonPort) {
        ESP_LOGI(TAG, "Auto-connecting JSON client to port %d", jsonPort);
        esp_err_t err = m_jsonClient.connect(serverIp, jsonPort);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to connect JSON client");
        }
    });
    
    // Update status
    updateStatus();
}

void JmriConfigScreen::disconnectFromJmri()
{
    ESP_LOGI(TAG, "Disconnecting from JMRI server");
    
    m_jsonClient.disconnect();
    m_wiThrottleClient.disconnect();
    
    updateStatus();
}

void JmriConfigScreen::saveSettings()
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return;
    }
    
    std::string serverIp = getServerIpText();
    std::string wtPort = getWiThrottlePortText();
    std::string powerMgr = getPowerManagerText();
    const char* speedStepsText = lv_textarea_get_text(m_speedStepsInput);
    int speedSteps = speedStepsText ? atoi(speedStepsText) : 4;
    if (speedSteps < 1) speedSteps = 1;
    if (speedSteps > 20) speedSteps = 20;
    
    nvs_set_str(handle, NVS_KEY_SERVER_IP, serverIp.c_str());
    nvs_set_str(handle, NVS_KEY_WITHROTTLE_PORT, wtPort.c_str());
    nvs_set_str(handle, NVS_KEY_POWER_MANAGER, powerMgr.c_str());
    nvs_set_i32(handle, NVS_KEY_SPEED_STEPS, speedSteps);
    
    nvs_commit(handle);
    nvs_close(handle);
    
    ESP_LOGI(TAG, "JMRI settings saved (Power Manager: %s, Speed Steps: %d)", powerMgr.c_str(), speedSteps);
}

void JmriConfigScreen::loadSettings()
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "No saved JMRI settings");
        return;
    }
    
    char buffer[64];
    size_t length;
    
    // Load server IP
    length = sizeof(buffer);
    if (nvs_get_str(handle, NVS_KEY_SERVER_IP, buffer, &length) == ESP_OK) {
        lv_textarea_set_text(m_serverIpInput, buffer);
    }
    
    // Load WiThrottle port
    length = sizeof(buffer);
    if (nvs_get_str(handle, NVS_KEY_WITHROTTLE_PORT, buffer, &length) == ESP_OK) {
        lv_textarea_set_text(m_wiThrottlePortInput, buffer);
    }
    
    // Load Power Manager name
    length = sizeof(buffer);
    if (nvs_get_str(handle, NVS_KEY_POWER_MANAGER, buffer, &length) == ESP_OK) {
        lv_textarea_set_text(m_powerManagerInput, buffer);
        // Update the JMRI client with the configured power manager name
        m_jsonClient.setConfiguredPowerName(buffer);
        ESP_LOGI(TAG, "Power Manager configured: %s", buffer);
    }
    
    // Load Speed Steps
    int32_t speedSteps = 4; // default
    if (nvs_get_i32(handle, NVS_KEY_SPEED_STEPS, &speedSteps) == ESP_OK) {
        char speedStepsStr[8];
        snprintf(speedStepsStr, sizeof(speedStepsStr), "%d", (int)speedSteps);
        lv_textarea_set_text(m_speedStepsInput, speedStepsStr);
        ESP_LOGI(TAG, "Speed Steps configured: %d", (int)speedSteps);
    }
    
    nvs_close(handle);
    ESP_LOGI(TAG, "JMRI settings loaded");
}

std::string JmriConfigScreen::getServerIpText() const
{
    const char* text = lv_textarea_get_text(m_serverIpInput);
    return text ? std::string(text) : std::string();
}

std::string JmriConfigScreen::getWiThrottlePortText() const
{
    const char* text = lv_textarea_get_text(m_wiThrottlePortInput);
    return text ? std::string(text) : std::string();
}

std::string JmriConfigScreen::getPowerManagerText() const
{
    const char* text = lv_textarea_get_text(m_powerManagerInput);
    return text ? std::string(text) : std::string();
}

// Event handlers
void JmriConfigScreen::onConnectButtonClicked(lv_event_t* e)
{
    JmriConfigScreen* screen = static_cast<JmriConfigScreen*>(lv_event_get_user_data(e));
    screen->connectToJmri();
}

void JmriConfigScreen::onDisconnectButtonClicked(lv_event_t* e)
{
    JmriConfigScreen* screen = static_cast<JmriConfigScreen*>(lv_event_get_user_data(e));
    screen->disconnectFromJmri();
}

void JmriConfigScreen::onBackButtonClicked(lv_event_t* e)
{
    ESP_LOGI(TAG, "Back button clicked");
    
    // Get the screen instance
    JmriConfigScreen* screen = static_cast<JmriConfigScreen*>(lv_event_get_user_data(e));
    
    // Hide keyboard if visible
    screen->hideKeyboard();
    
    // Load main screen (this will make the JMRI config screen inactive)
    show_main_screen();
    
    // Schedule deletion of this JMRI config screen after a short delay
    // This allows LVGL to finish any pending operations
    if (screen->m_screen) {
        lv_obj_del_async(screen->m_screen);
        screen->m_screen = nullptr;
    }
}

void JmriConfigScreen::onTextAreaFocused(lv_event_t* e)
{
    JmriConfigScreen* screen = static_cast<JmriConfigScreen*>(lv_event_get_user_data(e));
    lv_obj_t* textarea = lv_event_get_target(e);
    screen->showKeyboard(textarea);
}

void JmriConfigScreen::onTextAreaDefocused(lv_event_t* e)
{
    JmriConfigScreen* screen = static_cast<JmriConfigScreen*>(lv_event_get_user_data(e));
    screen->hideKeyboard();
}
