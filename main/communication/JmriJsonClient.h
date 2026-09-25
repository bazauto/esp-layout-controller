#pragma once

#include <atomic>
#include <string>
#include <map>
#include <functional>
#include <utility>
#include "esp_err.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "CallbackSlot.h"

/**
 * @brief JMRI JSON Protocol Client
 * 
 * Implements JMRI's JSON protocol for communication with JMRI server.
 * Uses WebSocket connection to JMRI JSON server.
 * Provides more detailed control than WiThrottle, especially for power districts.
 * 
 * Protocol documentation: https://www.jmri.org/help/en/html/web/JsonServlet.shtml
 */
class JmriJsonClient {
public:
    /**
     * @brief Power states for tracks/districts
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
     * @param powerName Power district name (e.g., "main", "prog", "LCC_0")
     * @param state New power state
     */
    using PowerStateCallback = std::function<void(const std::string& powerName, PowerState state)>;
    
    /**
     * @brief Callback for connection state changes
     * @param state New connection state
     */
    using ConnectionStateCallback = std::function<void(ConnectionState state)>;
    
    JmriJsonClient();
    ~JmriJsonClient();
    
    // Delete copy/move
    JmriJsonClient(const JmriJsonClient&) = delete;
    JmriJsonClient& operator=(const JmriJsonClient&) = delete;
    
    /**
     * @brief Initialize JSON client
     * @return ESP_OK on success
     */
    esp_err_t initialize();
    
    /**
     * @brief Connect to JMRI JSON server
     * @param host Server hostname or IP address
     * @param port Server port (default 12080 for JSON WebSocket)
     * @return ESP_OK if connection initiated
     */
    esp_err_t connect(const std::string& host, uint16_t port = 12080);
    
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
     * @brief Set the configured power manager name to control
     * @param powerName Name of the power manager (e.g., "DCC++", "main")
     */
    void setConfiguredPowerName(const std::string& powerName);
    
    /**
     * @brief Get the configured power manager name
     */
    std::string getConfiguredPowerName() const;
    
    /**
     * @brief Set power state for the configured power district
     * @param on True to turn power on, false to turn off
     * @return ESP_OK on success
     */
    esp_err_t setPower(bool on);
    
    /**
     * @brief Get current power state for the configured district
     * @return Current power state
     */
    PowerState getPower() const;
    
    /**
     * @brief Set power state change callback
     * Only called for the configured power manager
     */
    void setPowerStateCallback(PowerStateCallback callback) { m_powerCallback.set(std::move(callback)); }
    
    /**
     * @brief Set connection state change callback
     */
    void setConnectionStateCallback(ConnectionStateCallback callback) { m_connectionCallback.set(std::move(callback)); }
    
    /**
     * @brief Send heartbeat (keep-alive)
     * Should be called periodically when connected
     */
    void sendHeartbeat();
    
    /**
     * @brief Start heartbeat task
     * Automatically sends heartbeats every 30 seconds
     */
    void startHeartbeat();
    
    /**
     * @brief Stop heartbeat task, waiting for it to exit.
     *
     * Blocks for up to about a second when the task is mid-send. Called from
     * the WebSocket task too, where that is the send timing out on the lock
     * the WebSocket task itself holds while dispatching the event.
     */
    void stopHeartbeat();

#if CONFIG_THROTTLE_TESTS
    /**
     * @brief Test-only hook to process a raw JSON message
     */
    void testProcessMessage(const std::string& message);
#endif

private:
    void processMessage(const std::string& message);
    void handlePowerMessage(const std::string& type, const std::string& data);
    void setState(ConnectionState newState);
    esp_err_t sendJsonCommand(const std::string& type, const std::string& data);
    
    static void websocketEventHandler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);
    static void heartbeatTask(void* pvParameters);
    
    ConnectionState m_state;
    esp_websocket_client_handle_t m_client;
    std::string m_serverHost;
    uint16_t m_serverPort;
    
    // Heartbeat task. Started and stopped from both the WebSocket task and
    // connect()/disconnect(), so the handle is guarded by m_heartbeatMutex.
    TaskHandle_t m_heartbeatTask;
    SemaphoreHandle_t m_heartbeatMutex;
    SemaphoreHandle_t m_heartbeatExitSemaphore;
    std::atomic<bool> m_heartbeatRunning;
    
    // Configured power manager name to control and monitor
    /** Set on the LVGL and jmri_conn tasks, read on the WebSocket task:
     * guarded by m_powerMutex (F-42). */
    std::string m_configuredPowerName;
    
    // Power states for known districts
    std::map<std::string, PowerState> m_powerStates;
    SemaphoreHandle_t m_powerMutex;
    
    // Set on the LVGL or main task, invoked on the WebSocket task (F-30).
    CallbackSlot<void(const std::string&, PowerState)> m_powerCallback;
    CallbackSlot<void(ConnectionState)> m_connectionCallback;
};
