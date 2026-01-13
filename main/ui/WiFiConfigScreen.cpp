#include "WiFiConfigScreen.h"
#include "wifi_config_wrapper.h"
#include "esp_log.h"
#include <algorithm>

static const char* TAG = "WiFiConfigScreen";

// Screen dimensions - adjust for your 7" display
static const int SCREEN_WIDTH = 800;
static const int SCREEN_HEIGHT = 480;
static const int PADDING = 10;
static const int BUTTON_HEIGHT = 50;

WiFiConfigScreen::WiFiConfigScreen(WiFiManager& wifiManager)
    : m_screen(nullptr)
    , m_statusLabel(nullptr)
    , m_ssidLabel(nullptr)
    , m_ipLabel(nullptr)
    , m_networkList(nullptr)
    , m_ssidInput(nullptr)
    , m_passwordInput(nullptr)
    , m_scanButton(nullptr)
    , m_connectButton(nullptr)
    , m_disconnectButton(nullptr)
    , m_forgetButton(nullptr)
    , m_backButton(nullptr)
    , m_keyboard(nullptr)
    , m_keyboardLabel(nullptr)
    , m_wifiManager(wifiManager)
{
}

WiFiConfigScreen::~WiFiConfigScreen()
{
    close();
}

lv_obj_t* WiFiConfigScreen::create()
{
    // Create screen
    m_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(m_screen, lv_color_hex(0x000000), 0);
    
    // Button container height with padding
    const int buttonAreaHeight = BUTTON_HEIGHT + 3 * PADDING;
    
    // Create scrollable content container (everything except buttons) - full width
    lv_obj_t* scrollContainer = lv_obj_create(m_screen);
    lv_obj_set_size(scrollContainer, SCREEN_WIDTH, SCREEN_HEIGHT - buttonAreaHeight);
    lv_obj_align(scrollContainer, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(scrollContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scrollContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(scrollContainer, PADDING, 0);
    lv_obj_set_style_pad_row(scrollContainer, PADDING, 0);
    
    // Create fixed button container at bottom (non-scrollable)
    lv_obj_t* buttonContainer = lv_obj_create(m_screen);
    lv_obj_set_size(buttonContainer, SCREEN_WIDTH, buttonAreaHeight);
    lv_obj_align(buttonContainer, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(buttonContainer, LV_OBJ_FLAG_SCROLLABLE);  // Make it non-scrollable
    lv_obj_set_style_pad_all(buttonContainer, 0, 0);  // Remove padding from container itself
    
    // Create sections
    createStatusSection(scrollContainer);
    createNetworkListSection(scrollContainer);
    createInputSection(scrollContainer);
    createButtonSection(buttonContainer);
    
    // Create keyboard (initially hidden)
    createKeyboard();
    
    // Set WiFi state change callback
    m_wifiManager.setStateCallback([this](WiFiManager::State state, const std::string& ip) {
        onWiFiStateChanged(this, state);
    });
    
    // Initial status update
    updateStatus();
    
    // Load screen
    lv_scr_load(m_screen);
    
    ESP_LOGI(TAG, "WiFi configuration screen created");
    
    return m_screen;
}

void WiFiConfigScreen::createStatusSection(lv_obj_t* parent)
{
    // Status container
    lv_obj_t* statusContainer = lv_obj_create(parent);
    lv_obj_set_size(statusContainer, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(statusContainer, PADDING, 0);
    
    // Title label
    lv_obj_t* titleLabel = lv_label_create(statusContainer);
    lv_label_set_text(titleLabel, "WiFi Configuration");
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_24, 0);
    lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 0, 0);
    
    // Status label
    m_statusLabel = lv_label_create(statusContainer);
    lv_label_set_text(m_statusLabel, "Status: Disconnected");
    lv_obj_align(m_statusLabel, LV_ALIGN_TOP_LEFT, 0, 40);
    
    // SSID label
    m_ssidLabel = lv_label_create(statusContainer);
    lv_label_set_text(m_ssidLabel, "Network: None");
    lv_obj_align(m_ssidLabel, LV_ALIGN_TOP_LEFT, 0, 65);
    
    // IP address label
    m_ipLabel = lv_label_create(statusContainer);
    lv_label_set_text(m_ipLabel, "IP: Not connected");
    lv_obj_align(m_ipLabel, LV_ALIGN_TOP_LEFT, 0, 90);
}

void WiFiConfigScreen::createNetworkListSection(lv_obj_t* parent)
{
    // Network list label
    lv_obj_t* listLabel = lv_label_create(parent);
    lv_label_set_text(listLabel, "Available Networks:");
    lv_obj_set_style_text_font(listLabel, &lv_font_montserrat_16, 0);
    
    // Network list (dropdown style)
    m_networkList = lv_dropdown_create(parent);
    lv_obj_set_width(m_networkList, LV_PCT(100));
    lv_dropdown_set_options(m_networkList, "Scan for networks...");
    lv_obj_add_event_cb(m_networkList, onNetworkSelected, LV_EVENT_VALUE_CHANGED, this);
}

void WiFiConfigScreen::createInputSection(lv_obj_t* parent)
{
    // SSID input
    lv_obj_t* ssidLabel = lv_label_create(parent);
    lv_label_set_text(ssidLabel, "SSID:");
    
    m_ssidInput = lv_textarea_create(parent);
    lv_obj_set_width(m_ssidInput, LV_PCT(100));
    lv_textarea_set_one_line(m_ssidInput, true);
    lv_textarea_set_placeholder_text(m_ssidInput, "Enter network name...");
    lv_obj_add_event_cb(m_ssidInput, onTextAreaFocused, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(m_ssidInput, onTextAreaDefocused, LV_EVENT_DEFOCUSED, this);
    
    // Password input
    lv_obj_t* passwordLabel = lv_label_create(parent);
    lv_label_set_text(passwordLabel, "Password:");
    
    m_passwordInput = lv_textarea_create(parent);
    lv_obj_set_width(m_passwordInput, LV_PCT(100));
    lv_textarea_set_one_line(m_passwordInput, true);
    lv_textarea_set_password_mode(m_passwordInput, true);
    lv_textarea_set_placeholder_text(m_passwordInput, "Enter password...");
    lv_obj_add_event_cb(m_passwordInput, onTextAreaFocused, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(m_passwordInput, onTextAreaDefocused, LV_EVENT_DEFOCUSED, this);
}

void WiFiConfigScreen::createButtonSection(lv_obj_t* parent)
{
    // Button container with horizontal layout
    lv_obj_t* buttonContainer = lv_obj_create(parent);
    lv_obj_remove_style_all(buttonContainer);
    lv_obj_set_size(buttonContainer, LV_PCT(100), BUTTON_HEIGHT);
    lv_obj_center(buttonContainer);
    lv_obj_set_flex_flow(buttonContainer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttonContainer, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // Button width for 5 buttons with spacing
    const int buttonWidth = 140;
    
    // Scan button
    m_scanButton = lv_btn_create(buttonContainer);
    lv_obj_set_size(m_scanButton, buttonWidth, BUTTON_HEIGHT);
    lv_obj_t* scanLabel = lv_label_create(m_scanButton);
    lv_label_set_text(scanLabel, "Scan");
    lv_obj_center(scanLabel);
    lv_obj_add_event_cb(m_scanButton, onScanButtonClicked, LV_EVENT_CLICKED, this);
    
    // Connect button (green)
    m_connectButton = lv_btn_create(buttonContainer);
    lv_obj_set_size(m_connectButton, buttonWidth, BUTTON_HEIGHT);
    lv_obj_t* connectLabel = lv_label_create(m_connectButton);
    lv_label_set_text(connectLabel, "Connect");
    lv_obj_center(connectLabel);
    lv_obj_add_event_cb(m_connectButton, onConnectButtonClicked, LV_EVENT_CLICKED, this);
    lv_obj_set_style_bg_color(m_connectButton, lv_color_hex(0x00AA00), 0);
    
    // Disconnect button (orange - just disconnects, keeps credentials)
    m_disconnectButton = lv_btn_create(buttonContainer);
    lv_obj_set_size(m_disconnectButton, buttonWidth, BUTTON_HEIGHT);
    lv_obj_t* disconnectLabel = lv_label_create(m_disconnectButton);
    lv_label_set_text(disconnectLabel, "Disconnect");
    lv_obj_center(disconnectLabel);
    lv_obj_add_event_cb(m_disconnectButton, onDisconnectButtonClicked, LV_EVENT_CLICKED, this);
    lv_obj_set_style_bg_color(m_disconnectButton, lv_color_hex(0xFF8800), 0);
    
    // Forget button (red - disconnects and clears credentials)
    m_forgetButton = lv_btn_create(buttonContainer);
    lv_obj_set_size(m_forgetButton, buttonWidth, BUTTON_HEIGHT);
    lv_obj_t* forgetLabel = lv_label_create(m_forgetButton);
    lv_label_set_text(forgetLabel, "Forget");
    lv_obj_center(forgetLabel);
    lv_obj_add_event_cb(m_forgetButton, onForgetButtonClicked, LV_EVENT_CLICKED, this);
    lv_obj_set_style_bg_color(m_forgetButton, lv_color_hex(0xAA0000), 0);
    
    // Back button
    m_backButton = lv_btn_create(buttonContainer);
    lv_obj_set_size(m_backButton, buttonWidth, BUTTON_HEIGHT);
    lv_obj_t* backLabel = lv_label_create(m_backButton);
    lv_label_set_text(backLabel, "Back");
    lv_obj_center(backLabel);
    lv_obj_add_event_cb(m_backButton, onBackButtonClicked, LV_EVENT_CLICKED, this);
}

void WiFiConfigScreen::updateStatus()
{
    WiFiManager::State state = m_wifiManager.getState();
    std::string ssid = m_wifiManager.getStoredSsid();
    
    // Update SSID display
    if (ssid.empty()) {
        lv_label_set_text(m_ssidLabel, "Network: None");
    } else {
        std::string ssidStr = "Network: " + ssid;
        lv_label_set_text(m_ssidLabel, ssidStr.c_str());
    }
    
    switch (state) {
        case WiFiManager::State::DISCONNECTED:
            lv_label_set_text(m_statusLabel, "Status: Disconnected");
            lv_label_set_text(m_ipLabel, "IP: Not connected");
            lv_obj_clear_state(m_connectButton, LV_STATE_DISABLED);  // Enable connect button
            lv_obj_add_state(m_disconnectButton, LV_STATE_DISABLED);  // Disable disconnect when not connected
            lv_obj_add_state(m_forgetButton, LV_STATE_DISABLED);     // Disable forget when not connected
            break;
            
        case WiFiManager::State::CONNECTING:
            lv_label_set_text(m_statusLabel, "Status: Connecting...");
            lv_label_set_text(m_ipLabel, "IP: Waiting...");
            lv_obj_add_state(m_connectButton, LV_STATE_DISABLED);  // Disable while connecting
            lv_obj_add_state(m_disconnectButton, LV_STATE_DISABLED);  // Disable while connecting
            lv_obj_add_state(m_forgetButton, LV_STATE_DISABLED);     // Disable while connecting
            break;
            
        case WiFiManager::State::CONNECTED: {
            lv_label_set_text(m_statusLabel, "Status: Connected");
            std::string ipStr = "IP: " + m_wifiManager.getIpAddress();
            lv_label_set_text(m_ipLabel, ipStr.c_str());
            lv_obj_add_state(m_connectButton, LV_STATE_DISABLED);  // Disable connect when connected
            lv_obj_clear_state(m_disconnectButton, LV_STATE_DISABLED);  // Enable disconnect button
            lv_obj_clear_state(m_forgetButton, LV_STATE_DISABLED);     // Enable forget button
            break;
        }
            
        case WiFiManager::State::FAILED:
            lv_label_set_text(m_statusLabel, "Status: Connection Failed");
            lv_label_set_text(m_ipLabel, "IP: Not connected");
            lv_obj_clear_state(m_connectButton, LV_STATE_DISABLED);  // Enable connect to retry
            lv_obj_add_state(m_disconnectButton, LV_STATE_DISABLED);  // Disable disconnect when failed
            lv_obj_add_state(m_forgetButton, LV_STATE_DISABLED);     // Disable forget when failed
            break;
    }
}

void WiFiConfigScreen::close()
{
    if (m_screen) {
        // Only delete if it's not the active screen
        if (lv_scr_act() != m_screen) {
            lv_obj_del(m_screen);
        }
        m_screen = nullptr;
    }
}

void WiFiConfigScreen::onScanButtonClicked(lv_event_t* e)
{
    WiFiConfigScreen* screen = static_cast<WiFiConfigScreen*>(lv_event_get_user_data(e));
    screen->performScan();
}

void WiFiConfigScreen::onConnectButtonClicked(lv_event_t* e)
{
    WiFiConfigScreen* screen = static_cast<WiFiConfigScreen*>(lv_event_get_user_data(e));
    screen->connectToWiFi();
}

void WiFiConfigScreen::onDisconnectButtonClicked(lv_event_t* e)
{
    WiFiConfigScreen* screen = static_cast<WiFiConfigScreen*>(lv_event_get_user_data(e));
    screen->disconnectWiFi();
}

void WiFiConfigScreen::onForgetButtonClicked(lv_event_t* e)
{
    WiFiConfigScreen* screen = static_cast<WiFiConfigScreen*>(lv_event_get_user_data(e));
    screen->forgetWiFi();
}

void WiFiConfigScreen::onBackButtonClicked(lv_event_t* e)
{
    (void)e; // Screen pointer not needed - use wrapper function
    ESP_LOGI(TAG, "Back button clicked - returning to main screen");
    
    // Use wrapper function to safely navigate back
    close_wifi_config_screen();
}

void WiFiConfigScreen::onNetworkSelected(lv_event_t* e)
{
    WiFiConfigScreen* screen = static_cast<WiFiConfigScreen*>(lv_event_get_user_data(e));
    lv_obj_t* dropdown = lv_event_get_target(e);
    
    uint16_t selectedIdx = lv_dropdown_get_selected(dropdown);
    
    // Index 0 is "Select network..." placeholder, actual networks start at index 1
    if (selectedIdx > 0 && (selectedIdx - 1) < screen->m_scanResults.size()) {
        const std::string& ssid = screen->m_scanResults[selectedIdx - 1];
        lv_textarea_set_text(screen->m_ssidInput, ssid.c_str());
        ESP_LOGI(TAG, "Selected network: %s", ssid.c_str());
    }
}

void WiFiConfigScreen::onWiFiStateChanged(void* userData, WiFiManager::State state)
{
    WiFiConfigScreen* screen = static_cast<WiFiConfigScreen*>(userData);
    screen->updateStatus();
    
    ESP_LOGI(TAG, "WiFi state changed to %d", static_cast<int>(state));
}

void WiFiConfigScreen::performScan()
{
    ESP_LOGI(TAG, "Starting WiFi scan...");
    
    esp_err_t err = m_wifiManager.startScan();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start scan: %s", esp_err_to_name(err));
        return;
    }
    
    // Wait a bit for scan to complete (in real use, you'd use a callback/event)
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    m_scanResults = m_wifiManager.getScanResults();
    updateNetworkList();
}

void WiFiConfigScreen::updateNetworkList()
{
    if (m_scanResults.empty()) {
        lv_dropdown_set_options(m_networkList, "No networks found");
        ESP_LOGW(TAG, "No networks found in scan");
        return;
    }
    
    // Build options string for dropdown - start with placeholder
    std::string options = "Select network...";
    for (size_t i = 0; i < m_scanResults.size(); ++i) {
        options += "\n";
        options += m_scanResults[i];
    }
    
    lv_dropdown_set_options(m_networkList, options.c_str());
    lv_dropdown_set_selected(m_networkList, 0); // Select the placeholder
    ESP_LOGI(TAG, "Updated network list with %d networks", m_scanResults.size());
}

void WiFiConfigScreen::connectToWiFi()
{
    std::string ssid = getSsidText();
    std::string password = getPasswordText();
    
    // If no SSID entered, try to connect to saved network
    if (ssid.empty()) {
        if (m_wifiManager.hasStoredCredentials()) {
            ESP_LOGI(TAG, "No SSID entered - attempting to connect to saved network");
            esp_err_t err = m_wifiManager.connect();  // Use stored credentials
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to connect to saved network: %s", esp_err_to_name(err));
            }
            return;
        } else {
            ESP_LOGW(TAG, "Cannot connect: No SSID entered and no saved credentials");
            return;
        }
    }
    
    ESP_LOGI(TAG, "Connecting to WiFi: %s", ssid.c_str());
    
    esp_err_t err = m_wifiManager.connect(ssid, password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initiate connection: %s", esp_err_to_name(err));
    }
}

void WiFiConfigScreen::disconnectWiFi()
{
    ESP_LOGI(TAG, "Disconnecting from WiFi (keeping credentials)");
    m_wifiManager.disconnect();
    updateStatus();
}

void WiFiConfigScreen::forgetWiFi()
{
    ESP_LOGI(TAG, "Forgetting WiFi network and disconnecting");
    m_wifiManager.forgetNetwork();
    updateStatus();
}

std::string WiFiConfigScreen::getPasswordText() const
{
    const char* text = lv_textarea_get_text(m_passwordInput);
    return text ? std::string(text) : std::string();
}

std::string WiFiConfigScreen::getSsidText() const
{
    const char* text = lv_textarea_get_text(m_ssidInput);
    return text ? std::string(text) : std::string();
}

void WiFiConfigScreen::createKeyboard()
{
    // Create keyboard
    m_keyboard = lv_keyboard_create(m_screen);
    lv_obj_set_size(m_keyboard, SCREEN_WIDTH, SCREEN_HEIGHT / 2);
    lv_obj_align(m_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    
    // Create label ABOVE keyboard (as separate screen element, not child of keyboard)
    m_keyboardLabel = lv_label_create(m_screen);
    lv_label_set_text(m_keyboardLabel, "");
    lv_obj_set_style_text_font(m_keyboardLabel, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(m_keyboardLabel, lv_color_white(), 0);
    lv_obj_set_style_bg_color(m_keyboardLabel, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(m_keyboardLabel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(m_keyboardLabel, 10, 0);
    lv_obj_set_style_pad_ver(m_keyboardLabel, 8, 0);
    lv_obj_set_style_radius(m_keyboardLabel, 5, 0);
    // Center horizontally on screen, position just above keyboard
    lv_obj_align(m_keyboardLabel, LV_ALIGN_BOTTOM_MID, 0, -(SCREEN_HEIGHT / 2) - 35);
    
    // Hide keyboard and label initially
    lv_obj_add_flag(m_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(m_keyboardLabel, LV_OBJ_FLAG_HIDDEN);
}

void WiFiConfigScreen::showKeyboard(lv_obj_t* textarea)
{
    if (m_keyboard && textarea) {
        // Assign keyboard to the textarea
        lv_keyboard_set_textarea(m_keyboard, textarea);
        
        // Update label to show which field is being edited
        if (textarea == m_ssidInput) {
            lv_label_set_text(m_keyboardLabel, "Editing: Network Name (SSID)");
        } else if (textarea == m_passwordInput) {
            lv_label_set_text(m_keyboardLabel, "Editing: Password");
        }
        
        // Show keyboard and label
        lv_obj_clear_flag(m_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(m_keyboardLabel, LV_OBJ_FLAG_HIDDEN);
        
        ESP_LOGI(TAG, "Keyboard shown");
    }
}

void WiFiConfigScreen::hideKeyboard()
{
    if (m_keyboard) {
        lv_obj_add_flag(m_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(m_keyboardLabel, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(m_keyboard, nullptr);
        ESP_LOGI(TAG, "Keyboard hidden");
    }
}

void WiFiConfigScreen::onTextAreaFocused(lv_event_t* e)
{
    WiFiConfigScreen* screen = static_cast<WiFiConfigScreen*>(lv_event_get_user_data(e));
    lv_obj_t* textarea = lv_event_get_target(e);
    screen->showKeyboard(textarea);
}

void WiFiConfigScreen::onTextAreaDefocused(lv_event_t* e)
{
    WiFiConfigScreen* screen = static_cast<WiFiConfigScreen*>(lv_event_get_user_data(e));
    screen->hideKeyboard();
}
