#include "WiThrottleClient.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static const char* TAG = "WiThrottleClient";

// WiThrottle protocol commands
static const char* CMD_HEARTBEAT = "*";
static const char* CMD_HEARTBEAT_ON = "*+";
static const char* CMD_TRACK_POWER = "PPA";  // Power command
static const char* DEVICE_NAME = "ESP32-Layout-Controller";

// The receive task wakes at least this often, which is what lets it notice a
// requested shutdown and keep heartbeats on time without a task of their own.
static constexpr int RX_TIMEOUT_S = 1;

// Comfortably longer than one receive timeout plus a callback's worst-case wait
// for the LVGL lock, so a teardown that gives up means the task is wedged.
static constexpr int RX_EXIT_WAIT_MS = 3000;

// A send takes microseconds; one holding the send mutex this long is stuck on a
// peer that stopped reading, and teardown proceeds without waiting for it.
static constexpr int SEND_LOCK_WAIT_MS = 500;

// Longest heartbeat interval accepted from the server. Anything larger is taken
// as malformed rather than as permission to go quiet for hours.
static constexpr long MAX_HEARTBEAT_INTERVAL_S = 3600;

WiThrottleClient::WiThrottleClient()
    : m_state(ConnectionState::DISCONNECTED)
    , m_socket(-1)
    , m_serverHost("")
    , m_serverPort(12090)
    , m_mainTrackPower(PowerState::UNKNOWN)
    , m_progTrackPower(PowerState::UNKNOWN)
    , m_webPort(0)
    , m_stateMutex(nullptr)
    , m_sendMutex(nullptr)
    , m_taskExitSemaphore(nullptr)
    , m_receiveTaskHandle(nullptr)
    , m_running(false)
    , m_heartbeatPeriodMs(0)
    , m_lastHeartbeatUs(0)
{
    m_stateMutex = xSemaphoreCreateMutex();
    if (!m_stateMutex) {
        ESP_LOGE(TAG, "Failed to create WiThrottle state mutex");
    }
    m_sendMutex = xSemaphoreCreateMutex();
    if (!m_sendMutex) {
        ESP_LOGE(TAG, "Failed to create WiThrottle send mutex");
    }
    m_taskExitSemaphore = xSemaphoreCreateBinary();
    if (!m_taskExitSemaphore) {
        ESP_LOGE(TAG, "Failed to create WiThrottle task-exit semaphore");
    }
}

WiThrottleClient::~WiThrottleClient()
{
    disconnect();
    if (m_stateMutex) {
        vSemaphoreDelete(m_stateMutex);
        m_stateMutex = nullptr;
    }
    if (m_sendMutex) {
        vSemaphoreDelete(m_sendMutex);
        m_sendMutex = nullptr;
    }
    if (m_taskExitSemaphore) {
        vSemaphoreDelete(m_taskExitSemaphore);
        m_taskExitSemaphore = nullptr;
    }
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

    // When the server ends a session, its receive task exits but the socket is
    // left open. Reap that here, before a new socket replaces it: overwriting it
    // leaked one of lwIP's ten sockets per JMRI restart or WiFi drop (F-22).
    teardownSession();
    
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
    
    // Receive timeout: see RX_TIMEOUT_S.
    struct timeval timeout;
    timeout.tv_sec = RX_TIMEOUT_S;
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
    
    // Identify: HU first, a stable per-device id, so JMRI can recognise this
    // device across reconnects; then the name shown in JMRI. The server answers
    // N with its heartbeat interval, handled in handleHeartbeatAnnouncement.
    ESP_LOGI(TAG, "Sending device identification...");
    sendCommand("HU" + deviceId());
    sendCommand(std::string("N") + DEVICE_NAME);
    
    ESP_LOGI(TAG, "Waiting for server messages (version, roster, etc.)...");

    // Heartbeats are off until this session's server announces an interval.
    m_heartbeatPeriodMs = 0;
    m_lastHeartbeatUs = 0;

    // Only a task that timed out on its way out can have left a stale signal
    // here; clear it so the next teardown waits for the task it is stopping.
    if (m_taskExitSemaphore) {
        xSemaphoreTake(m_taskExitSemaphore, 0);
    }

    // Start receive task
    m_running = true;
    if (xTaskCreate(receiveTask, "withrottle_rx", 4096, this, 5, &m_receiveTaskHandle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create receive task");
        m_receiveTaskHandle = nullptr;
        teardownSession();
        setState(ConnectionState::FAILED);
        return ESP_FAIL;
    }

    // A new session holds nothing, even when the UI still shows locos allocated
    // from the last one. Without this, after a JMRI restart every throttle looks
    // live while JMRI ignores its commands -- the stop press included (F-22).
    reacquireLocomotives();
    
    return ESP_OK;
}

void WiThrottleClient::teardownSession()
{
    // Take the socket away from senders first, under the send mutex, so no new
    // send() can start on it. Bounded: a send() stuck on a peer that stopped
    // reading holds the mutex, and the shutdown() below is what frees it.
    const bool haveSendLock =
        m_sendMutex && xSemaphoreTake(m_sendMutex, pdMS_TO_TICKS(SEND_LOCK_WAIT_MS)) == pdTRUE;
    const int sock = m_socket;
    m_socket = -1;
    if (haveSendLock) {
        xSemaphoreGive(m_sendMutex);
    }

    if (sock < 0) {
        return;
    }

    // Unblocks recv() -- the task reads from its own copy of the descriptor --
    // and any send() still in flight.
    m_running = false;
    shutdown(sock, SHUT_RDWR);

    bool exited = true;
    if (m_receiveTaskHandle && m_taskExitSemaphore) {
        exited = (xSemaphoreTake(m_taskExitSemaphore, pdMS_TO_TICKS(RX_EXIT_WAIT_MS)) == pdTRUE);
        m_receiveTaskHandle = nullptr;
    }

    // If a send() was in flight, let it leave before the descriptor goes.
    if (!haveSendLock && m_sendMutex &&
        xSemaphoreTake(m_sendMutex, pdMS_TO_TICKS(SEND_LOCK_WAIT_MS)) == pdTRUE) {
        xSemaphoreGive(m_sendMutex);
    }

    if (exited) {
        close(sock);
    } else {
        // Closing now would free the descriptor for reuse while the task may
        // still read from it -- and lwIP hands out the lowest free index, so
        // the next socket opened would be the one it read. One leaked socket
        // is the lesser harm, and only a wedged task gets here.
        ESP_LOGE(TAG, "Receive task did not exit within %d ms; leaving socket %d open",
                 RX_EXIT_WAIT_MS, sock);
    }
}

void WiThrottleClient::reacquireLocomotives()
{
    std::vector<std::pair<char, ThrottleState>> held;
    if (lockState(pdMS_TO_TICKS(100))) {
        for (const auto& entry : m_throttleStates) {
            if (entry.second.acquired) {
                held.emplace_back(entry.first, entry.second);
            }
        }
        unlockState();
    } else {
        ESP_LOGW(TAG, "Failed to lock state to re-acquire locos");
        return;
    }

    for (const auto& entry : held) {
        const char throttleId = entry.first;
        const ThrottleState& state = entry.second;
        const std::string address = std::string(1, state.addressType) + std::to_string(state.address);
        ESP_LOGI(TAG, "Re-acquiring loco %s on throttle %c for the new session",
                 address.c_str(), throttleId);
        // JMRI answers with the loco's current speed and direction, which
        // re-seeds the display for the new session.
        sendCommand("M" + std::string(1, throttleId) + "+" + address + "<;>" + address);
    }
}

void WiThrottleClient::disconnect()
{
    if (m_socket >= 0) {
        ESP_LOGI(TAG, "Disconnecting from WiThrottle server");
    }
    teardownSession();
    
    setState(ConnectionState::DISCONNECTED);
    m_mainTrackPower = PowerState::UNKNOWN;
    m_progTrackPower = PowerState::UNKNOWN;

    // The acquisition record is kept on purpose. It mirrors what the UI shows
    // as allocated -- ThrottleController does not release on a disconnect --
    // and the next connect() re-acquires from it. Clearing it here left the
    // UI showing live throttles whose every command this client then refused.
    // Only releaseLocomotive() removes an entry.
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
        if (lockState(pdMS_TO_TICKS(50))) {
            m_throttleStates[throttleId].acquired = true;
            m_throttleStates[throttleId].address = address;
            m_throttleStates[throttleId].addressType = addressType;
            unlockState();
        } else {
            ESP_LOGW(TAG, "Failed to lock state for acquire tracking");
        }
    }
    
    return result;
}

esp_err_t WiThrottleClient::releaseLocomotive(char throttleId)
{
    // Forget the loco whether or not the release reaches the server. The
    // record follows what the UI shows, and the UI has released it: keeping it
    // would re-acquire, on the next session, a loco the operator let go (F-22).
    if (lockState(pdMS_TO_TICKS(50))) {
        m_throttleStates.erase(throttleId);
        unlockState();
    } else {
        ESP_LOGW(TAG, "Failed to lock state for release tracking");
    }

    if (!isConnected()) {
        ESP_LOGW(TAG, "Not connected to server; release recorded locally only");
        return ESP_ERR_INVALID_STATE;
    }
    
    // WiThrottle protocol: M<throttleId>-<addressType><address><;>r
    // Or to release ALL locos on throttle: M<throttleId>-*<;>r
    // Example: MT-*<;>r (release all locos on throttle T)
    std::string command = "M" + std::string(1, throttleId) + "-*<;>r";
    
    ESP_LOGI(TAG, "Releasing throttle %c", throttleId);
    
    return sendCommand(command);
}

esp_err_t WiThrottleClient::setSpeed(char throttleId, int speed)
{
    if (!isConnected()) {
        ESP_LOGW(TAG, "Not connected to server");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Check if throttle has an acquired loco
    ThrottleState state;
    if (!lockState(pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "Failed to lock state for speed command");
        return ESP_ERR_INVALID_STATE;
    }
    auto it = m_throttleStates.find(throttleId);
    if (it == m_throttleStates.end() || !it->second.acquired) {
        unlockState();
        ESP_LOGW(TAG, "No loco acquired on throttle %c", throttleId);
        return ESP_ERR_INVALID_STATE;
    }
    state = it->second;
    unlockState();
    
    // Clamp speed to valid range
    if (speed < 0) speed = 0;
    if (speed > 126) speed = 126;
    
    // WiThrottle protocol: M<throttleId>A<addressType><address><;>V<speed>
    // Example: MTAS3<;>V50 (set loco S3 on throttle T to speed 50)
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
    ThrottleState state;
    if (!lockState(pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "Failed to lock state for direction command");
        return ESP_ERR_INVALID_STATE;
    }
    auto it = m_throttleStates.find(throttleId);
    if (it == m_throttleStates.end() || !it->second.acquired) {
        unlockState();
        ESP_LOGW(TAG, "No loco acquired on throttle %c", throttleId);
        return ESP_ERR_INVALID_STATE;
    }
    state = it->second;
    unlockState();
    
    // WiThrottle protocol: M<throttleId>A<addressType><address><;>R<direction>
    // R1 = forward, R0 = reverse
    // Example: MTAS3<;>R1 (set loco S3 on throttle T forward)
    std::string command = "M" + std::string(1, throttleId) + 
                         "A" + std::string(1, state.addressType) + std::to_string(state.address) +
                         "<;>R" + (forward ? "1" : "0");
    
    ESP_LOGD(TAG, "Setting throttle %c direction: %s", throttleId, forward ? "FORWARD" : "REVERSE");
    
    return sendCommand(command);
}

esp_err_t WiThrottleClient::setFunction(char throttleId, int function, bool state)
{
    if (!isConnected()) {
        ESP_LOGW(TAG, "Not connected to server");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Check if throttle has an acquired loco
    ThrottleState throttleState;
    if (!lockState(pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "Failed to lock state for function command");
        return ESP_ERR_INVALID_STATE;
    }
    auto it = m_throttleStates.find(throttleId);
    if (it == m_throttleStates.end() || !it->second.acquired) {
        unlockState();
        ESP_LOGW(TAG, "No loco acquired on throttle %c", throttleId);
        return ESP_ERR_INVALID_STATE;
    }
    throttleState = it->second;
    unlockState();
    
    // Validate function number
    if (function < 0 || function > 28) {
        ESP_LOGW(TAG, "Invalid function number: %d", function);
        return ESP_ERR_INVALID_ARG;
    }
    
    // WiThrottle protocol: M<throttleId>A<addressType><address><;>F<state><function>
    // F1<function> = activate, F0<function> = deactivate
    // Example: MTAS3<;>F10 (activate F0 on loco S3, throttle T)
    std::string command = "M" + std::string(1, throttleId) + 
                         "A" + std::string(1, throttleState.addressType) + std::to_string(throttleState.address) +
                         "<;>F" + (state ? "1" : "0") + std::to_string(function);
    
    ESP_LOGI(TAG, "Sending function command: throttle %c F%d -> %s", throttleId, function, state ? "ON" : "OFF");
    ESP_LOGD(TAG, "Function command payload: %s", command.c_str());
    
    return sendCommand(command);
}

esp_err_t WiThrottleClient::emergencyStopAll()
{
    if (!isConnected()) {
        ESP_LOGW(TAG, "Not connected to server; emergency stop not sent");
        return ESP_ERR_INVALID_STATE;
    }

    std::vector<std::pair<char, ThrottleState>> held;
    if (!lockState(pdMS_TO_TICKS(100))) {
        ESP_LOGE(TAG, "Failed to lock state for emergency stop");
        return ESP_ERR_TIMEOUT;
    }
    for (const auto& entry : m_throttleStates) {
        if (entry.second.acquired) {
            held.emplace_back(entry.first, entry.second);
        }
    }
    unlockState();

    // Every loco is tried even if one fails: a stop that reaches three of four
    // locos is better than one that gives up at the first.
    esp_err_t result = ESP_OK;
    for (const auto& entry : held) {
        const std::string command = "M" + std::string(1, entry.first) + "A" +
                                    std::string(1, entry.second.addressType) +
                                    std::to_string(entry.second.address) + "<;>X";
        ESP_LOGW(TAG, "EMERGENCY STOP throttle %c (loco %c%d)", entry.first,
                 entry.second.addressType, entry.second.address);
        const esp_err_t err = sendCommand(command);
        if (err != ESP_OK && result == ESP_OK) {
            result = err;
        }
    }
    return result;
}

esp_err_t WiThrottleClient::querySpeed(char throttleId)
{
    if (!isConnected()) {
        ESP_LOGW(TAG, "Not connected to server");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Check if throttle has an acquired loco
    ThrottleState throttleState;
    if (!lockState(pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "Failed to lock state for speed query");
        return ESP_ERR_INVALID_STATE;
    }
    auto it = m_throttleStates.find(throttleId);
    if (it == m_throttleStates.end() || !it->second.acquired) {
        unlockState();
        ESP_LOGW(TAG, "No loco acquired on throttle %c", throttleId);
        return ESP_ERR_INVALID_STATE;
    }
    throttleState = it->second;
    unlockState();
    
    // WiThrottle protocol: M<throttleId>A<addressType><address><;>qV
    // Response will be: M<throttleId>A<addressType><address><;>V<speed>
    std::string command = "M" + std::string(1, throttleId) + 
                         "A" + std::string(1, throttleState.addressType) + std::to_string(throttleState.address) +
                         "<;>qV";
    
    ESP_LOGD(TAG, "Querying throttle %c speed", throttleId);
    
    return sendCommand(command);
}

esp_err_t WiThrottleClient::queryDirection(char throttleId)
{
    if (!isConnected()) {
        ESP_LOGW(TAG, "Not connected to server");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Check if throttle has an acquired loco
    ThrottleState throttleState;
    if (!lockState(pdMS_TO_TICKS(50))) {
        ESP_LOGW(TAG, "Failed to lock state for direction query");
        return ESP_ERR_INVALID_STATE;
    }
    auto it = m_throttleStates.find(throttleId);
    if (it == m_throttleStates.end() || !it->second.acquired) {
        unlockState();
        ESP_LOGW(TAG, "No loco acquired on throttle %c", throttleId);
        return ESP_ERR_INVALID_STATE;
    }
    throttleState = it->second;
    unlockState();
    
    // WiThrottle protocol: M<throttleId>A<addressType><address><;>qR
    // Response will be: M<throttleId>A<addressType><address><;>R<direction>
    std::string command = "M" + std::string(1, throttleId) + 
                         "A" + std::string(1, throttleState.addressType) + std::to_string(throttleState.address) +
                         "<;>qR";
    
    ESP_LOGD(TAG, "Querying throttle %c direction", throttleId);
    
    return sendCommand(command);
}

void WiThrottleClient::sendHeartbeat()
{
    if (isConnected()) {
        sendCommand(CMD_HEARTBEAT);
    }
}

#if CONFIG_THROTTLE_TESTS
void WiThrottleClient::testProcessMessage(const std::string& message)
{
    processMessage(message);
}
#endif

bool WiThrottleClient::lockState(TickType_t timeout) const
{
    if (!m_stateMutex) {
        return true;
    }
    return xSemaphoreTake(m_stateMutex, timeout) == pdTRUE;
}

void WiThrottleClient::unlockState() const
{
    if (m_stateMutex) {
        xSemaphoreGive(m_stateMutex);
    }
}

std::vector<WiThrottleClient::Locomotive> WiThrottleClient::getRosterSnapshot() const
{
    std::vector<Locomotive> snapshot;
    if (!lockState(pdMS_TO_TICKS(50))) {
        return snapshot;
    }
    snapshot = m_roster;
    unlockState();
    return snapshot;
}

size_t WiThrottleClient::getRosterSize() const
{
    if (!lockState(pdMS_TO_TICKS(50))) {
        return 0;
    }
    size_t size = m_roster.size();
    unlockState();
    return size;
}

bool WiThrottleClient::getRosterEntry(int index, Locomotive& outEntry) const
{
    if (index < 0) {
        return false;
    }
    if (!lockState(pdMS_TO_TICKS(50))) {
        return false;
    }
    if (index >= static_cast<int>(m_roster.size())) {
        unlockState();
        return false;
    }
    outEntry = m_roster[static_cast<size_t>(index)];
    unlockState();
    return true;
}

void WiThrottleClient::receiveTask(void* arg)
{
    WiThrottleClient* client = static_cast<WiThrottleClient*>(arg);
    char buffer[512];
    std::string messageBuffer;

    // This task's own copy. teardownSession() clears m_socket before closing,
    // and must not be able to swap a different descriptor under a live recv().
    const int sock = client->m_socket;
    
    while (client->m_running) {
        // Every pass -- after data or after the one-second receive timeout --
        // is a chance to send a heartbeat that has come due.
        client->serviceHeartbeat();

        int len = recv(sock, buffer, sizeof(buffer) - 1, 0);
        
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
        
        // Guard against unbounded growth (e.g. server sends data with no newlines)
        static constexpr size_t MAX_MESSAGE_BUFFER = 4096;
        if (messageBuffer.size() > MAX_MESSAGE_BUFFER) {
            ESP_LOGW(TAG, "Message buffer exceeded %zu bytes — discarding", MAX_MESSAGE_BUFFER);
            messageBuffer.clear();
        }
        
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
    
    // Signal disconnect() that we have exited
    if (client->m_taskExitSemaphore) {
        xSemaphoreGive(client->m_taskExitSemaphore);
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
                    m_webPortCallback(m_webPort);
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
            
        case '*':  // Heartbeat interval, announced by the server after N
            handleHeartbeatAnnouncement(message);
            break;
            
        case 'M':  // Multi-throttle (throttle state changes)
            handleThrottleMessage(message);
            break;
            
        default:
            ESP_LOGW(TAG, "Unhandled message type: %c", msgType);
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
    if (mainChanged) {
        m_powerCallback("main", newState);
    }
    if (progChanged) {
        m_powerCallback("prog", newState);
    }
}

void WiThrottleClient::handleHeartbeatAnnouncement(const std::string& message)
{
    // "*<seconds>": how long JMRI will wait between messages before e-stopping
    // this device's locos -- but only once monitoring is switched on with "*+".
    // Never sending "*+" left the dead-man switch off, so a crashed, rebooted
    // or half-open device left its locos running (F-23).
    const char* digits = message.c_str() + 1;
    char* end = nullptr;
    const long seconds = std::strtol(digits, &end, 10);
    if (end == digits || *end != '\0' || seconds < 0 || seconds > MAX_HEARTBEAT_INTERVAL_S) {
        ESP_LOGW(TAG, "Ignoring malformed heartbeat announcement: %s", message.c_str());
        return;
    }

    if (seconds == 0) {
        ESP_LOGW(TAG, "Server has heartbeat monitoring disabled; no dead-man stop for this device");
        m_heartbeatPeriodMs = 0;
        return;
    }

    // Half the interval, so one late or lost heartbeat is not an e-stop.
    m_heartbeatPeriodMs = static_cast<uint32_t>(seconds) * 500;
    m_lastHeartbeatUs = esp_timer_get_time();
    sendCommand(CMD_HEARTBEAT_ON);
    ESP_LOGI(TAG, "Heartbeat monitoring on: server interval %lds, sending every %lu ms",
             seconds, static_cast<unsigned long>(m_heartbeatPeriodMs));
}

void WiThrottleClient::serviceHeartbeat()
{
    if (m_heartbeatPeriodMs == 0) {
        return;
    }
    const int64_t now = esp_timer_get_time();
    if (now - m_lastHeartbeatUs >= static_cast<int64_t>(m_heartbeatPeriodMs) * 1000) {
        m_lastHeartbeatUs = now;
        sendHeartbeat();
    }
}

std::string WiThrottleClient::deviceId()
{
    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        return "ESP32LC";
    }
    char id[13];
    snprintf(id, sizeof(id), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return std::string(id);
}

void WiThrottleClient::setState(ConnectionState newState)
{
    if (m_state != newState) {
        m_state = newState;
        ESP_LOGI(TAG, "Connection state changed: %d", (int)newState);
        
        m_connectionCallback(newState);
    }
}

esp_err_t WiThrottleClient::sendCommand(const std::string& command)
{
    if (m_socket < 0) {
        ESP_LOGW(TAG, "Cannot send command - not connected");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Serialise send() across tasks (main, polling timer, receive heartbeat)
    if (m_sendMutex && xSemaphoreTake(m_sendMutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire send mutex");
        return ESP_ERR_TIMEOUT;
    }

    // Re-read under the mutex: teardownSession() clears it under the same
    // mutex before closing, so a descriptor read here is still open.
    const int sock = m_socket;
    if (sock < 0) {
        if (m_sendMutex) {
            xSemaphoreGive(m_sendMutex);
        }
        ESP_LOGW(TAG, "Cannot send command - session ended");
        return ESP_ERR_INVALID_STATE;
    }
    
    std::string fullCommand = command + "\n";
    int len = send(sock, fullCommand.c_str(), fullCommand.length(), 0);
    
    if (m_sendMutex) {
        xSemaphoreGive(m_sendMutex);
    }
    
    if (len < 0) {
        ESP_LOGE(TAG, "Failed to send command: %d", errno);
        return ESP_FAIL;
    }
    
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
    
    std::vector<Locomotive> newRoster;
    
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
            // Not message[pos + 1]: pos can already be at the end (F-42).
            ESP_LOGW(TAG, "Roster entry %d has no \\[ at offset %u",
                     i + 1, static_cast<unsigned>(pos));
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
    newRoster.push_back(loco);
        
        ESP_LOGD(TAG, "  Loco %d: '%s' addr=%d (%c)", i + 1, name.c_str(), address, addressType);
    }
    
    ESP_LOGI(TAG, "Roster loaded: %d locomotives", newRoster.size());

    if (lockState(pdMS_TO_TICKS(50))) {
        m_roster = newRoster;
        unlockState();
    } else {
        ESP_LOGW(TAG, "Failed to lock state for roster update");
    }
    
    // Notify callback with snapshot
    m_rosterCallback(newRoster);
}

void WiThrottleClient::handleThrottleMessage(const std::string& message)
{
    // Multi-throttle message format: M<throttleId><command><data>
    // Examples:
    //   M0AS3<;>V50     - Throttle 0, Action, address S3, speed 50
    //   M0AS3<;>R1      - Throttle 0, Action, address S3, direction forward
    //   M0AS3<;>F15     - Throttle 0, Action, address S3, function 1 on

    if (message.length() < 3) {
        ESP_LOGW(TAG, "Throttle message too short: %s", message.c_str());
        return;
    }
    
    char throttleId = message[1];  // '0', '1', '2', '3'
    char command = message[2];     // 'A' = action, '+' = add, '-' = remove, 'L' = labels
    
    if (command == 'L') {
        size_t delimPos = message.find("<;>");
        if (delimPos == std::string::npos) {
            ESP_LOGW(TAG, "Throttle label message missing delimiter: %s", message.c_str());
            return;
        }

        std::string data = message.substr(delimPos + 3);
        std::vector<std::string> labels;

        const std::string delimiter = "]\\[";
        size_t pos = 0;
        if (data.rfind(delimiter, 0) == 0) {
            pos = delimiter.size();
        }

        while (pos <= data.length()) {
            size_t next = data.find(delimiter, pos);
            if (next == std::string::npos) {
                labels.push_back(data.substr(pos));
                break;
            }
            labels.push_back(data.substr(pos, next - pos));
            pos = next + delimiter.size();
        }

        if (labels.size() < 29) {
            labels.resize(29);
        }
        if (labels.size() > 29) {
            labels.resize(29);
        }

        m_functionLabelsCallback(throttleId, labels);
        return;
    }

    // We only care about 'A' (action) messages for state updates
    if (command != 'A') {
        ESP_LOGD(TAG, "Ignoring non-action throttle message: %s", message.c_str());
        return;
    }
    
    // Find the <;> delimiter that separates address from data
    size_t delimPos = message.find("<;>");
    if (delimPos == std::string::npos) {
        ESP_LOGW(TAG, "Throttle message missing delimiter: %s", message.c_str());
        return;
    }
    
    // Extract address (e.g., "S3" or "L41")
    std::string addressPart = message.substr(3, delimPos - 3);
    if (addressPart.length() < 2) {
        ESP_LOGW(TAG, "Throttle message invalid address: %s", message.c_str());
        return;
    }
    
    int address = std::atoi(addressPart.substr(1).c_str());
    
    // Extract command data after delimiter
    std::string data = message.substr(delimPos + 3);
    if (data.empty()) {
        ESP_LOGW(TAG, "Throttle message missing data: %s", message.c_str());
        return;
    }
    
    // Parse the data command
    char dataType = data[0];  // 'V' = speed, 'R' = direction, 'F' = function
    
    ThrottleUpdate update;
    update.throttleId = throttleId;
    update.address = address;
    update.speed = -1;
    update.direction = -1;
    update.function = -1;
    update.functionState = false;
    
    switch (dataType) {
        case 'V':  // Speed
            if (data.length() > 1) {
                update.speed = std::atoi(data.substr(1).c_str());
                ESP_LOGD(TAG, "Throttle %c speed: %d", throttleId, update.speed);
            }
            break;
            
        case 'R':  // Direction
            if (data.length() > 1) {
                update.direction = (data[1] == '1') ? 1 : 0;
                ESP_LOGD(TAG, "Throttle %c direction: %s", throttleId, update.direction ? "forward" : "reverse");
            }
            break;
            
        case 'F':  // Function
            if (data.length() > 2) {
                // Format: F<state><function> e.g., F10 (F0 on), F110 (F10 on)
                bool funcState = (data[1] == '1');
                int funcNum = std::atoi(data.substr(2).c_str());
                update.function = funcNum;
                update.functionState = funcState;
                ESP_LOGD(TAG, "Throttle %c function %d: %s", throttleId, funcNum, funcState ? "on" : "off");
            }
            break;
            
        default:
            ESP_LOGD(TAG, "Unknown throttle data type: %c in %s", dataType, message.c_str());
            return;
    }
    
    // Notify callback
    m_throttleCallback(update);
}
