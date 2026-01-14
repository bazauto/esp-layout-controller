#include "wifi_config_wrapper.h"
#include "WiFiConfigScreen.h"
#include "WiFiManager.h"
#include "lvgl.h"
#include "esp_log.h"

static const char* TAG = "WiFiWrapper";

// Global WiFiManager instance (should be managed by application controller in future)
static WiFiManager* g_wifiManager = nullptr;
static WiFiConfigScreen* g_wifiConfigScreen = nullptr;
static lv_obj_t* g_previousScreen = nullptr;

void init_wifi_manager(void)
{
    if (g_wifiManager != nullptr) {
        ESP_LOGW(TAG, "WiFi manager already initialized");
        return;
    }
    
    ESP_LOGI(TAG, "Initializing WiFi manager");
    g_wifiManager = new WiFiManager();
    g_wifiManager->initialize();
    
    // Attempt to connect using stored credentials
    ESP_LOGI(TAG, "Attempting auto-connect with stored credentials");
    esp_err_t ret = g_wifiManager->connect();
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Auto-connect initiated");
    } else if (ret == ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG, "No stored credentials found - use settings to configure WiFi");
    } else {
        ESP_LOGW(TAG, "Auto-connect failed: %s", esp_err_to_name(ret));
    }
}

void show_wifi_config_screen(void)
{
    // Save the current screen before switching
    g_previousScreen = lv_scr_act();
    
    // Initialize WiFiManager if not already done
    if (g_wifiManager == nullptr) {
        g_wifiManager = new WiFiManager();
        g_wifiManager->initialize();
    }
    
    // Don't delete old screen - LVGL will handle it
    
    // Create and show new WiFi config screen
    g_wifiConfigScreen = new WiFiConfigScreen(*g_wifiManager);
    g_wifiConfigScreen->create();
}

void close_wifi_config_screen(void)
{
    // Load previous screen first (before deleting)
    if (g_previousScreen) {
        lv_scr_load(g_previousScreen);
    }
    
    // Now safe to delete WiFi config screen
    if (g_wifiConfigScreen != nullptr) {
        delete g_wifiConfigScreen;
        g_wifiConfigScreen = nullptr;
    }
}

bool is_wifi_connected(void)
{
    if (g_wifiManager == nullptr) {
        return false;
    }
    return g_wifiManager->isConnected();
}
