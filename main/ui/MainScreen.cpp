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
    // Right half - loco details and track power controls
    m_rightPanel = lv_obj_create(lv_obj_get_parent(m_leftPanel));
    lv_obj_set_grid_cell(m_rightPanel, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    
    // Add track power controls
    createTrackPowerControls(m_rightPanel);
    
    // Add a label as placeholder for loco details (lower section)
    lv_obj_t* placeholderLabel = lv_label_create(m_rightPanel);
    lv_label_set_text(placeholderLabel, "Loco Details\n(Coming Soon)");
    lv_obj_set_style_text_align(placeholderLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(placeholderLabel, LV_ALIGN_BOTTOM_MID, 0, -20);
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
    // Create a container for track power button on the right side
    lv_obj_t* powerContainer = lv_obj_create(parent);
    lv_obj_set_size(powerContainer, LV_PCT(90), LV_SIZE_CONTENT);
    lv_obj_align(powerContainer, LV_ALIGN_TOP_RIGHT, -20, 60);  // Below settings button
    lv_obj_set_flex_flow(powerContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(powerContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(powerContainer, 15, 0);
    
    // Track power button
    m_trackPowerButton = lv_btn_create(powerContainer);
    lv_obj_set_size(m_trackPowerButton, LV_PCT(100), 80);
    lv_obj_t* powerLabel = lv_label_create(m_trackPowerButton);
    lv_label_set_text(powerLabel, "Track Power\n---");
    lv_obj_center(powerLabel);
    lv_obj_add_event_cb(m_trackPowerButton, onTrackPowerClicked, LV_EVENT_CLICKED, this);
    
    // Set initial state
    if (m_jmriClient) {
        updateTrackPowerButton(m_trackPowerButton, m_jmriClient->getPower());
        
        // Register for power state updates
        m_jmriClient->setPowerStateCallback([this](const std::string& powerName, JmriJsonClient::PowerState state) {
            onJmriPowerChanged(this, powerName, state);
        });
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
            stateText = "ON";
            break;
        case JmriJsonClient::PowerState::OFF:
            color = 0xAA0000;  // Red
            stateText = "OFF";
            break;
        default:
            color = 0x888888;  // Gray
            stateText = "---";
            break;
    }
    
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    
    // Update label text (preserve track name)
    const char* currentText = lv_label_get_text(label);
    std::string trackName;
    if (strstr(currentText, "Main")) {
        trackName = "Main Track\n";
    } else if (strstr(currentText, "Prog")) {
        trackName = "Prog Track\n";
    } else {
        trackName = "Track\n";
    }
    
    std::string newText = trackName + stateText;
    lv_label_set_text(label, newText.c_str());
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
