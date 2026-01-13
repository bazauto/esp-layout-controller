#include "MainScreen.h"
#include "wifi_config_wrapper.h"
#include "esp_log.h"

static const char* TAG = "MainScreen";

// Screen dimensions
static const int SCREEN_WIDTH = 800;
static const int SCREEN_HEIGHT = 480;

MainScreen::MainScreen()
    : m_screen(nullptr)
    , m_leftPanel(nullptr)
    , m_rightPanel(nullptr)
    , m_settingsButton(nullptr)
{
    // Initialize throttles
    for (int i = 0; i < 4; ++i) {
        m_throttles[i] = Throttle(i);
    }
}

MainScreen::~MainScreen()
{
    // LVGL will clean up objects when screen is deleted
    if (m_screen) {
        lv_obj_del(m_screen);
    }
}

lv_obj_t* MainScreen::create()
{
    // Get the active screen (created by default)
    m_screen = lv_scr_act();
    
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
    // Right half - placeholder for future use
    m_rightPanel = lv_obj_create(lv_obj_get_parent(m_leftPanel));
    lv_obj_set_grid_cell(m_rightPanel, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
    
    // Add a label as placeholder
    lv_obj_t* placeholderLabel = lv_label_create(m_rightPanel);
    lv_label_set_text(placeholderLabel, "Loco Details\n(Coming Soon)");
    lv_obj_set_style_text_align(placeholderLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(placeholderLabel);
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
    
    // Add gear icon
    lv_obj_t* btnLabel = lv_label_create(m_settingsButton);
    lv_label_set_text(btnLabel, LV_SYMBOL_SETTINGS);
    lv_obj_center(btnLabel);
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
