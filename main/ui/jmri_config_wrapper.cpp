#include "jmri_config_wrapper.h"
#include "main_screen_wrapper.h"
#include "wifi_config_wrapper.h"
#include "JmriConfigScreen.h"
#include "../communication/JmriJsonClient.h"
#include "../communication/WiThrottleClient.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "JmriConfigWrapper";

// NVS keys
static const char* NVS_NAMESPACE = "jmri";
static const char* NVS_KEY_SERVER_IP = "server_ip";
static const char* NVS_KEY_JSON_PORT = "json_port";
static const char* NVS_KEY_WITHROTTLE_PORT = "wt_port";
static const char* NVS_KEY_POWER_MANAGER = "power_mgr";

// Auto-reconnect state
static TaskHandle_t g_reconnectTask = nullptr;
static bool g_autoReconnectEnabled = false;
static std::string g_savedServerIp;
static uint16_t g_savedJsonPort = 12080;
static uint16_t g_savedWtPort = 12090;
static std::string g_savedPowerMgr = "DCC++";

// Global JMRI config screen instance
static JmriConfigScreen* g_jmriConfigScreen = nullptr;

// Forward declaration
static void jmri_reconnect_task(void* arg);

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
    
    // Load power manager name
    char powerMgr[64] = "DCC++";
    length = sizeof(powerMgr);
    nvs_get_str(handle, NVS_KEY_POWER_MANAGER, powerMgr, &length);
    
    nvs_close(handle);
    
    // Parse ports
    uint16_t jsonPort = std::atoi(jsonPortStr);
    uint16_t wtPort = std::atoi(wtPortStr);
    
    if (jsonPort == 0) jsonPort = 12080;
    if (wtPort == 0) wtPort = 12090;
    
    // Save settings for auto-reconnect
    g_savedServerIp = serverIp;
    g_savedJsonPort = jsonPort;
    g_savedWtPort = wtPort;
    g_savedPowerMgr = powerMgr;
    
    ESP_LOGI(TAG, "Auto-connecting to JMRI: %s (JSON:%d, WiThrottle:%d, Power:%s)", 
             serverIp, jsonPort, wtPort, powerMgr);
    
    // Get clients
    JmriJsonClient* jsonClient = get_jmri_client();
    WiThrottleClient* wtClient = get_withrottle_client();
    
    if (!jsonClient || !wtClient) {
        ESP_LOGE(TAG, "Clients not initialized");
        return false;
    }
    
    // Set power manager name before connecting
    jsonClient->setConfiguredPowerName(g_savedPowerMgr);
    
    // Attempt connection - don't crash if it fails
    err = jsonClient->connect(g_savedServerIp.c_str(), g_savedJsonPort);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "JSON client auto-connect failed (will remain disconnected)");
    }
    
    err = wtClient->connect(g_savedServerIp.c_str(), g_savedWtPort);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiThrottle client auto-connect failed (will remain disconnected)");
    }
    
    // Enable auto-reconnect (will start monitoring task if not already running)
    g_autoReconnectEnabled = true;
    if (g_reconnectTask == nullptr) {
        xTaskCreate(jmri_reconnect_task, "jmri_reconnect", 3072, NULL, 4, &g_reconnectTask);
    }
    
    return true;
}

static void jmri_reconnect_task(void* arg)
{
    ESP_LOGI(TAG, "Auto-reconnect task started");
    
    int failedAttempts = 0;
    const int maxBackoff = 60; // Max 60 seconds between retries
    
    while (true) {
        // Check every 5 seconds
        vTaskDelay(pdMS_TO_TICKS(5000));
        
        // Skip if auto-reconnect disabled or no WiFi
        if (!g_autoReconnectEnabled || !is_wifi_connected()) {
            failedAttempts = 0;
            continue;
        }
        
        JmriJsonClient* jsonClient = get_jmri_client();
        WiThrottleClient* wtClient = get_withrottle_client();
        
        if (!jsonClient || !wtClient) {
            continue;
        }
        
        bool jsonConnected = jsonClient->isConnected();
        bool wtConnected = wtClient->isConnected();
        
        // If both connected, reset failure counter
        if (jsonConnected && wtConnected) {
            if (failedAttempts > 0) {
                ESP_LOGI(TAG, "Connection restored");
                failedAttempts = 0;
            }
            continue;
        }
        
        // Calculate backoff delay (exponential: 5, 10, 20, 30, 60, 60...)
        int backoffDelay = 5 * (1 << failedAttempts); // 5 * 2^attempts
        if (backoffDelay > maxBackoff) {
            backoffDelay = maxBackoff;
        }
        
        ESP_LOGW(TAG, "JMRI disconnected (attempt %d, next retry in %ds)", 
                 failedAttempts + 1, backoffDelay);
        
        // Wait with backoff
        vTaskDelay(pdMS_TO_TICKS(backoffDelay * 1000));
        
        // Attempt reconnection
        if (!jsonConnected && !g_savedServerIp.empty()) {
            ESP_LOGI(TAG, "Attempting to reconnect JSON client...");
            jsonClient->setConfiguredPowerName(g_savedPowerMgr);
            esp_err_t err = jsonClient->connect(g_savedServerIp.c_str(), g_savedJsonPort);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "JSON client reconnected");
            }
        }
        
        if (!wtConnected && !g_savedServerIp.empty()) {
            ESP_LOGI(TAG, "Attempting to reconnect WiThrottle client...");
            esp_err_t err = wtClient->connect(g_savedServerIp.c_str(), g_savedWtPort);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "WiThrottle client reconnected");
            }
        }
        
        failedAttempts++;
    }
}
