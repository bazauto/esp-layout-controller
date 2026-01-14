#pragma once

#include <string>
#include <functional>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * @brief WiThrottle Protocol Client for JMRI
 * 
 * Implements the WiThrottle protocol for communication with JMRI
 * and other DCC command stations.
 * 
 * Protocol documentation: https://www.jmriwireless.net/WiThrottle/Protocol
 */
class WiThrottleClient {
public:
    /**
     * @brief Track power states
     */
    enum class PowerState {
        OFF = 0,
        ON = 1,
        UNKNOWN = 2
    };
    
    /**
     * @brief Connection states
     */
    enum class ConnectionState {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        FAILED
    };
    
    /**
     * @brief Callback for power state changes
     * @param track Track identifier ("main" or "prog")
     * @param state New power state
     */
    using PowerStateCallback = std::function<void(const std::string& track, PowerState state)>;
    
    /**
     * @brief Callback for connection state changes
     * @param state New connection state
     */
    using ConnectionStateCallback = std::function<void(ConnectionState state)>;
    
    WiThrottleClient();
    ~WiThrottleClient();
    
    // Delete copy/move
    WiThrottleClient(const WiThrottleClient&) = delete;
    WiThrottleClient& operator=(const WiThrottleClient&) = delete;
    
    /**
     * @brief Initialize WiThrottle client
     * @return ESP_OK on success
     */
    esp_err_t initialize();
    
    /**
     * @brief Connect to JMRI WiThrottle server
     * @param host Server hostname or IP address
     * @param port Server port (default 12090)
     * @return ESP_OK if connection initiated
     */
    esp_err_t connect(const std::string& host, uint16_t port = 12090);
    
    /**
     * @brief Disconnect from server
     */
    void disconnect();
    
    /**
     * @brief Check if connected
     */
    bool isConnected() const { return m_state == ConnectionState::CONNECTED; }
    
    /**
     * @brief Get current connection state
     */
    ConnectionState getState() const { return m_state; }
    
    /**
     * @brief Set track power state
     * @param track Track identifier ("main" or "prog")
     * @param on True to turn power on, false to turn off
     * @return ESP_OK on success
     */
    esp_err_t setTrackPower(const std::string& track, bool on);
    
    /**
     * @brief Get current track power state
     * @param track Track identifier ("main" or "prog")
     * @return Current power state
     */
    PowerState getTrackPower(const std::string& track) const;
    
    /**
     * @brief Set power state change callback
     */
    void setPowerStateCallback(PowerStateCallback callback) { m_powerCallback = callback; }
    
    /**
     * @brief Set connection state change callback
     */
    void setConnectionStateCallback(ConnectionStateCallback callback) { m_connectionCallback = callback; }
    
    /**
     * @brief Send heartbeat (keep-alive)
     * Should be called periodically when connected
     */
    void sendHeartbeat();

private:
    void processMessage(const std::string& message);
    void handlePowerMessage(const std::string& message);
    void setState(ConnectionState newState);
    esp_err_t sendCommand(const std::string& command);
    
    static void receiveTask(void* arg);
    
    ConnectionState m_state;
    int m_socket;
    std::string m_serverHost;
    uint16_t m_serverPort;
    
    PowerState m_mainTrackPower;
    PowerState m_progTrackPower;
    
    PowerStateCallback m_powerCallback;
    ConnectionStateCallback m_connectionCallback;
    
    TaskHandle_t m_receiveTaskHandle;
    bool m_running;
};
