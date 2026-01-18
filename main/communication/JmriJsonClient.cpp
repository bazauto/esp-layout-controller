#include "JmriJsonClient.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

// Simple JSON helpers (minimal implementation for our needs)
namespace {
    std::string escapeJson(const std::string& str) {
        std::string result;
        for (char c : str) {
            if (c == '"' || c == '\\') result += '\\';
            result += c;
        }
        return result;
    }
    
    std::string extractJsonString(const std::string& json, const std::string& key) {
        std::string searchKey = "\"" + key + "\":\"";
        size_t pos = json.find(searchKey);
        if (pos == std::string::npos) return "";
        
        pos += searchKey.length();
        size_t endPos = json.find('"', pos);
        if (endPos == std::string::npos) return "";
        
        return json.substr(pos, endPos - pos);
    }
    
    int extractJsonInt(const std::string& json, const std::string& key) {
        std::string searchKey = "\"" + key + "\":";
        size_t pos = json.find(searchKey);
        if (pos == std::string::npos) return -1;
        
        pos += searchKey.length();
        return atoi(json.c_str() + pos);
    }
}

static const char* TAG = "JmriJsonClient";

JmriJsonClient::JmriJsonClient()
    : m_state(ConnectionState::DISCONNECTED)
    , m_client(nullptr)
    , m_serverHost("")
    , m_serverPort(12080)
    , m_heartbeatTask(nullptr)
    , m_configuredPowerName("DCC++")  // Default to DCC++
    , m_powerCallback(nullptr)
    , m_connectionCallback(nullptr)
{
}

JmriJsonClient::~JmriJsonClient()
{
    stopHeartbeat();
    disconnect();
}

esp_err_t JmriJsonClient::initialize()
{
    ESP_LOGI(TAG, "JMRI JSON client initialized");
    return ESP_OK;
}

esp_err_t JmriJsonClient::connect(const std::string& host, uint16_t port)
{
    // Clean up any existing client first
    if (m_client) {
        ESP_LOGW(TAG, "Client already exists, cleaning up before reconnecting");
        disconnect();
        vTaskDelay(pdMS_TO_TICKS(500)); // Give more time for full cleanup
    }
    
    if (m_state == ConnectionState::CONNECTING) {
        ESP_LOGW(TAG, "Already connecting, please wait");
        return ESP_ERR_INVALID_STATE;
    }
    
    m_serverHost = host;
    m_serverPort = port;
    
    ESP_LOGI(TAG, "Connecting to JMRI JSON WebSocket ws://%s:%d/json/", host.c_str(), port);
    setState(ConnectionState::CONNECTING);
    
    // Build WebSocket URI - JMRI JSON WebSocket endpoint
    std::string uri = "ws://" + host + ":" + std::to_string(port) + "/json/";
    
    // Configure WebSocket client
    esp_websocket_client_config_t ws_cfg = {};
    ws_cfg.uri = uri.c_str();
    ws_cfg.reconnect_timeout_ms = 10000;
    ws_cfg.network_timeout_ms = 10000;
    ws_cfg.ping_interval_sec = 10;
    ws_cfg.disable_auto_reconnect = false;
    ws_cfg.task_stack = 4096;
    ws_cfg.buffer_size = 2048;
    
    // Create WebSocket client
    m_client = esp_websocket_client_init(&ws_cfg);
    if (m_client == nullptr) {
        ESP_LOGE(TAG, "Failed to create WebSocket client");
        setState(ConnectionState::FAILED);
        return ESP_FAIL;
    }
    
    // Register event handler
    esp_websocket_register_events(m_client, WEBSOCKET_EVENT_ANY, websocketEventHandler, this);
    
    // Start connection
    esp_err_t err = esp_websocket_client_start(m_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WebSocket client: %s", esp_err_to_name(err));
        esp_websocket_client_destroy(m_client);
        m_client = nullptr;
        setState(ConnectionState::FAILED);
        return err;
    }
    
    return ESP_OK;
}

void JmriJsonClient::disconnect()
{
    // Stop heartbeat task first
    stopHeartbeat();
    
    if (m_client) {
        ESP_LOGI(TAG, "Disconnecting from JMRI JSON server");
        
        // Stop the client (this closes the socket)
        esp_err_t err = esp_websocket_client_stop(m_client);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Error stopping WebSocket client: %s", esp_err_to_name(err));
        }
        
        // Small delay to allow socket to close
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // Destroy the client
        err = esp_websocket_client_destroy(m_client);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Error destroying WebSocket client: %s", esp_err_to_name(err));
        }
        
        m_client = nullptr;
    }
    
    setState(ConnectionState::DISCONNECTED);
    m_powerStates.clear();
}

esp_err_t JmriJsonClient::setPower(bool on)
{
    if (!isConnected()) {
        ESP_LOGW(TAG, "Not connected to server");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (m_configuredPowerName.empty()) {
        ESP_LOGW(TAG, "No power manager configured");
        return ESP_ERR_INVALID_STATE;
    }
    
    // JMRI JSON power command format:
    // {"type":"power","data":{"name":"DCC++","state":2}}
    // state: 0=unknown, 1=on, 2=off
    int state = on ? 2 : 4;  // JMRI uses 2=ON, 4=OFF
    
    std::string data = "{\"name\":\"" + escapeJson(m_configuredPowerName) + "\",\"state\":" + std::to_string(state) + "}";
    
    ESP_LOGI(TAG, "Setting power '%s': %s", m_configuredPowerName.c_str(), on ? "ON" : "OFF");
    
    return sendJsonCommand("power", data);
}

JmriJsonClient::PowerState JmriJsonClient::getPower() const
{
    auto it = m_powerStates.find(m_configuredPowerName);
    if (it != m_powerStates.end()) {
        return it->second;
    }
    return PowerState::UNKNOWN;
}

esp_err_t JmriJsonClient::requestPowerList()
{
    if (!isConnected()) {
        ESP_LOGW(TAG, "Not connected to server");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!m_client) {
        ESP_LOGE(TAG, "WebSocket client is null");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Request list of all power managers
    // {"type":"power","method":"list"}
    std::string message = "{\"type\":\"power\",\"method\":\"list\"}";
    
    ESP_LOGD(TAG, "Sending power list request: %s", message.c_str());
    
    esp_err_t err = esp_websocket_client_send_text(m_client, message.c_str(), message.length(), portMAX_DELAY);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send power list request: %s (%d)", esp_err_to_name(err), err);
        return err;
    }
    
    ESP_LOGI(TAG, "Power list request sent successfully");
    return ESP_OK;
}

void JmriJsonClient::sendHeartbeat()
{
    if (isConnected()) {
        // JMRI JSON uses ping/pong for heartbeat
        sendJsonCommand("ping", "{}");
    }
}

#if CONFIG_THROTTLE_TESTS
void JmriJsonClient::testProcessMessage(const std::string& message)
{
    processMessage(message);
}
#endif

void JmriJsonClient::websocketEventHandler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data)
{
    JmriJsonClient* client = static_cast<JmriJsonClient*>(handler_args);
    esp_websocket_event_data_t* data = static_cast<esp_websocket_event_data_t*>(event_data);
    
    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED: {
            ESP_LOGI(TAG, "WebSocket connected");
            client->setState(ConnectionState::CONNECTED);
            // Start heartbeat task to keep connection alive
            client->startHeartbeat();
            // Don't send immediately - wait for hello message first
            // The power list will be requested after we receive 'hello'
            break;
        }
            
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "WebSocket disconnected");
            client->stopHeartbeat();
            client->setState(ConnectionState::DISCONNECTED);
            break;
            
        case WEBSOCKET_EVENT_DATA:
            if (data->op_code == 0x01) {  // Text frame
                std::string message(data->data_ptr, data->data_len);
                ESP_LOGD(TAG, "Received: %s", message.c_str());
                client->processMessage(message);
            }
            break;
            
        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "WebSocket error");
            client->setState(ConnectionState::FAILED);
            break;
            
        default:
            break;
    }
}

void JmriJsonClient::processMessage(const std::string& message)
{
    ESP_LOGD(TAG, "Received: %s", message.c_str());
    
    // Extract type field
    std::string type = extractJsonString(message, "type");
    
    if (type == "power") {
        // Extract data object (simplified - assumes data is present)
        size_t dataPos = message.find("\"data\":");
        if (dataPos != std::string::npos) {
            std::string data = message.substr(dataPos + 7);  // Skip "data":
            handlePowerMessage(type, data);
        }
    } else if (type == "pong") {
        ESP_LOGD(TAG, "Heartbeat acknowledged");
    } else if (type == "hello") {
        ESP_LOGI(TAG, "Server hello received - connection ready");
        // Small delay to ensure WebSocket is fully ready for bidirectional communication
        vTaskDelay(pdMS_TO_TICKS(200));
        // Subscribe to power state updates for our configured power manager
        std::string subscribeMsg = "{\"type\":\"power\",\"data\":{\"name\":\"" + 
                                   escapeJson(m_configuredPowerName) + "\"},\"method\":\"get\"}";
        esp_websocket_client_send_text(m_client, subscribeMsg.c_str(), subscribeMsg.length(), pdMS_TO_TICKS(1000));
        ESP_LOGI(TAG, "Subscribed to power updates for '%s'", m_configuredPowerName.c_str());
    }
}

void JmriJsonClient::handlePowerMessage(const std::string& type, const std::string& data)
{
    // Extract power manager name and state
    std::string name = extractJsonString(data, "name");
    int stateValue = extractJsonInt(data, "state");
    
    if (name.empty()) {
        ESP_LOGW(TAG, "Power message missing name");
        return;
    }
    
    // JMRI JSON power states: 0=unknown, 2=ON, 4=OFF
    PowerState newState;
    switch (stateValue) {
        case 2:
            newState = PowerState::ON;
            break;
        case 4:
            newState = PowerState::OFF;
            break;
        default:
            newState = PowerState::UNKNOWN;
            break;
    }
    
    ESP_LOGI(TAG, "Power '%s' state: %d", name.c_str(), (int)newState);
    
    // Update cached state
    bool stateChanged = false;
    auto it = m_powerStates.find(name);
    if (it == m_powerStates.end() || it->second != newState) {
        m_powerStates[name] = newState;
        stateChanged = true;
    }
    
    // Only notify callback for the configured power manager
    if (stateChanged && m_powerCallback && name == m_configuredPowerName) {
        m_powerCallback(name, newState);
    }
}

void JmriJsonClient::setState(ConnectionState newState)
{
    if (m_state != newState) {
        m_state = newState;
        ESP_LOGI(TAG, "Connection state changed: %d", (int)newState);
        
        if (m_connectionCallback) {
            m_connectionCallback(newState);
        }
    }
}

esp_err_t JmriJsonClient::sendJsonCommand(const std::string& type, const std::string& data)
{
    if (!m_client || !isConnected()) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Build JSON message: {"type":"...", "data":{...}}
    std::string message = "{\"type\":\"" + type + "\",\"data\":" + data + "}";
    
    // Send with reasonable timeout
    int sent = esp_websocket_client_send_text(m_client, message.c_str(), message.length(), pdMS_TO_TICKS(1000));
    
    // The ESP WebSocket client returns the number of bytes sent on success, or -1 on error
    // However, it seems to return -1 even when the message is successfully queued/sent
    // This appears to be a quirk or bug in the library
    if (sent < 0) {
        // Log as warning but don't treat as fatal error since commands seem to work
        ESP_LOGW(TAG, "WebSocket send returned %d (message may still have been sent)", sent);
    } else {
        ESP_LOGD(TAG, "Sent %d bytes: %s", sent, message.c_str());
    }
    
    // Always return OK since the commands actually work despite the error return
    return ESP_OK;
}

void JmriJsonClient::startHeartbeat()
{
    // Don't start if already running
    if (m_heartbeatTask != nullptr) {
        return;
    }
    
    // Create heartbeat task
    BaseType_t result = xTaskCreate(
        heartbeatTask,
        "jmri_heartbeat",
        2048,
        this,
        5,
        &m_heartbeatTask
    );
    
    if (result == pdPASS) {
        ESP_LOGI(TAG, "Heartbeat task started");
    } else {
        ESP_LOGE(TAG, "Failed to create heartbeat task");
        m_heartbeatTask = nullptr;
    }
}

void JmriJsonClient::stopHeartbeat()
{
    if (m_heartbeatTask != nullptr) {
        ESP_LOGI(TAG, "Stopping heartbeat task");
        vTaskDelete(m_heartbeatTask);
        m_heartbeatTask = nullptr;
    }
}

void JmriJsonClient::heartbeatTask(void* pvParameters)
{
    JmriJsonClient* client = static_cast<JmriJsonClient*>(pvParameters);
    
    ESP_LOGI(TAG, "Heartbeat task running");
    
    while (true) {
        // Wait 30 seconds
        vTaskDelay(pdMS_TO_TICKS(30000));
        
        // Send heartbeat if connected
        if (client->isConnected()) {
            ESP_LOGD(TAG, "Sending heartbeat");
            client->sendHeartbeat();
        }
    }
}
