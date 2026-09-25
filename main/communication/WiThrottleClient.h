#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <utility>
#include "esp_err.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "CallbackSlot.h"

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
     * @brief Locomotive entry from roster
     */
    struct Locomotive {
        int address;           // DCC address
        std::string name;      // Loco name/number
        char addressType;      // 'S' = short, 'L' = long
        
        Locomotive() : address(0), addressType('S') {}
        Locomotive(int addr, const std::string& n, char type) 
            : address(addr), name(n), addressType(type) {}
    };
    
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
    
    /**
     * @brief Callback for roster updates
     * @param roster Vector of locomotives
     */
    using RosterCallback = std::function<void(const std::vector<Locomotive>& roster)>;
    
    /**
     * @brief Callback for web server port discovery
     * @param port JSON web server port number
     */
    using WebPortCallback = std::function<void(uint16_t port)>;
    
    /**
     * @brief Throttle state change notification
     */
    struct ThrottleUpdate {
        char throttleId;           // Throttle identifier (0-3)
        int address;               // Loco DCC address
        int speed;                 // Speed (0-126), -1 if not in message
        int direction;             // Direction (0=reverse, 1=forward), -1 if not in message
        int function;              // Function number (0-28), -1 if not in message
        bool functionState;        // Function state (only valid if function >= 0)
    };
    
    /**
     * @brief Callback for throttle state changes
     * Called when throttle speed/direction/function changes are received
     * @param update Throttle update information
     */
    using ThrottleStateCallback = std::function<void(const ThrottleUpdate& update)>;

    /**
     * @brief Callback for function label updates
     * @param throttleId Throttle identifier (0-3)
     * @param labels Function labels (index = function number)
     */
    using FunctionLabelsCallback = std::function<void(char throttleId, const std::vector<std::string>& labels)>;
    
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
     * @brief Acquire a locomotive for throttle control
     * @param throttleId Throttle identifier (0-3 for our 4 throttles)
     * @param address Locomotive DCC address
     * @param isLongAddress True for long address (L), false for short (S)
     * @return ESP_OK on success
     */
    esp_err_t acquireLocomotive(char throttleId, int address, bool isLongAddress);
    
    /**
     * @brief Release a locomotive from throttle control
     * @param throttleId Throttle identifier (0-3)
     * @return ESP_OK on success
     */
    esp_err_t releaseLocomotive(char throttleId);
    
    /**
     * @brief Set locomotive speed
     * @param throttleId Throttle identifier (0-3)
     * @param speed Speed value (0-126, where 0=stop, 1=emergency stop)
     * @return ESP_OK on success
     */
    esp_err_t setSpeed(char throttleId, int speed);
    
    /**
     * @brief Set locomotive direction
     * @param throttleId Throttle identifier (0-3)
     * @param forward True for forward, false for reverse
     * @return ESP_OK on success
     */
    esp_err_t setDirection(char throttleId, bool forward);
    
    /**
     * @brief Set locomotive function state
     * @param throttleId Throttle identifier (0-3)
     * @param function Function number (0-28)
     * @param state True to activate, false to deactivate
     * @return ESP_OK on success
     */
    esp_err_t setFunction(char throttleId, int function, bool state);

    /**
     * @brief Emergency-stop every loco this client holds (`M<id>A<addr><;>X`).
     *
     * WiThrottle has no layout-wide stop, so this is the strongest one it has.
     * @return ESP_OK when every held loco was sent the stop (or none is held).
     */
    esp_err_t emergencyStopAll();
    
    /**
     * @brief Query locomotive speed
     * @param throttleId Throttle identifier (0-3)
     * @return ESP_OK on success
     */
    esp_err_t querySpeed(char throttleId);
    
    /**
     * @brief Query locomotive direction
     * @param throttleId Throttle identifier (0-3)
     * @return ESP_OK on success
     */
    esp_err_t queryDirection(char throttleId);
    
    /**
     * @brief Set power state change callback
     */
    void setPowerStateCallback(PowerStateCallback callback) { m_powerCallback.set(std::move(callback)); }
    
    /**
     * @brief Set connection state change callback
     */
    void setConnectionStateCallback(ConnectionStateCallback callback) { m_connectionCallback.set(std::move(callback)); }
    
    /**
     * @brief Set roster update callback
     */
    void setRosterCallback(RosterCallback callback) { m_rosterCallback.set(std::move(callback)); }
    
    /**
     * @brief Set web port discovery callback
     */
    void setWebPortCallback(WebPortCallback callback) { m_webPortCallback.set(std::move(callback)); }
    
    /**
     * @brief Set throttle state change callback
     */
    void setThrottleStateCallback(ThrottleStateCallback callback) { m_throttleCallback.set(std::move(callback)); }

    /**
     * @brief Set function labels callback
     */
    void setFunctionLabelsCallback(FunctionLabelsCallback callback) { m_functionLabelsCallback.set(std::move(callback)); }

    /**
    * @brief Get a copy of the current roster (thread-safe)
    */
    std::vector<Locomotive> getRosterSnapshot() const;

    /**
    * @brief Get number of roster entries (thread-safe)
    */
    size_t getRosterSize() const;

    /**
    * @brief Get a roster entry by index (thread-safe)
    * @return true if index valid and entry copied
    */
    bool getRosterEntry(int index, Locomotive& outEntry) const;

#if CONFIG_THROTTLE_TESTS
    /**
     * @brief Test-only hook to process a raw protocol message
     */
    void testProcessMessage(const std::string& message);
#endif
    
    /**
     * @brief Get discovered web server port (0 if not yet discovered)
     */
    uint16_t getWebPort() const { return m_webPort; }
    
    /**
     * @brief Send heartbeat (keep-alive)
     * Sent by the receive task at half the server's announced interval.
     */
    void sendHeartbeat();

    /**
     * @brief How often this session sends a heartbeat, or 0 when off.
     *
     * Half the interval the server announced with "*<seconds>". Non-zero means
     * heartbeat monitoring was switched on with "*+", so JMRI e-stops this
     * device's locos if the heartbeats stop (F-23).
     */
    uint32_t getHeartbeatPeriodMs() const { return m_heartbeatPeriodMs; }

private:
    const std::vector<Locomotive>& getRoster() const { return m_roster; }

    bool lockState(TickType_t timeout) const;
    void unlockState() const;

    void processMessage(const std::string& message);
    void handlePowerMessage(const std::string& message);
    void handleRosterMessage(const std::string& message);
    void handleThrottleMessage(const std::string& message);
    void handleHeartbeatAnnouncement(const std::string& message);
    void setState(ConnectionState newState);
    esp_err_t sendCommand(const std::string& command);

    /**
     * @brief Stop the receive task and close the socket, if there is one.
     *
     * Deliberately leaves the acquisition record alone: that mirrors what the
     * UI shows as allocated, and the next session re-acquires from it.
     */
    void teardownSession();

    /** Re-acquires every loco the record holds, on a fresh session (F-22). */
    void reacquireLocomotives();

    /** Called by the receive task on every wake; sends "*" when one is due. */
    void serviceHeartbeat();

    /** Stable per-device id for the "HU" line: the WiFi station MAC. */
    static std::string deviceId();
    
    static void receiveTask(void* arg);
    
    // Throttle state tracking
    struct ThrottleState {
        bool acquired;
        int address;
        char addressType;  // 'S' or 'L'
        
        ThrottleState() : acquired(false), address(0), addressType('S') {}
    };
    std::map<char, ThrottleState> m_throttleStates;  // Key is throttleId
    
    ConnectionState m_state;
    int m_socket;
    std::string m_serverHost;
    uint16_t m_serverPort;
    
    PowerState m_mainTrackPower;
    PowerState m_progTrackPower;
    
    std::vector<Locomotive> m_roster;
    uint16_t m_webPort;
    
    // Set on the LVGL or main task, invoked on the receive task (F-30).
    CallbackSlot<void(const std::string&, PowerState)> m_powerCallback;
    CallbackSlot<void(ConnectionState)> m_connectionCallback;
    CallbackSlot<void(const std::vector<Locomotive>&)> m_rosterCallback;
    CallbackSlot<void(uint16_t)> m_webPortCallback;
    CallbackSlot<void(char, const std::vector<std::string>&)> m_functionLabelsCallback;
    CallbackSlot<void(const ThrottleUpdate&)> m_throttleCallback;

    mutable SemaphoreHandle_t m_stateMutex;
    SemaphoreHandle_t m_sendMutex;
    SemaphoreHandle_t m_taskExitSemaphore;
    
    TaskHandle_t m_receiveTaskHandle;
    bool m_running;

    // Touched only by the receive task once it is running, and reset in
    // connect() before it starts.
    uint32_t m_heartbeatPeriodMs;
    int64_t m_lastHeartbeatUs;
};
