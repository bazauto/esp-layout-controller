#include "jmri_config_wrapper.h"
#include "main_screen_wrapper.h"
#include "wifi_config_wrapper.h"
#include "JmriConfigScreen.h"
#include "../communication/JmriJsonClient.h"
#include "../communication/WiThrottleClient.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char* TAG = "JmriConfigWrapper";

// NVS keys
static const char* NVS_NAMESPACE = "jmri";
static const char* NVS_KEY_SERVER_IP = "server_ip";
static const char* NVS_KEY_JSON_PORT = "json_port";
static const char* NVS_KEY_WITHROTTLE_PORT = "wt_port";

// Global JMRI config screen instance
static JmriConfigScreen* g_jmriConfigScreen = nullptr;

extern "C" void show_jmri_config_screen(void)
{
    JmriJsonClient* jsonClient = get_jmri_client();
    WiThrottleClient* wtClient = get_withrottle_client();
    
    if (!jsonClient || !wtClient) {
        ESP_LOGE(TAG, "Clients not initialized");
        return;
    }
    
    // Don't delete old screen - LVGL will handle it
    
    // Create and show JMRI config screen
    g_jmriConfigScreen = new JmriConfigScreen(*jsonClient, *wtClient);
    g_jmriConfigScreen->create();
}

extern "C" bool jmri_auto_connect(void)
{
    // Check if WiFi is connected first
    if (!is_wifi_connected()) {
        ESP_LOGI(TAG, "WiFi not connected, skipping JMRI auto-connect");
        return false;
    }
    
    // Check if settings exist
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "No saved JMRI settings for auto-connect");
        return false;
    }
    
    char serverIp[64] = {0};
    char jsonPortStr[8] = "12080";
    char wtPortStr[8] = "12090";
    size_t length;
    
    // Load server IP
    length = sizeof(serverIp);
    err = nvs_get_str(handle, NVS_KEY_SERVER_IP, serverIp, &length);
    if (err != ESP_OK || serverIp[0] == '\0') {
        ESP_LOGD(TAG, "No server IP saved");
        nvs_close(handle);
        return false;
    }
    
    // Load ports (optional, use defaults if not set)
    length = sizeof(jsonPortStr);
    nvs_get_str(handle, NVS_KEY_JSON_PORT, jsonPortStr, &length);
    
    length = sizeof(wtPortStr);
    nvs_get_str(handle, NVS_KEY_WITHROTTLE_PORT, wtPortStr, &length);
    
    nvs_close(handle);
    
    // Parse ports
    uint16_t jsonPort = std::atoi(jsonPortStr);
    uint16_t wtPort = std::atoi(wtPortStr);
    
    if (jsonPort == 0) jsonPort = 12080;
    if (wtPort == 0) wtPort = 12090;
    
    ESP_LOGI(TAG, "Auto-connecting to JMRI: %s (JSON:%d, WiThrottle:%d)", 
             serverIp, jsonPort, wtPort);
    
    // Get clients
    JmriJsonClient* jsonClient = get_jmri_client();
    WiThrottleClient* wtClient = get_withrottle_client();
    
    if (!jsonClient || !wtClient) {
        ESP_LOGE(TAG, "Clients not initialized");
        return false;
    }
    
    // Attempt connection - don't crash if it fails
    err = jsonClient->connect(serverIp, jsonPort);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "JSON client auto-connect failed (will remain disconnected)");
    }
    
    err = wtClient->connect(serverIp, wtPort);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiThrottle client auto-connect failed (will remain disconnected)");
    }
    
    return true;
}
