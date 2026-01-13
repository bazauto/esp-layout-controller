#include "WiFiManager.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <cstring>

WiFiManager::WiFiManager()
    : m_state(State::DISCONNECTED)
    , m_stateCallback(nullptr)
    , m_retryCount(0)
    , m_initialized(false)
{
}

WiFiManager::~WiFiManager()
{
    if (m_initialized) {
        esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &WiFiManager::eventHandler);
        esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &WiFiManager::eventHandler);
        esp_wifi_stop();
        esp_wifi_deinit();
    }
}

esp_err_t WiFiManager::initialize()
{
    if (m_initialized) {
        return ESP_OK;
    }

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, 
                                               &WiFiManager::eventHandler, this));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, 
                                               &WiFiManager::eventHandler, this));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    
    // Start WiFi (required for scanning even without connection)
    ESP_ERROR_CHECK(esp_wifi_start());

    m_initialized = true;
    ESP_LOGI(TAG, "WiFi Manager initialized");

    return ESP_OK;
}

esp_err_t WiFiManager::connect()
{
    std::string ssid, password;
    esp_err_t ret = loadCredentials(ssid, password);
    
    if (ret != ESP_OK || ssid.empty()) {
        ESP_LOGW(TAG, "No stored credentials found");
        return ESP_ERR_NOT_FOUND;
    }

    return connect(ssid, password);
}

esp_err_t WiFiManager::connect(const std::string& ssid, const std::string& password)
{
    if (!m_initialized) {
        ESP_LOGE(TAG, "WiFi Manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Save credentials for auto-reconnect
    saveCredentials(ssid, password);

    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, ssid.c_str(), sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password.c_str(), sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    // WiFi is already started in initialize(), just connect

    setState(State::CONNECTING);
    m_retryCount = 0;

    ESP_LOGI(TAG, "Connecting to SSID: %s", ssid.c_str());

    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect failed: %d", ret);
        setState(State::FAILED);
    }

    return ret;
}

void WiFiManager::disconnect()
{
    if (m_state != State::DISCONNECTED) {
        esp_wifi_disconnect();
        setState(State::DISCONNECTED);
        ESP_LOGI(TAG, "Disconnected from WiFi (credentials retained)");
    }
}

void WiFiManager::forgetNetwork()
{
    // Disconnect first
    disconnect();
    
    // Clear stored credentials
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    
    if (ret == ESP_OK) {
        nvs_erase_key(handle, NVS_SSID_KEY);
        nvs_erase_key(handle, NVS_PASSWORD_KEY);
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGI(TAG, "Network forgotten - credentials cleared from NVS");
    } else {
        ESP_LOGW(TAG, "Failed to open NVS to forget network");
    }
}

std::string WiFiManager::getIpAddress() const
{
    if (m_state != State::CONNECTED) {
        return "";
    }

    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == nullptr) {
        return "";
    }

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) {
        return "";
    }

    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
    return std::string(ip_str);
}

std::string WiFiManager::getStoredSsid() const
{
    std::string ssid, password;
    if (loadCredentials(ssid, password) == ESP_OK) {
        return ssid;
    }
    return "";
}

bool WiFiManager::hasStoredCredentials() const
{
    std::string ssid, password;
    return loadCredentials(ssid, password) == ESP_OK && !ssid.empty();
}

void WiFiManager::clearStoredCredentials()
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret == ESP_OK) {
        nvs_erase_key(handle, NVS_SSID_KEY);
        nvs_erase_key(handle, NVS_PASSWORD_KEY);
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGI(TAG, "Credentials cleared");
    }
}

void WiFiManager::setStateCallback(StateCallback callback)
{
    m_stateCallback = callback;
}

esp_err_t WiFiManager::startScan()
{
    wifi_scan_config_t scan_config = {
        .ssid = nullptr,
        .bssid = nullptr,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {
            .active = {
                .min = 0,
                .max = 0
            }
        },
        .home_chan_dwell_time = 0,
        .channel_bitmap = {},
        .coex_background_scan = false
    };

    return esp_wifi_scan_start(&scan_config, false);
}

std::vector<std::string> WiFiManager::getScanResults(uint16_t maxResults)
{
    std::vector<std::string> results;
    
    uint16_t count = 0;
    esp_wifi_scan_get_ap_num(&count);
    
    if (count == 0) {
        return results;
    }

    if (count > maxResults) {
        count = maxResults;
    }

    wifi_ap_record_t* ap_records = new wifi_ap_record_t[count];
    if (esp_wifi_scan_get_ap_records(&count, ap_records) == ESP_OK) {
        for (uint16_t i = 0; i < count; i++) {
            results.push_back(std::string((char*)ap_records[i].ssid));
        }
    }

    delete[] ap_records;
    return results;
}

void WiFiManager::eventHandler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    WiFiManager* manager = static_cast<WiFiManager*>(arg);
    manager->handleWiFiEvent(event_base, event_id, event_data);
}

void WiFiManager::handleWiFiEvent(esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            ESP_LOGI(TAG, "WiFi started");
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            if (m_state == State::CONNECTING || m_state == State::CONNECTED) {
                if (m_retryCount < MAX_RETRY_ATTEMPTS) {
                    esp_wifi_connect();
                    m_retryCount++;
                    ESP_LOGI(TAG, "Retry connecting to WiFi (%d/%d)", m_retryCount, MAX_RETRY_ATTEMPTS);
                    setState(State::CONNECTING);
                } else {
                    ESP_LOGE(TAG, "Failed to connect to WiFi after %d attempts", MAX_RETRY_ATTEMPTS);
                    setState(State::FAILED);
                }
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        m_retryCount = 0;
        setState(State::CONNECTED);
    }
}

esp_err_t WiFiManager::loadCredentials(std::string& ssid, std::string& password) const
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    
    if (ret != ESP_OK) {
        return ret;
    }

    // Read SSID
    size_t ssid_len = 0;
    ret = nvs_get_str(handle, NVS_SSID_KEY, nullptr, &ssid_len);
    if (ret == ESP_OK && ssid_len > 0) {
        char* ssid_buf = new char[ssid_len];
        nvs_get_str(handle, NVS_SSID_KEY, ssid_buf, &ssid_len);
        ssid = std::string(ssid_buf);
        delete[] ssid_buf;
    }

    // Read password
    size_t pwd_len = 0;
    ret = nvs_get_str(handle, NVS_PASSWORD_KEY, nullptr, &pwd_len);
    if (ret == ESP_OK && pwd_len > 0) {
        char* pwd_buf = new char[pwd_len];
        nvs_get_str(handle, NVS_PASSWORD_KEY, pwd_buf, &pwd_len);
        password = std::string(pwd_buf);
        delete[] pwd_buf;
    }

    nvs_close(handle);
    return ESP_OK;
}

esp_err_t WiFiManager::saveCredentials(const std::string& ssid, const std::string& password)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    
    if (ret != ESP_OK) {
        return ret;
    }

    nvs_set_str(handle, NVS_SSID_KEY, ssid.c_str());
    nvs_set_str(handle, NVS_PASSWORD_KEY, password.c_str());
    nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "Credentials saved to NVS");
    return ESP_OK;
}

void WiFiManager::setState(State newState)
{
    if (m_state != newState) {
        m_state = newState;
        
        if (m_stateCallback) {
            std::string ip = (newState == State::CONNECTED) ? getIpAddress() : "";
            m_stateCallback(newState, ip);
        }
    }
}
