#pragma once

#include <string>
#include <functional>
#include "esp_wifi.h"
#include "esp_event.h"

/**
 * @brief Manages WiFi connection with configuration support.
 * 
 * Features:
 * - Connect to WiFi with stored credentials
 * - Configuration via UI
 * - Automatic reconnection
 * - Connection status callbacks
 * - NVS storage for credentials
 */
class WiFiManager {
public:
    /**
     * @brief WiFi connection state
     */
    enum class State {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        FAILED
    };

    /**
     * @brief Callback for connection state changes
     * @param state New connection state
     * @param ip IP address (empty if not connected)
     */
    using StateCallback = std::function<void(State state, const std::string& ip)>;

    WiFiManager();
    ~WiFiManager();

    // Delete copy/move
    WiFiManager(const WiFiManager&) = delete;
    WiFiManager& operator=(const WiFiManager&) = delete;

    /**
     * @brief Initialize WiFi subsystem
     * @return ESP_OK on success
     */
    esp_err_t initialize();

    /**
     * @brief Load credentials from NVS and connect
     * @return ESP_OK if connection started successfully
     */
    esp_err_t connect();

    /**
     * @brief Connect with specific credentials (and save to NVS)
     * @param ssid WiFi SSID
     * @param password WiFi password
     * @return ESP_OK if connection started successfully
     */
    esp_err_t connect(const std::string& ssid, const std::string& password);

    /**
     * @brief Disconnect from WiFi
     * Note: Keeps stored credentials for auto-reconnect on reboot
     */
    void disconnect();
    
    /**
     * @brief Disconnect and forget stored credentials
     * Clears saved SSID and password from NVS
     */
    void forgetNetwork();

    /**
     * @brief Check if WiFi is connected
     */
    bool isConnected() const { return m_state == State::CONNECTED; }

    /**
     * @brief Get current connection state
     */
    State getState() const { return m_state; }

    /**
     * @brief Get current IP address
     * @return IP address string, or empty if not connected
     */
    std::string getIpAddress() const;

    /**
     * @brief Get stored SSID from NVS
     * @return SSID string, or empty if none stored
     */
    std::string getStoredSsid() const;

    /**
     * @brief Check if credentials are stored
     */
    bool hasStoredCredentials() const;

    /**
     * @brief Clear stored credentials from NVS
     */
    void clearStoredCredentials();

    /**
     * @brief Set callback for state changes
     * @param callback Function to call on state changes
     */
    void setStateCallback(StateCallback callback);

    /**
     * @brief Start WiFi scan for available networks
     * @return ESP_OK if scan started successfully
     */
    esp_err_t startScan();

    /**
     * @brief Get scan results
     * @param maxResults Maximum number of results to return
     * @return Vector of SSID strings
     */
    std::vector<std::string> getScanResults(uint16_t maxResults = 20);

private:
    static constexpr const char* TAG = "WiFiManager";
    static constexpr const char* NVS_NAMESPACE = "wifi";
    static constexpr const char* NVS_SSID_KEY = "ssid";
    static constexpr const char* NVS_PASSWORD_KEY = "password";
    static constexpr int MAX_RETRY_ATTEMPTS = 5;

    State m_state;
    StateCallback m_stateCallback;
    int m_retryCount;
    bool m_initialized;

    // Event handlers
    static void eventHandler(void* arg, esp_event_base_t event_base,
                           int32_t event_id, void* event_data);
    void handleWiFiEvent(esp_event_base_t event_base, int32_t event_id, void* event_data);

    // NVS helpers
    esp_err_t loadCredentials(std::string& ssid, std::string& password) const;
    esp_err_t saveCredentials(const std::string& ssid, const std::string& password);

    // State management
    void setState(State newState);
};
