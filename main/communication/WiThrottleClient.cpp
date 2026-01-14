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
    , m_webPort(0)
    , m_powerCallback(nullptr)
    , m_connectionCallback(nullptr)
    , m_rosterCallback(nullptr)
    , m_webPortCallback(nullptr)
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
    
    // Send device name (identifies us to JMRI)
    ESP_LOGI(TAG, "Sending device identification...");
    sendCommand(std::string("N") + "ESP32-Layout-Controller");
    
    // Send hardware identifier
    sendCommand(std::string("H") + "ESP32-S3");
    
    ESP_LOGI(TAG, "Waiting for server messages (version, roster, etc.)...");
    
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

esp_err_t WiThrottleClient::acquireLocomotive(char throttleId, int address, bool isLongAddress)
{
    if (!isConnected()) {
        ESP_LOGW(TAG, "Not connected to server");
        return ESP_ERR_INVALID_STATE;
    }
    
    // WiThrottle protocol: M<throttleId>+<addressType><address><;><addressType><address>
    // Example: MT+S3<;>S3 (acquire short address 3 on throttle T)
    // The address format is repeated: once after + and once after <;>
    // Throttle IDs are typically single characters: T, S, 0-9, etc.
    // Address types: S (short, 1-127) or L (long, 128-9999)
    char addressType = isLongAddress ? 'L' : 'S';
    
    std::string command = "M" + std::string(1, throttleId) + 
                          "+" + std::string(1, addressType) + std::to_string(address) +
                          "<;>" + std::string(1, addressType) + std::to_string(address);
    
    ESP_LOGI(TAG, "Acquiring loco %d (%c) on throttle %c", address, addressType, throttleId);
    
    esp_err_t result = sendCommand(command);
    
    // Track the acquired loco state for this throttle
    if (result == ESP_OK) {
        m_throttleStates[throttleId].acquired = true;
        m_throttleStates[throttleId].address = address;
        m_throttleStates[throttleId].addressType = addressType;
    }
    
    return result;
}

esp_err_t WiThrottleClient::releaseLocomotive(char throttleId)
{
    if (!isConnected()) {
        ESP_LOGW(TAG, "Not connected to server");
        return ESP_ERR_INVALID_STATE;
    }
    
    // WiThrottle protocol: M<throttleId>-<addressType><address><;>r
    // Or to release ALL locos on throttle: M<throttleId>-*<;>r
    // Example: MT-*<;>r (release all locos on throttle T)
    std::string command = "M" + std::string(1, throttleId) + "-*<;>r";
    
    ESP_LOGI(TAG, "Releasing throttle %c", throttleId);
    
    esp_err_t result = sendCommand(command);
    
    // Clear the throttle state
    if (result == ESP_OK) {
        m_throttleStates[throttleId].acquired = false;
        m_throttleStates[throttleId].address = 0;
        m_throttleStates[throttleId].addressType = 'S';
    }
    
    return result;
}

esp_err_t WiThrottleClient::setSpeed(char throttleId, int speed)
{
    if (!isConnected()) {
        ESP_LOGW(TAG, "Not connected to server");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Check if throttle has an acquired loco
    auto it = m_throttleStates.find(throttleId);
    if (it == m_throttleStates.end() || !it->second.acquired) {
        ESP_LOGW(TAG, "No loco acquired on throttle %c", throttleId);
        return ESP_ERR_INVALID_STATE;
    }
    
    // Clamp speed to valid range
    if (speed < 0) speed = 0;
    if (speed > 126) speed = 126;
    
    // WiThrottle protocol: M<throttleId>A<addressType><address><;>V<speed>
    // Example: MTAS3<;>V50 (set loco S3 on throttle T to speed 50)
    const auto& state = it->second;
    std::string command = "M" + std::string(1, throttleId) + 
                         "A" + std::string(1, state.addressType) + std::to_string(state.address) +
                         "<;>V" + std::to_string(speed);
    
    ESP_LOGD(TAG, "Setting throttle %c speed to %d", throttleId, speed);
    
    return sendCommand(command);
}

esp_err_t WiThrottleClient::setDirection(char throttleId, bool forward)
{
    if (!isConnected()) {
        ESP_LOGW(TAG, "Not connected to server");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Check if throttle has an acquired loco
    auto it = m_throttleStates.find(throttleId);
    if (it == m_throttleStates.end() || !it->second.acquired) {
        ESP_LOGW(TAG, "No loco acquired on throttle %c", throttleId);
        return ESP_ERR_INVALID_STATE;
    }
    
    // WiThrottle protocol: M<throttleId>A<addressType><address><;>R<direction>
    // R1 = forward, R0 = reverse
    // Example: MTAS3<;>R1 (set loco S3 on throttle T forward)
    const auto& state = it->second;
    std::string command = "M" + std::string(1, throttleId) + 
                         "A" + std::string(1, state.addressType) + std::to_string(state.address) +
                         "<;>R" + (forward ? "1" : "0");
    
    ESP_LOGI(TAG, "Setting throttle %c direction: %s", throttleId, forward ? "FORWARD" : "REVERSE");
    
    return sendCommand(command);
}

esp_err_t WiThrottleClient::setFunction(char throttleId, int function, bool state)
{
    if (!isConnected()) {
        ESP_LOGW(TAG, "Not connected to server");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Check if throttle has an acquired loco
    auto it = m_throttleStates.find(throttleId);
    if (it == m_throttleStates.end() || !it->second.acquired) {
        ESP_LOGW(TAG, "No loco acquired on throttle %c", throttleId);
        return ESP_ERR_INVALID_STATE;
    }
    
    // Validate function number
    if (function < 0 || function > 28) {
        ESP_LOGW(TAG, "Invalid function number: %d", function);
        return ESP_ERR_INVALID_ARG;
    }
    
    // WiThrottle protocol: M<throttleId>A<addressType><address><;>F<state><function>
    // F1<function> = activate, F0<function> = deactivate
    // Example: MTAS3<;>F10 (activate F0 on loco S3, throttle T)
    const auto& throttleState = it->second;
    std::string command = "M" + std::string(1, throttleId) + 
                         "A" + std::string(1, throttleState.addressType) + std::to_string(throttleState.address) +
                         "<;>F" + (state ? "1" : "0") + std::to_string(function);
    
    ESP_LOGD(TAG, "Setting throttle %c function F%d: %s", throttleId, function, state ? "ON" : "OFF");
    
    return sendCommand(command);
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
    // Log at debug level for normal operation
    ESP_LOGD(TAG, "RX: %s", message.c_str());
    
    if (message.empty()) {
        return;
    }
    
    // Parse message type
    char msgType = message[0];
    
    switch (msgType) {
        case 'P':  // Power or Web Port
            if (message.length() > 1 && message[1] == 'W') {
                // Web Port (PW<port>)
                if (message.length() > 2) {
                    m_webPort = std::atoi(message.substr(2).c_str());
                    ESP_LOGI(TAG, "Discovered JSON web server port: %d", m_webPort);
                    if (m_webPortCallback) {
                        m_webPortCallback(m_webPort);
                    }
                }
            } else if (message.length() > 1 && message[1] == 'P') {
                // Power message (PPA)
                handlePowerMessage(message);
            }
            break;
            
        case 'V':  // Version
            ESP_LOGI(TAG, "Server version: %s", message.substr(1).c_str());
            break;
            
        case 'R':  // Roster or Routes
            if (message.length() > 1 && message[1] == 'L') {
                // Roster List
                handleRosterMessage(message);
            } else if (message.length() > 1 && message[1] == 'C') {
                // Roster Consist (ignore for now)
                ESP_LOGD(TAG, "Roster consist message (ignored)");
            } else {
                ESP_LOGD(TAG, "Other roster message: %s", message.c_str());
            }
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
        ESP_LOGW(TAG, "Cannot send command - not connected");
        return ESP_ERR_INVALID_STATE;
    }
    
    std::string fullCommand = command + "\n";
    int len = send(m_socket, fullCommand.c_str(), fullCommand.length(), 0);
    
    if (len < 0) {
        ESP_LOGE(TAG, "Failed to send command: %d", errno);
        return ESP_FAIL;
    }
    
    // Log all sent commands at INFO level for testing
    ESP_LOGD(TAG, "TX: %s", command.c_str());
    return ESP_OK;
}

void WiThrottleClient::handleRosterMessage(const std::string& message)
{
    // Roster format: RL<count>]\[<name1>}|{<addr1>}|{<type1>]\[<name2>}|{<addr2>}|{<type2>...
    // Example: RL2]\[56086}|{3}|{S]\[Shunter}|{4}|{S
    // Delimiters: ]\[ (3 chars) separates entries, }|{ (3 chars) separates fields
    
    ESP_LOGI(TAG, "Parsing roster message");
    
    if (message.length() < 3 || message.substr(0, 2) != "RL") {
        ESP_LOGW(TAG, "Invalid roster message format");
        return;
    }
    
    // Clear existing roster
    m_roster.clear();
    
    // Find the count (ends with ])
    size_t countEnd = message.find(']', 2);
    if (countEnd == std::string::npos) {
        ESP_LOGW(TAG, "No count delimiter found");
        return;
    }
    
    int count = std::atoi(message.substr(2, countEnd - 2).c_str());
    ESP_LOGI(TAG, "Roster count: %d", count);
    
    // Parse each loco entry
    // After count delimiter ], we start with ]\[ (backslash IS part of protocol)
    size_t pos = countEnd + 1; // Position after the ]
    
    for (int i = 0; i < count; i++) {
        // Expect ]\[ delimiter (3 characters: ], \, [)
        if (pos + 2 >= message.length() || 
            message[pos] != '\\' || message[pos + 1] != '[') {
            ESP_LOGW(TAG, "Expected \\[ at position %d (got '%c%c')", 
                     pos, message[pos], message[pos+1]);
            break;
        }
        pos += 2; // Skip the \[
        
        // Find name (ends with }|{ delimiter)
        size_t nameEnd = message.find("}|{", pos);
        if (nameEnd == std::string::npos) {
            ESP_LOGW(TAG, "No name delimiter at position %d", pos);
            break;
        }
        std::string name = message.substr(pos, nameEnd - pos);
        pos = nameEnd + 3; // Skip the }|{
        
        // Find address (ends with }|{)
        size_t addrEnd = message.find("}|{", pos);
        if (addrEnd == std::string::npos) {
            ESP_LOGW(TAG, "No address delimiter at position %d", pos);
            break;
        }
        std::string addrStr = message.substr(pos, addrEnd - pos);
        int address = std::atoi(addrStr.c_str());
        pos = addrEnd + 3;
        
        // Get address type (S or L)
        char addressType = 'S';
        if (pos < message.length()) {
            addressType = message[pos];
            pos++;
        }
        
        // Skip the ] that ends this entry
        if (pos < message.length() && message[pos] == ']') {
            pos++; // Now positioned at \ for next entry's ]\[ delimiter
        }
        
        // Add to roster
        Locomotive loco(address, name, addressType);
        m_roster.push_back(loco);
        
        ESP_LOGD(TAG, "  Loco %d: '%s' addr=%d (%c)", i + 1, name.c_str(), address, addressType);
    }
    
    ESP_LOGI(TAG, "Roster loaded: %d locomotives", m_roster.size());
    
    // Notify callback
    if (m_rosterCallback) {
        m_rosterCallback(m_roster);
    }
}
