#include "WiFiController.h"
#include "../communication/WiFiManager.h"
#include "esp_log.h"

static const char* TAG = "WiFiController";

WiFiController::WiFiController()
    : m_wifiManager(nullptr)
{
}

WiFiController::~WiFiController() = default;

void WiFiController::initialize()
{
    if (!m_wifiManager) {
        m_wifiManager = std::make_unique<WiFiManager>();
        m_wifiManager->initialize();
    }
}

void WiFiController::autoConnect()
{
    initialize();

    ESP_LOGI(TAG, "Attempting auto-connect with stored credentials");
    esp_err_t ret = m_wifiManager->connect();

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Auto-connect initiated");
    } else if (ret == ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG, "No stored credentials found - use settings to configure WiFi");
    } else {
        ESP_LOGW(TAG, "Auto-connect failed: %s", esp_err_to_name(ret));
    }
}

bool WiFiController::isConnected() const
{
    return m_wifiManager ? m_wifiManager->isConnected() : false;
}

WiFiManager* WiFiController::getManager() const
{
    return m_wifiManager.get();
}
