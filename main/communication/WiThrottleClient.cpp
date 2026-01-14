#include "WiThrottleClient.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <cstring>

static const char* TAG = "WiThrottleClient";

// WiThrottle protocol commands
static const char* CMD_HEARTBEAT = "*";
static const char* CMD_TRACK_POWER = "PPA";  // Power command

WiThrottleClient::WiThrottleClient()
    : m_state(ConnectionState::DISCONNECTED)
    , m_socket(-1)
    , m_serverHost("")
    , m_serverPort(12090)
    , m_mainTrackPower(PowerState::UNKNOWN)
    , m_progTrackPower(PowerState::UNKNOWN)
    , m_powerCallback(nullptr)
    , m_connectionCallback(nullptr)
    , m_receiveTaskHandle(nullptr)
    , m_running(false)
{
}

WiThrottleClient::~WiThrottleClient()
{
    disconnect();
}

esp_err_t WiThrottleClient::initialize()
{
    ESP_LOGI(TAG, "WiThrottle client initialized");
    return ESP_OK;
}

esp_err_t WiThrottleClient::connect(const std::string& host, uint16_t port)
{
    if (m_state == ConnectionState::CONNECTED || m_state == ConnectionState::CONNECTING) {
        ESP_LOGW(TAG, "Already connected or connecting");
        return ESP_ERR_INVALID_STATE;
    }
    
    m_serverHost = host;
    m_serverPort = port;
    
    ESP_LOGI(TAG, "Connecting to WiThrottle server %s:%d", host.c_str(), port);
    setState(ConnectionState::CONNECTING);
    
    // Create socket
    m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_socket < 0) {
        ESP_LOGE(TAG, "Failed to create socket: %d", errno);
        setState(ConnectionState::FAILED);
        return ESP_FAIL;
    }
    
    // Set socket timeout
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    // Resolve hostname
    struct hostent* server = gethostbyname(host.c_str());
    if (server == nullptr) {
        ESP_LOGE(TAG, "Failed to resolve hostname: %s", host.c_str());
        close(m_socket);
        m_socket = -1;
        setState(ConnectionState::FAILED);
        return ESP_FAIL;
    }
    
    // Connect
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    
    int ret = ::connect(m_socket, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to connect: %d", errno);
        close(m_socket);
        m_socket = -1;
        setState(ConnectionState::FAILED);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Connected to WiThrottle server");
    setState(ConnectionState::CONNECTED);
    
    // Send device name
    sendCommand(std::string("N") + "ESP32-Layout-Controller");
    
    // Start receive task
    m_running = true;
    xTaskCreate(receiveTask, "withrottle_rx", 4096, this, 5, &m_receiveTaskHandle);
    
    return ESP_OK;
}

void WiThrottleClient::disconnect()
{
    if (m_socket >= 0) {
        ESP_LOGI(TAG, "Disconnecting from WiThrottle server");
        m_running = false;
        
        // Wait for receive task to finish
        if (m_receiveTaskHandle) {
            vTaskDelay(pdMS_TO_TICKS(100));
            if (eTaskGetState(m_receiveTaskHandle) != eDeleted) {
                vTaskDelete(m_receiveTaskHandle);
            }
            m_receiveTaskHandle = nullptr;
        }
        
        close(m_socket);
        m_socket = -1;
    }
    
    setState(ConnectionState::DISCONNECTED);
    m_mainTrackPower = PowerState::UNKNOWN;
    m_progTrackPower = PowerState::UNKNOWN;
}

esp_err_t WiThrottleClient::setTrackPower(const std::string& track, bool on)
{
    if (!isConnected()) {
        ESP_LOGW(TAG, "Not connected to server");
        return ESP_ERR_INVALID_STATE;
    }
    
    // WiThrottle protocol: PPA<X> where X is power state
    // PPA0 = power off, PPA1 = power on for all tracks
    // For individual tracks we use the same command (JMRI handles both)
    std::string command = std::string(CMD_TRACK_POWER) + (on ? "1" : "0");
    
    ESP_LOGI(TAG, "Setting %s track power: %s", track.c_str(), on ? "ON" : "OFF");
    
    return sendCommand(command);
}

WiThrottleClient::PowerState WiThrottleClient::getTrackPower(const std::string& track) const
{
    if (track == "main") {
        return m_mainTrackPower;
    } else if (track == "prog") {
        return m_progTrackPower;
    }
    return PowerState::UNKNOWN;
}

void WiThrottleClient::sendHeartbeat()
{
    if (isConnected()) {
        sendCommand(CMD_HEARTBEAT);
    }
}

void WiThrottleClient::receiveTask(void* arg)
{
    WiThrottleClient* client = static_cast<WiThrottleClient*>(arg);
    char buffer[512];
    std::string messageBuffer;
    
    while (client->m_running) {
        int len = recv(client->m_socket, buffer, sizeof(buffer) - 1, 0);
        
        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Timeout, continue
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            ESP_LOGE(TAG, "Receive error: %d", errno);
            break;
        } else if (len == 0) {
            ESP_LOGW(TAG, "Connection closed by server");
            break;
        }
        
        buffer[len] = '\0';
        messageBuffer += buffer;
        
        // Process complete messages (separated by newline)
        size_t pos;
        while ((pos = messageBuffer.find('\n')) != std::string::npos) {
            std::string message = messageBuffer.substr(0, pos);
            messageBuffer.erase(0, pos + 1);
            
            if (!message.empty()) {
                client->processMessage(message);
            }
        }
    }
    
    // Connection lost
    if (client->m_state == ConnectionState::CONNECTED) {
        ESP_LOGW(TAG, "Connection lost");
        client->setState(ConnectionState::DISCONNECTED);
    }
    
    vTaskDelete(nullptr);
}

void WiThrottleClient::processMessage(const std::string& message)
{
    ESP_LOGD(TAG, "Received: %s", message.c_str());
    
    if (message.empty()) {
        return;
    }
    
    // Parse message type
    char msgType = message[0];
    
    switch (msgType) {
        case 'P':  // Power message
            handlePowerMessage(message);
            break;
            
        case 'V':  // Version
            ESP_LOGI(TAG, "Server version: %s", message.substr(1).c_str());
            break;
            
        case 'R':  // Roster
            ESP_LOGD(TAG, "Roster update received");
            break;
            
        case 'H':  // Heartbeat response
            ESP_LOGD(TAG, "Heartbeat acknowledged");
            break;
            
        case '*':  // Heartbeat request from server
            sendHeartbeat();
            break;
            
        default:
            ESP_LOGD(TAG, "Unhandled message type: %c", msgType);
            break;
    }
}

void WiThrottleClient::handlePowerMessage(const std::string& message)
{
    // Power message format: PPA<state>
    // PPA0 = power off
    // PPA1 = power on
    // PPA2 = unknown
    
    if (message.length() < 4 || message.substr(0, 3) != "PPA") {
        return;
    }
    
    char state = message[3];
    PowerState newState;
    
    switch (state) {
        case '0':
            newState = PowerState::OFF;
            break;
        case '1':
            newState = PowerState::ON;
            break;
        default:
            newState = PowerState::UNKNOWN;
            break;
    }
    
    ESP_LOGI(TAG, "Track power state changed: %d", (int)newState);
    
    // Update both tracks (JMRI sends global power state)
    bool mainChanged = (m_mainTrackPower != newState);
    bool progChanged = (m_progTrackPower != newState);
    
    m_mainTrackPower = newState;
    m_progTrackPower = newState;
    
    // Notify callbacks
    if (m_powerCallback) {
        if (mainChanged) {
            m_powerCallback("main", newState);
        }
        if (progChanged) {
            m_powerCallback("prog", newState);
        }
    }
}

void WiThrottleClient::setState(ConnectionState newState)
{
    if (m_state != newState) {
        m_state = newState;
        ESP_LOGI(TAG, "Connection state changed: %d", (int)newState);
        
        if (m_connectionCallback) {
            m_connectionCallback(newState);
        }
    }
}

esp_err_t WiThrottleClient::sendCommand(const std::string& command)
{
    if (m_socket < 0) {
        return ESP_ERR_INVALID_STATE;
    }
    
    std::string fullCommand = command + "\n";
    int len = send(m_socket, fullCommand.c_str(), fullCommand.length(), 0);
    
    if (len < 0) {
        ESP_LOGE(TAG, "Failed to send command: %d", errno);
        return ESP_FAIL;
    }
    
    ESP_LOGD(TAG, "Sent: %s", command.c_str());
    return ESP_OK;
}
