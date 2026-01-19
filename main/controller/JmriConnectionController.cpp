#include "JmriConnectionController.h"
#include "WiFiController.h"
#include "../communication/JmriJsonClient.h"
#include "../communication/WiThrottleClient.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "JmriConnCtrl";

static const char* NVS_NAMESPACE = "jmri";
static const char* NVS_KEY_SERVER_IP = "server_ip";
static const char* NVS_KEY_JSON_PORT = "json_port";
static const char* NVS_KEY_WITHROTTLE_PORT = "wt_port";
static const char* NVS_KEY_POWER_MANAGER = "power_mgr";

JmriConnectionController::JmriConnectionController(JmriJsonClient* jsonClient,
                                                   WiThrottleClient* wtClient,
                                                   WiFiController* wifiController)
    : m_jsonClient(jsonClient)
    , m_wtClient(wtClient)
    , m_wifiController(wifiController)
    , m_autoReconnectEnabled(false)
    , m_savedServerIp()
    , m_savedJsonPort(12080)
    , m_savedWtPort(12090)
    , m_savedPowerMgr("DCC++")
    , m_reconnectTaskHandle(nullptr)
    , m_autoConnectTaskHandle(nullptr)
{
}

JmriConnectionController::~JmriConnectionController() = default;

void JmriConnectionController::loadSettingsAndAutoConnect()
{
    if (!m_wifiController || !m_wifiController->isConnected()) {
        ESP_LOGI(TAG, "WiFi not connected, skipping JMRI auto-connect");
        return;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "No saved JMRI settings for auto-connect");
        return;
    }

    char serverIp[64] = {0};
    char jsonPortStr[8] = "12080";
    char wtPortStr[8] = "12090";
    char powerMgr[64] = "DCC++";
    size_t length;

    length = sizeof(serverIp);
    err = nvs_get_str(handle, NVS_KEY_SERVER_IP, serverIp, &length);
    if (err != ESP_OK || serverIp[0] == '\0') {
        ESP_LOGD(TAG, "No server IP saved");
        nvs_close(handle);
        return;
    }

    length = sizeof(jsonPortStr);
    nvs_get_str(handle, NVS_KEY_JSON_PORT, jsonPortStr, &length);

    length = sizeof(wtPortStr);
    nvs_get_str(handle, NVS_KEY_WITHROTTLE_PORT, wtPortStr, &length);

    length = sizeof(powerMgr);
    nvs_get_str(handle, NVS_KEY_POWER_MANAGER, powerMgr, &length);

    nvs_close(handle);

    uint16_t jsonPort = static_cast<uint16_t>(std::atoi(jsonPortStr));
    uint16_t wtPort = static_cast<uint16_t>(std::atoi(wtPortStr));

    if (jsonPort == 0) jsonPort = 12080;
    if (wtPort == 0) wtPort = 12090;

    m_savedServerIp = serverIp;
    m_savedJsonPort = jsonPort;
    m_savedWtPort = wtPort;
    m_savedPowerMgr = powerMgr;

    ESP_LOGI(TAG, "Auto-connecting to JMRI: %s (JSON:%d, WiThrottle:%d, Power:%s)",
             m_savedServerIp.c_str(), m_savedJsonPort, m_savedWtPort, m_savedPowerMgr.c_str());

    if (!m_jsonClient || !m_wtClient) {
        ESP_LOGE(TAG, "Clients not initialized");
        return;
    }

    m_jsonClient->setConfiguredPowerName(m_savedPowerMgr);

    err = m_jsonClient->connect(m_savedServerIp.c_str(), m_savedJsonPort);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "JSON client auto-connect failed (will remain disconnected)");
    }

    err = m_wtClient->connect(m_savedServerIp.c_str(), m_savedWtPort);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiThrottle client auto-connect failed (will remain disconnected)");
    }

    enableAutoReconnect(true);
}

void JmriConnectionController::enableAutoReconnect(bool enable)
{
    m_autoReconnectEnabled = enable;
    if (enable && m_reconnectTaskHandle == nullptr) {
        startReconnectTask();
    }
}

void JmriConnectionController::startAutoConnectTask()
{
    if (m_autoConnectTaskHandle != nullptr) {
        return;
    }

    xTaskCreate([](void* arg) {
        auto* controller = static_cast<JmriConnectionController*>(arg);
        if (!controller) {
            vTaskDelete(NULL);
            return;
        }

        for (int i = 0; i < 60; i++) {
            if (controller->m_wifiController && controller->m_wifiController->isConnected()) {
                ESP_LOGI(TAG, "WiFi connected, attempting JMRI auto-connect");
                vTaskDelay(pdMS_TO_TICKS(1000));
                controller->loadSettingsAndAutoConnect();
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        }

        controller->m_autoConnectTaskHandle = nullptr;
        vTaskDelete(NULL);
    }, "jmri_autoconn", 4096, this, 5, reinterpret_cast<TaskHandle_t*>(&m_autoConnectTaskHandle));
}

void JmriConnectionController::startReconnectTask()
{
    xTaskCreate(reconnectTask, "jmri_reconnect", 3072, this, 4, reinterpret_cast<TaskHandle_t*>(&m_reconnectTaskHandle));
}

void JmriConnectionController::reconnectTask(void* arg)
{
    auto* controller = static_cast<JmriConnectionController*>(arg);
    if (!controller) return;

    ESP_LOGI(TAG, "Auto-reconnect task started");

    int failedAttempts = 0;
    const int maxBackoff = 60;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));

        if (!controller->m_autoReconnectEnabled ||
            !controller->m_wifiController ||
            !controller->m_wifiController->isConnected()) {
            failedAttempts = 0;
            continue;
        }

        if (!controller->m_jsonClient || !controller->m_wtClient) {
            continue;
        }

        bool jsonConnected = controller->m_jsonClient->isConnected();
        bool wtConnected = controller->m_wtClient->isConnected();

        if (jsonConnected && wtConnected) {
            if (failedAttempts > 0) {
                ESP_LOGI(TAG, "Connection restored");
                failedAttempts = 0;
            }
            continue;
        }

        int backoffDelay = 5 * (1 << failedAttempts);
        if (backoffDelay > maxBackoff) {
            backoffDelay = maxBackoff;
        }

        ESP_LOGW(TAG, "JMRI disconnected (attempt %d, next retry in %ds)",
                 failedAttempts + 1, backoffDelay);

        vTaskDelay(pdMS_TO_TICKS(backoffDelay * 1000));

        if (!jsonConnected && !controller->m_savedServerIp.empty()) {
            ESP_LOGI(TAG, "Attempting to reconnect JSON client...");
            controller->m_jsonClient->setConfiguredPowerName(controller->m_savedPowerMgr);
            esp_err_t err = controller->m_jsonClient->connect(controller->m_savedServerIp.c_str(), controller->m_savedJsonPort);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "JSON client reconnected");
            }
        }

        if (!wtConnected && !controller->m_savedServerIp.empty()) {
            ESP_LOGI(TAG, "Attempting to reconnect WiThrottle client...");
            esp_err_t err = controller->m_wtClient->connect(controller->m_savedServerIp.c_str(), controller->m_savedWtPort);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "WiThrottle client reconnected");
            }
        }

        failedAttempts++;
    }
}
