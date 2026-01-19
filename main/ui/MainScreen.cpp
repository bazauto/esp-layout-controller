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
    , m_powerStatusBar(nullptr)
    , m_rosterCarousel(nullptr)
    , m_wiThrottleClient(nullptr)
    , m_jmriClient(nullptr)
{
    // Throttles are now managed by ThrottleController
}

MainScreen::~MainScreen()
{
    // Don't delete LVGL objects here - LVGL manages screen lifecycle
    // When lv_scr_load() is called with a new screen, LVGL will clean up the old one
    // Our ThrottleMeter objects will be destroyed naturally with their parent containers
}

lv_obj_t* MainScreen::create(WiThrottleClient* wiThrottleClient, JmriJsonClient* jmriClient, ThrottleController* throttleController)
{
    m_wiThrottleClient = wiThrottleClient;
    m_jmriClient = jmriClient;
    m_throttleController = throttleController;  // Store reference (not owned)
    
    // Register UI update callback with the controller
    if (m_throttleController) {
        m_throttleController->setUIUpdateCallback(onUIUpdateNeeded, this);
    }
    
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
    
    // Initial UI update
    updateAllThrottles();
    
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
    
    // Use a single row on left for meters (maximize vertical space)
    static lv_coord_t leftCol[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t leftRow[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(m_leftPanel, leftCol, leftRow);
}

void MainScreen::createRightPanel()
{
    // Right half - track power and virtual encoder
    m_rightPanel = lv_obj_create(lv_obj_get_parent(m_leftPanel));
    lv_obj_set_grid_cell(m_rightPanel, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_flex_flow(m_rightPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(m_rightPanel, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(m_rightPanel, 10, 0);
    lv_obj_set_style_pad_row(m_rightPanel, 10, 0);
    lv_obj_set_style_pad_bottom(m_rightPanel, 70, 0);
    
    // Add track power controls at the top
    m_powerStatusBar = std::make_unique<PowerStatusBar>();
    m_powerStatusBar->create(m_rightPanel, m_jmriClient);
    
    // Roster selection carousel
    m_rosterCarousel = std::make_unique<RosterCarousel>();
    m_rosterCarousel->create(m_rightPanel);
    
    // Virtual encoder panel for testing
    m_virtualEncoderPanel = std::make_unique<VirtualEncoderPanel>();
    m_virtualEncoderPanel->create(m_rightPanel, 
                                  onVirtualEncoderRotation,
                                  onVirtualEncoderPress,
                                  this);
}

void MainScreen::createThrottleMeters()
{
    // Container for the 2x2 grid of meters (full height)
    lv_obj_t* meterGrid = lv_obj_create(m_leftPanel);
    lv_obj_remove_style_all(meterGrid);
    lv_obj_set_grid_cell(meterGrid, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    
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
            
            // Wire up callbacks (store throttleId in user data)
            m_throttleMeters[meterIdx]->setKnobTouchCallback(onKnobIndicatorTouched, this);
            m_throttleMeters[meterIdx]->setFunctionsCallback(onFunctionsButtonClicked, this);
            m_throttleMeters[meterIdx]->setReleaseCallback(onReleaseButtonClicked, this);
            
            // Store throttle ID in the meter container's user data
            lv_obj_set_user_data(m_throttleMeters[meterIdx]->getContainer(), (void*)(intptr_t)meterIdx);
            
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
    
    if (!m_throttleController || !m_throttleMeters[throttleId]) {
        return;
    }

    ThrottleController::ThrottleSnapshot snapshot;
    if (!m_throttleController->getThrottleSnapshot(throttleId, snapshot)) {
        return;
    }

    ThrottleMeter* meter = m_throttleMeters[throttleId].get();

    // Update speed display
    meter->setValue(snapshot.currentSpeed);

    // Update direction indicator
    meter->setDirection(snapshot.direction);

    // Update loco info
    if (snapshot.hasLocomotive) {
        meter->setLocomotive(snapshot.locoName.c_str(), snapshot.locoAddress);
    } else {
        meter->clearLocomotive();
    }

    // Update knob assignment indicators
    int assignedKnob = snapshot.assignedKnob;
    meter->setAssignedKnob(assignedKnob);

    // Update knob availability (disable indicator if other knob is active)
    if (assignedKnob >= 0) {
        // One knob is assigned, disable the other
        meter->setKnobAvailable(0, assignedKnob == 0);
        meter->setKnobAvailable(1, assignedKnob == 1);
    } else {
        // No knob assigned, both available
        meter->setKnobAvailable(0, true);
        meter->setKnobAvailable(1, true);
    }
}

void MainScreen::updateAllThrottles()
{
    for (int i = 0; i < 4; ++i) {
        updateThrottle(i);
    }

    if (m_rosterCarousel) {
        m_rosterCarousel->update(m_throttleController);
    }
}
Throttle* MainScreen::getThrottle(int throttleId)
{
    if (!m_throttleController) return nullptr;
    return m_throttleController->getThrottle(throttleId);
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
    WiThrottleClient::Locomotive loco;
    if (!screen->m_wiThrottleClient->getRosterEntry(0, loco)) {
        ESP_LOGW(TAG, "No locomotives in roster");
        return;
    }
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

void MainScreen::onOldReleaseButtonClicked(lv_event_t* e)
{
    MainScreen* screen = static_cast<MainScreen*>(lv_event_get_user_data(e));
    
    if (!screen->m_wiThrottleClient || !screen->m_wiThrottleClient->isConnected()) {
        ESP_LOGW(TAG, "WiThrottle not connected");
        return;
    }
    
    ESP_LOGI(TAG, "Releasing throttle T");
    screen->m_wiThrottleClient->releaseLocomotive('T');
}

void MainScreen::onKnobIndicatorTouched(lv_event_t* e)
{
    MainScreen* screen = static_cast<MainScreen*>(lv_event_get_user_data(e));
    if (!screen->m_throttleController) return;
    
    // Get the knob indicator button that was touched
    lv_obj_t* indicator = lv_event_get_target(e);
    int knobId = (int)(intptr_t)lv_obj_get_user_data(indicator);
    
    // Find the throttle meter container by walking up parent chain
    lv_obj_t* current = indicator;
    int throttleId = -1;
    while (current) {
        for (int i = 0; i < 4; i++) {
            if (screen->m_throttleMeters[i] && current == screen->m_throttleMeters[i]->getContainer()) {
                throttleId = i;
                break;
            }
        }
        if (throttleId >= 0) break;
        current = lv_obj_get_parent(current);
    }
    
    if (throttleId >= 0) {
        ESP_LOGI(TAG, "Knob %d indicator touched on throttle %d", knobId, throttleId);
        screen->m_throttleController->onKnobIndicatorTouched(throttleId, knobId);
    }
}

void MainScreen::onFunctionsButtonClicked(lv_event_t* e)
{
    MainScreen* screen = static_cast<MainScreen*>(lv_event_get_user_data(e));
    if (!screen->m_throttleController) return;
    
    // Find the throttle meter container by walking up parent chain
    lv_obj_t* button = lv_event_get_target(e);
    lv_obj_t* current = button;
    int throttleId = -1;
    while (current) {
        for (int i = 0; i < 4; i++) {
            if (screen->m_throttleMeters[i] && current == screen->m_throttleMeters[i]->getContainer()) {
                throttleId = i;
                break;
            }
        }
        if (throttleId >= 0) break;
        current = lv_obj_get_parent(current);
    }
    
    if (throttleId >= 0) {
        ESP_LOGI(TAG, "Functions button clicked on throttle %d", throttleId);
        screen->m_throttleController->onThrottleFunctions(throttleId);
    }
}

void MainScreen::onReleaseButtonClicked(lv_event_t* e)
{
    MainScreen* screen = static_cast<MainScreen*>(lv_event_get_user_data(e));
    if (!screen->m_throttleController) return;
    
    // Find the throttle meter container by walking up parent chain
    lv_obj_t* button = lv_event_get_target(e);
    lv_obj_t* current = button;
    int throttleId = -1;
    while (current) {
        for (int i = 0; i < 4; i++) {
            if (screen->m_throttleMeters[i] && current == screen->m_throttleMeters[i]->getContainer()) {
                throttleId = i;
                break;
            }
        }
        if (throttleId >= 0) break;
        current = lv_obj_get_parent(current);
    }
    
    if (throttleId >= 0) {
        ESP_LOGI(TAG, "Release button clicked on throttle %d", throttleId);
        screen->m_throttleController->onThrottleRelease(throttleId);
    }
}

void MainScreen::onUIUpdateNeeded(void* userData)
{
    MainScreen* screen = static_cast<MainScreen*>(userData);
    if (screen) {
        // CRITICAL: Lock LVGL mutex before accessing LVGL objects
        // This callback may be called from WiThrottle network task
        if (lvgl_port_lock(100)) {
            screen->updateAllThrottles();
            lvgl_port_unlock();
        } else {
            ESP_LOGW(TAG, "Failed to acquire LVGL lock for UI update");
        }
    }
}

void MainScreen::onVirtualEncoderRotation(void* userData, int knobId, int delta)
{
    MainScreen* screen = static_cast<MainScreen*>(userData);
    if (!screen->m_throttleController) return;
    
    ESP_LOGI(TAG, "Virtual encoder: knob %d rotated %+d", knobId, delta);
    screen->m_throttleController->onKnobRotation(knobId, delta);
}

void MainScreen::onVirtualEncoderPress(void* userData, int knobId)
{
    MainScreen* screen = static_cast<MainScreen*>(userData);
    if (!screen->m_throttleController) return;
    
    ESP_LOGI(TAG, "Virtual encoder: knob %d pressed", knobId);
    screen->m_throttleController->onKnobPress(knobId);
}
