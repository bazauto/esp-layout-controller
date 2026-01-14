#include "MainScreen.h"
#include "wifi_config_wrapper.h"
#include "jmri_config_wrapper.h"
#include "esp_log.h"
#include "lvgl_port.h"

extern "C" {
    bool lvgl_port_lock(int timeout_ms);
    void lvgl_port_unlock(void);
}

static const char* TAG = "MainScreen";

// Screen dimensions
static const int SCREEN_WIDTH = 800;
static const int SCREEN_HEIGHT = 480;

MainScreen::MainScreen()
    : m_screen(nullptr)
    , m_leftPanel(nullptr)
    , m_rightPanel(nullptr)
    , m_settingsButton(nullptr)
    , m_trackPowerButton(nullptr)
    , m_connectionStatusLabel(nullptr)
    , m_wiThrottleClient(nullptr)
    , m_jmriClient(nullptr)
{
    // Initialize throttles
    for (int i = 0; i < 4; ++i) {
        m_throttles[i] = Throttle(i);
    }
}

MainScreen::~MainScreen()
{
    // Don't delete LVGL objects here - LVGL manages screen lifecycle
    // When lv_scr_load() is called with a new screen, LVGL will clean up the old one
    // Our ThrottleMeter objects will be destroyed naturally with their parent containers
}

lv_obj_t* MainScreen::create(WiThrottleClient* wiThrottleClient, JmriJsonClient* jmriClient)
{
    m_wiThrottleClient = wiThrottleClient;
    m_jmriClient = jmriClient;
    
    // Create a new screen or clean the current one
    m_screen = lv_obj_create(nullptr);
    
    // Load the new screen (this makes it active and hides previous screen)
    lv_scr_load(m_screen);
    
    // Create left and right panels
    createLeftPanel();
    createRightPanel();
    
    // Create throttle meters in left panel
    createThrottleMeters();
    
    // Create settings button
    createSettingsButton();
    
    ESP_LOGI(TAG, "Main screen created");
    
    return m_screen;
}

void MainScreen::createLeftPanel()
{
    // Main container: horizontal split left/right
    lv_obj_t* mainCont = lv_obj_create(m_screen);
    lv_obj_remove_style_all(mainCont);
    lv_obj_set_size(mainCont, LV_PCT(100), LV_PCT(100));
    
    // Use grid for main container: 2 columns (left/right)
    static lv_coord_t mainCol[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t mainRow[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(mainCont, mainCol, mainRow);
    
    // Left half (will contain 2x2 grid of meters)
    m_leftPanel = lv_obj_create(mainCont);
    lv_obj_remove_style_all(m_leftPanel);
    lv_obj_set_grid_cell(m_leftPanel, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    
    // Use 3 rows on left: top padding, meters, bottom padding
    static lv_coord_t leftCol[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t leftRow[] = {LV_GRID_FR(1), LV_GRID_FR(8), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(m_leftPanel, leftCol, leftRow);
}

void MainScreen::createRightPanel()
{
    // Right half - track power and test controls
    m_rightPanel = lv_obj_create(lv_obj_get_parent(m_leftPanel));
    lv_obj_set_grid_cell(m_rightPanel, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_flex_flow(m_rightPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(m_rightPanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(m_rightPanel, 10, 0);
    lv_obj_set_style_pad_row(m_rightPanel, 10, 0);
    
    // Add track power controls at the top
    createTrackPowerControls(m_rightPanel);
    
    // Test Controls Section
    lv_obj_t* testLabel = lv_label_create(m_rightPanel);
    lv_label_set_text(testLabel, "WiThrottle Test Controls");
    lv_obj_set_style_text_font(testLabel, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(testLabel, lv_color_hex(0x00AAFF), 0);
    lv_obj_set_style_pad_top(testLabel, 20, 0);
    
    // Info label
    lv_obj_t* infoLabel = lv_label_create(m_rightPanel);
    lv_label_set_text(infoLabel, "Controls first loco in roster on Throttle 'T'");
    lv_obj_set_style_text_color(infoLabel, lv_color_hex(0x808080), 0);
    
    // Acquire button
    lv_obj_t* acquireBtn = lv_btn_create(m_rightPanel);
    lv_obj_set_size(acquireBtn, 250, 50);
    lv_obj_add_event_cb(acquireBtn, onAcquireButtonClicked, LV_EVENT_CLICKED, this);
    lv_obj_set_style_bg_color(acquireBtn, lv_color_hex(0x00AA00), 0);
    lv_obj_t* acquireLabel = lv_label_create(acquireBtn);
    lv_label_set_text(acquireLabel, "Acquire Loco");
    lv_obj_center(acquireLabel);
    
    // Speed control buttons in a row
    lv_obj_t* speedRow = lv_obj_create(m_rightPanel);
    lv_obj_remove_style_all(speedRow);
    lv_obj_set_size(speedRow, LV_PCT(100), 50);
    lv_obj_set_flex_flow(speedRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(speedRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // Speed buttons
    const char* speedLabels[] = {"Stop", "Slow", "Med", "Fast"};
    const int speeds[] = {0, 30, 60, 100};
    for (int i = 0; i < 4; i++) {
        lv_obj_t* speedBtn = lv_btn_create(speedRow);
        lv_obj_set_size(speedBtn, 60, 50);
        lv_obj_set_user_data(speedBtn, (void*)(intptr_t)speeds[i]);
        lv_obj_add_event_cb(speedBtn, onSpeedButtonClicked, LV_EVENT_CLICKED, this);
        lv_obj_t* label = lv_label_create(speedBtn);
        lv_label_set_text(label, speedLabels[i]);
        lv_obj_center(label);
    }
    
    // Direction buttons in a row
    lv_obj_t* dirRow = lv_obj_create(m_rightPanel);
    lv_obj_remove_style_all(dirRow);
    lv_obj_set_size(dirRow, LV_PCT(100), 50);
    lv_obj_set_flex_flow(dirRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dirRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    lv_obj_t* fwdBtn = lv_btn_create(dirRow);
    lv_obj_set_size(fwdBtn, 120, 50);
    lv_obj_add_event_cb(fwdBtn, onForwardButtonClicked, LV_EVENT_CLICKED, this);
    lv_obj_t* fwdLabel = lv_label_create(fwdBtn);
    lv_label_set_text(fwdLabel, "Forward");
    lv_obj_center(fwdLabel);
    
    lv_obj_t* revBtn = lv_btn_create(dirRow);
    lv_obj_set_size(revBtn, 120, 50);
    lv_obj_add_event_cb(revBtn, onReverseButtonClicked, LV_EVENT_CLICKED, this);
    lv_obj_t* revLabel = lv_label_create(revBtn);
    lv_label_set_text(revLabel, "Reverse");
    lv_obj_center(revLabel);
    
    // Function F0 (lights) button
    lv_obj_t* f0Btn = lv_btn_create(m_rightPanel);
    lv_obj_set_size(f0Btn, 250, 50);
    lv_obj_add_event_cb(f0Btn, onF0ButtonClicked, LV_EVENT_CLICKED, this);
    lv_obj_t* f0Label = lv_label_create(f0Btn);
    lv_label_set_text(f0Label, "Toggle F0 (Lights)");
    lv_obj_center(f0Label);
    
    // Release button
    lv_obj_t* releaseBtn = lv_btn_create(m_rightPanel);
    lv_obj_set_size(releaseBtn, 250, 50);
    lv_obj_add_event_cb(releaseBtn, onReleaseButtonClicked, LV_EVENT_CLICKED, this);
    lv_obj_set_style_bg_color(releaseBtn, lv_color_hex(0xAA0000), 0);
    lv_obj_t* releaseLabel = lv_label_create(releaseBtn);
    lv_label_set_text(releaseLabel, "Release Loco");
    lv_obj_center(releaseLabel);
}

void MainScreen::createThrottleMeters()
{
    // Container for the 2x2 grid of meters (in the middle row)
    lv_obj_t* meterGrid = lv_obj_create(m_leftPanel);
    lv_obj_remove_style_all(meterGrid);
    lv_obj_set_grid_cell(meterGrid, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    
    // Grid for meters: 2 columns x 2 rows
    static lv_coord_t gridCol[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t gridRow[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(meterGrid, gridCol, gridRow);
    
    // Create 4 containers and place ThrottleMeters inside
    int meterIdx = 0;
    for (int r = 0; r < 2; ++r) {
        for (int c = 0; c < 2; ++c) {
            lv_obj_t* cell = lv_obj_create(meterGrid);
            lv_obj_remove_style_all(cell);
            lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_STRETCH, c, 1, LV_GRID_ALIGN_STRETCH, r, 1);
            
            // Create C++ throttle meter with scale 0.9 to leave padding
            m_throttleMeters[meterIdx] = std::make_unique<ThrottleMeter>(cell, 0.9f);
            
            meterIdx++;
        }
    }
}

void MainScreen::createSettingsButton()
{
    // Settings button in bottom-right corner
    m_settingsButton = lv_btn_create(m_screen);
    lv_obj_set_size(m_settingsButton, 80, 50);
    lv_obj_align(m_settingsButton, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_add_event_cb(m_settingsButton, onSettingsButtonClicked, LV_EVENT_CLICKED, this);
    
    // Add WiFi icon
    lv_obj_t* btnLabel = lv_label_create(m_settingsButton);
    lv_label_set_text(btnLabel, LV_SYMBOL_WIFI);
    lv_obj_center(btnLabel);
    
    // JMRI button next to settings button
    lv_obj_t* jmriButton = lv_btn_create(m_screen);
    lv_obj_set_size(jmriButton, 80, 50);
    lv_obj_align(jmriButton, LV_ALIGN_BOTTOM_RIGHT, -100, -10);
    lv_obj_add_event_cb(jmriButton, onJmriButtonClicked, LV_EVENT_CLICKED, this);
    
    // Add settings icon
    lv_obj_t* jmriLabel = lv_label_create(jmriButton);
    lv_label_set_text(jmriLabel, LV_SYMBOL_SETTINGS);
    lv_obj_center(jmriLabel);
}

void MainScreen::onJmriButtonClicked(lv_event_t* e)
{
    ESP_LOGI(TAG, "JMRI button clicked");
    show_jmri_config_screen();
}

void MainScreen::onSettingsButtonClicked(lv_event_t* e)
{
    (void)e;
    show_wifi_config_screen();
}

void MainScreen::updateThrottle(int throttleId)
{
    if (throttleId < 0 || throttleId >= 4) {
        ESP_LOGW(TAG, "Invalid throttle ID: %d", throttleId);
        return;
    }
    
    // TODO: Update the throttle meter display with current throttle state
    // This will be implemented when we connect the meters to the model
    ESP_LOGD(TAG, "Update throttle %d", throttleId);
}

void MainScreen::updateAllThrottles()
{
    for (int i = 0; i < 4; ++i) {
        updateThrottle(i);
    }
}

Throttle* MainScreen::getThrottle(int throttleId)
{
    if (throttleId < 0 || throttleId >= 4) {
        return nullptr;
    }
    return &m_throttles[throttleId];
}

void MainScreen::createTrackPowerControls(lv_obj_t* parent)
{
    // Create a horizontal status bar at the top of the right panel
    lv_obj_t* statusBar = lv_obj_create(parent);
    lv_obj_set_size(statusBar, LV_PCT(90), 50);
    lv_obj_align(statusBar, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_flex_flow(statusBar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(statusBar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(statusBar, 5, 0);
    lv_obj_set_style_pad_column(statusBar, 10, 0);
    
    // Track power button (compact, square-ish)
    m_trackPowerButton = lv_btn_create(statusBar);
    lv_obj_set_size(m_trackPowerButton, 160, 40);
    lv_obj_t* powerLabel = lv_label_create(m_trackPowerButton);
    lv_label_set_text(powerLabel, "Track Power");
    lv_obj_center(powerLabel);
    lv_obj_add_event_cb(m_trackPowerButton, onTrackPowerClicked, LV_EVENT_CLICKED, this);
    
    // Connection status indicator
    m_connectionStatusLabel = lv_label_create(statusBar);
    lv_label_set_text(m_connectionStatusLabel, LV_SYMBOL_CLOSE " Disconnected");
    lv_obj_set_style_text_color(m_connectionStatusLabel, lv_color_hex(0xFF0000), 0);
    lv_obj_center(m_connectionStatusLabel);
    
    // Set initial state
    if (m_jmriClient) {
        updateTrackPowerButton(m_trackPowerButton, m_jmriClient->getPower());
        
        // Register for power state updates
        m_jmriClient->setPowerStateCallback([this](const std::string& powerName, JmriJsonClient::PowerState state) {
            onJmriPowerChanged(this, powerName, state);
        });
        
        // Register for connection state updates
        m_jmriClient->setConnectionStateCallback([this](JmriJsonClient::ConnectionState state) {
            onJmriConnectionChanged(this, state);
        });
        
        // Update initial connection status
        updateConnectionStatus();
    }
}

void MainScreen::updateTrackPowerButton(lv_obj_t* button, JmriJsonClient::PowerState state)
{
    if (!button) return;
    
    lv_obj_t* label = lv_obj_get_child(button, 0);
    if (!label) return;
    
    // Determine button color and text based on state
    uint32_t color;
    const char* stateText;
    
    switch (state) {
        case JmriJsonClient::PowerState::ON:
            color = 0x00AA00;  // Green
            stateText = "Power";
            break;
        case JmriJsonClient::PowerState::OFF:
            color = 0xAA0000;  // Red
            stateText = "Power";
            break;
        default:
            color = 0x888888;  // Gray
            stateText = "Power";
            break;
    }
    
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_label_set_text(label, stateText);
}

void MainScreen::onTrackPowerClicked(lv_event_t* e)
{
    MainScreen* screen = static_cast<MainScreen*>(lv_event_get_user_data(e));
    
    if (!screen->m_jmriClient || !screen->m_jmriClient->isConnected()) {
        ESP_LOGW(TAG, "Not connected to JMRI server");
        return;
    }
    
    // Toggle power state
    auto currentState = screen->m_jmriClient->getPower();
    bool newState = (currentState != JmriJsonClient::PowerState::ON);
    
    ESP_LOGI(TAG, "Toggling track power: %s", newState ? "ON" : "OFF");
    screen->m_jmriClient->setPower(newState);
}

void MainScreen::onJmriPowerChanged(void* userData, const std::string& powerName, JmriJsonClient::PowerState state)
{
    MainScreen* screen = static_cast<MainScreen*>(userData);
    
    // Update the power button
    // Note: This is called from JMRI JSON receive task, so we need LVGL lock
    if (lvgl_port_lock(-1)) {
        screen->updateTrackPowerButton(screen->m_trackPowerButton, state);
        lvgl_port_unlock();
    }
}

void MainScreen::onJmriConnectionChanged(void* userData, JmriJsonClient::ConnectionState state)
{
    MainScreen* screen = static_cast<MainScreen*>(userData);
    
    // Update connection status
    // Note: This is called from WebSocket task, so we need LVGL lock
    if (lvgl_port_lock(-1)) {
        screen->updateConnectionStatus();
        lvgl_port_unlock();
    }
}

void MainScreen::updateConnectionStatus()
{
    if (!m_connectionStatusLabel || !m_jmriClient) return;
    
    auto state = m_jmriClient->getState();
    
    const char* icon;
    const char* text;
    uint32_t color;
    
    switch (state) {
        case JmriJsonClient::ConnectionState::CONNECTED:
            icon = LV_SYMBOL_OK;
            text = " Connected";
            color = 0x00AA00;  // Green
            break;
        case JmriJsonClient::ConnectionState::CONNECTING:
            icon = LV_SYMBOL_REFRESH;
            text = " Connecting";
            color = 0xFFAA00;  // Orange
            break;
        case JmriJsonClient::ConnectionState::FAILED:
            icon = LV_SYMBOL_WARNING;
            text = " Failed";
            color = 0xFF0000;  // Red
            break;
        case JmriJsonClient::ConnectionState::DISCONNECTED:
        default:
            icon = LV_SYMBOL_CLOSE;
            text = " Disconnected";
            color = 0x888888;  // Gray
            break;
    }
    
    std::string statusText = std::string(icon) + text;
    lv_label_set_text(m_connectionStatusLabel, statusText.c_str());
    lv_obj_set_style_text_color(m_connectionStatusLabel, lv_color_hex(color), 0);
}

// Test control event handlers
void MainScreen::onAcquireButtonClicked(lv_event_t* e)
{
    MainScreen* screen = static_cast<MainScreen*>(lv_event_get_user_data(e));
    
    if (!screen->m_wiThrottleClient || !screen->m_wiThrottleClient->isConnected()) {
        ESP_LOGW(TAG, "WiThrottle not connected");
        return;
    }
    
    // Get first loco from roster
    const auto& roster = screen->m_wiThrottleClient->getRoster();
    if (roster.empty()) {
        ESP_LOGW(TAG, "No locomotives in roster");
        return;
    }
    
    const auto& loco = roster[0];
    bool isLong = (loco.addressType == 'L');
    
    ESP_LOGI(TAG, "Acquiring loco: %s (addr=%d, type=%c)", 
             loco.name.c_str(), loco.address, loco.addressType);
    
    screen->m_wiThrottleClient->acquireLocomotive('T', loco.address, isLong);
}

void MainScreen::onSpeedButtonClicked(lv_event_t* e)
{
    MainScreen* screen = static_cast<MainScreen*>(lv_event_get_user_data(e));
    lv_obj_t* btn = lv_event_get_target(e);
    
    if (!screen->m_wiThrottleClient || !screen->m_wiThrottleClient->isConnected()) {
        ESP_LOGW(TAG, "WiThrottle not connected");
        return;
    }
    
    int speed = (int)(intptr_t)lv_obj_get_user_data(btn);
    ESP_LOGI(TAG, "Setting speed to %d", speed);
    
    screen->m_wiThrottleClient->setSpeed('T', speed);
}

void MainScreen::onForwardButtonClicked(lv_event_t* e)
{
    MainScreen* screen = static_cast<MainScreen*>(lv_event_get_user_data(e));
    
    if (!screen->m_wiThrottleClient || !screen->m_wiThrottleClient->isConnected()) {
        ESP_LOGW(TAG, "WiThrottle not connected");
        return;
    }
    
    ESP_LOGI(TAG, "Setting direction: FORWARD");
    screen->m_wiThrottleClient->setDirection('T', true);
}

void MainScreen::onReverseButtonClicked(lv_event_t* e)
{
    MainScreen* screen = static_cast<MainScreen*>(lv_event_get_user_data(e));
    
    if (!screen->m_wiThrottleClient || !screen->m_wiThrottleClient->isConnected()) {
        ESP_LOGW(TAG, "WiThrottle not connected");
        return;
    }
    
    ESP_LOGI(TAG, "Setting direction: REVERSE");
    screen->m_wiThrottleClient->setDirection('T', false);
}

void MainScreen::onF0ButtonClicked(lv_event_t* e)
{
    MainScreen* screen = static_cast<MainScreen*>(lv_event_get_user_data(e));
    
    if (!screen->m_wiThrottleClient || !screen->m_wiThrottleClient->isConnected()) {
        ESP_LOGW(TAG, "WiThrottle not connected");
        return;
    }
    
    // Toggle F0 - we'll just send ON for now (could track state later)
    static bool f0State = false;
    f0State = !f0State;
    
    ESP_LOGI(TAG, "Setting F0: %s", f0State ? "ON" : "OFF");
    screen->m_wiThrottleClient->setFunction('T', 0, f0State);
}

void MainScreen::onReleaseButtonClicked(lv_event_t* e)
{
    MainScreen* screen = static_cast<MainScreen*>(lv_event_get_user_data(e));
    
    if (!screen->m_wiThrottleClient || !screen->m_wiThrottleClient->isConnected()) {
        ESP_LOGW(TAG, "WiThrottle not connected");
        return;
    }
    
    ESP_LOGI(TAG, "Releasing throttle T");
    screen->m_wiThrottleClient->releaseLocomotive('T');
}
