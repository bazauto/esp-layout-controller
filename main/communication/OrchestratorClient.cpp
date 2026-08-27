#include "OrchestratorClient.h"

#include <cstring>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "OrchestratorClient";

namespace {

/** Login and roster reads are small; this caps a hostile or broken response. */
constexpr int HTTP_RESPONSE_LIMIT = 8192;

/** A STATE_SNAPSHOT carrying a whole layout is the largest frame we expect. */
constexpr size_t MAX_FRAME_BYTES = 24576;

constexpr int HTTP_TIMEOUT_MS = 8000;

/** Collects a response body and the session cookie during an HTTP request. */
struct HttpCapture {
    std::string body;
    std::string setCookie;
    bool truncated = false;
};

esp_err_t httpEventHandler(esp_http_client_event_t* evt)
{
    auto* capture = static_cast<HttpCapture*>(evt->user_data);
    if (!capture) {
        return ESP_OK;
    }

    switch (evt->event_id) {
        case HTTP_EVENT_ON_HEADER:
            // Header names are case-insensitive on the wire, so do not assume
            // the server spells it exactly "Set-Cookie".
            if (evt->header_key && strcasecmp(evt->header_key, "Set-Cookie") == 0 &&
                evt->header_value) {
                capture->setCookie = evt->header_value;
            }
            break;

        case HTTP_EVENT_ON_DATA:
            if (evt->data_len > 0 && evt->data) {
                if (capture->body.size() + evt->data_len > HTTP_RESPONSE_LIMIT) {
                    capture->truncated = true;
                    break;
                }
                capture->body.append(static_cast<const char*>(evt->data), evt->data_len);
            }
            break;

        default:
            break;
    }
    return ESP_OK;
}

/** Reads a required string field. Returns false when absent or not a string. */
bool jsonString(const cJSON* obj, const char* key, std::string& out)
{
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(item) || item->valuestring == nullptr) {
        return false;
    }
    out = item->valuestring;
    return true;
}

/**
 * @brief Reads a required integer field.
 *
 * Rejects a non-number and a non-integral number alike. cJSON reports every
 * number as a double, so 12.7 would otherwise truncate silently into a speed.
 */
bool jsonInt(const cJSON* obj, const char* key, int& out)
{
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    const double value = item->valuedouble;
    if (value != static_cast<double>(static_cast<int>(value))) {
        return false;
    }
    out = static_cast<int>(value);
    return true;
}

const char* directionToWire(OrchestratorClient::Direction direction)
{
    switch (direction) {
        case OrchestratorClient::Direction::FORWARD: return "fwd";
        case OrchestratorClient::Direction::REVERSE: return "rev";
        case OrchestratorClient::Direction::STOP:
        default:                                    return "stop";
    }
}

bool wireToDirection(const std::string& wire, OrchestratorClient::Direction& out)
{
    if (wire == "fwd")  { out = OrchestratorClient::Direction::FORWARD; return true; }
    if (wire == "rev")  { out = OrchestratorClient::Direction::REVERSE; return true; }
    if (wire == "stop") { out = OrchestratorClient::Direction::STOP;    return true; }
    return false;
}

bool wireToSystemStatus(const std::string& wire, OrchestratorClient::SystemStatus& out)
{
    if (wire == "online")    { out = OrchestratorClient::SystemStatus::ONLINE;    return true; }
    if (wire == "safe-stop") { out = OrchestratorClient::SystemStatus::SAFE_STOP; return true; }
    if (wire == "offline")   { out = OrchestratorClient::SystemStatus::OFFLINE;   return true; }
    return false;
}

}  // namespace

OrchestratorClient::OrchestratorClient()
    : m_client(nullptr)
    , m_state(ConnectionState::DISCONNECTED)
    , m_port(DEFAULT_PORT)
    , m_systemStatus(SystemStatus::UNKNOWN)
    , m_trackPower(TrackPower::UNKNOWN)
    , m_lastMessageUs(0)
    , m_stateMutex(nullptr)
{
}

OrchestratorClient::~OrchestratorClient()
{
    disconnect();
    if (m_stateMutex) {
        vSemaphoreDelete(m_stateMutex);
        m_stateMutex = nullptr;
    }
}

esp_err_t OrchestratorClient::initialize()
{
    if (!m_stateMutex) {
        m_stateMutex = xSemaphoreCreateMutex();
        if (!m_stateMutex) {
            ESP_LOGE(TAG, "Failed to create state mutex");
            return ESP_ERR_NO_MEM;
        }
    }
    ESP_LOGI(TAG, "Orchestrator client initialised");
    return ESP_OK;
}

bool OrchestratorClient::lockState(TickType_t timeout) const
{
    return m_stateMutex && xSemaphoreTake(m_stateMutex, timeout) == pdTRUE;
}

void OrchestratorClient::unlockState() const
{
    if (m_stateMutex) {
        xSemaphoreGive(m_stateMutex);
    }
}

void OrchestratorClient::setState(ConnectionState newState)
{
    ConnectionStateCallback callback;
    bool changed = false;

    if (lockState(pdMS_TO_TICKS(100))) {
        changed = (m_state != newState);
        m_state = newState;
        callback = m_connectionCallback;
        unlockState();
    } else {
        // Never let a lock timeout lose a state change: the UI's view of
        // whether the link is up gates whether knobs are live.
        m_state = newState;
        callback = m_connectionCallback;
        changed = true;
    }

    if (changed) {
        ESP_LOGI(TAG, "Connection state: %s", stateName(newState));
        if (callback) {
            callback(newState);
        }
    }
}

bool OrchestratorClient::isConnected() const
{
    return getState() == ConnectionState::CONNECTED;
}

OrchestratorClient::ConnectionState OrchestratorClient::getState() const
{
    ConnectionState state = ConnectionState::DISCONNECTED;
    if (lockState(pdMS_TO_TICKS(50))) {
        state = m_state;
        unlockState();
    } else {
        state = m_state;
    }
    return state;
}

OrchestratorClient::SystemStatus OrchestratorClient::getSystemStatus() const
{
    SystemStatus status = SystemStatus::UNKNOWN;
    if (lockState(pdMS_TO_TICKS(50))) {
        status = m_systemStatus;
        unlockState();
    }
    return status;
}

uint32_t OrchestratorClient::secondsSinceLastMessage() const
{
    int64_t last = 0;
    if (lockState(pdMS_TO_TICKS(50))) {
        last = m_lastMessageUs;
        unlockState();
    }
    if (last == 0) {
        return 0;
    }
    return static_cast<uint32_t>((esp_timer_get_time() - last) / 1000000);
}

// --- Connecting ------------------------------------------------------------

esp_err_t OrchestratorClient::connect(const std::string& host,
                                      uint16_t port,
                                      const std::string& username,
                                      const std::string& password)
{
    if (host.empty()) {
        ESP_LOGE(TAG, "Refusing to connect: no host configured");
        return ESP_ERR_INVALID_ARG;
    }
    if (username.empty() || password.empty()) {
        ESP_LOGE(TAG, "Refusing to connect: no operator credential configured");
        return ESP_ERR_INVALID_ARG;
    }

    const ConnectionState current = getState();
    if (current == ConnectionState::CONNECTING || current == ConnectionState::AUTHENTICATING) {
        ESP_LOGW(TAG, "Already connecting");
        return ESP_ERR_INVALID_STATE;
    }

    disconnect();

    m_host = host;
    m_port = port;

    setState(ConnectionState::AUTHENTICATING);
    esp_err_t err = authenticate(host, port, username, password);
    if (err != ESP_OK) {
        setState(ConnectionState::FAILED);
        return err;
    }

    setState(ConnectionState::CONNECTING);
    err = openSocket(host, port);
    if (err != ESP_OK) {
        setState(ConnectionState::FAILED);
    }
    return err;
}

esp_err_t OrchestratorClient::authenticate(const std::string& host,
                                           uint16_t port,
                                           const std::string& username,
                                           const std::string& password)
{
    const std::string url = "http://" + host + ":" + std::to_string(port) + "/api/auth/login";

    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "username", username.c_str());
    cJSON_AddStringToObject(root, "password", password.c_str());
    char* bodyRaw = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!bodyRaw) {
        return ESP_ERR_NO_MEM;
    }
    const std::string body = bodyRaw;
    cJSON_free(bodyRaw);

    HttpCapture capture;
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = HTTP_TIMEOUT_MS;
    cfg.event_handler = httpEventHandler;
    cfg.user_data = &capture;
    cfg.disable_auto_redirect = true;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "Failed to create HTTP client for login");
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body.c_str(), static_cast<int>(body.size()));

    // The password is in `body`; never log it, and never log the response
    // headers either -- the session cookie is a bearer credential.
    ESP_LOGI(TAG, "Authenticating to %s as '%s'", url.c_str(), username.c_str());

    esp_err_t err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Login request failed: %s", esp_err_to_name(err));
        return err;
    }

    if (status == 401) {
        ESP_LOGE(TAG, "Login rejected: bad operator credential");
        return ESP_ERR_INVALID_STATE;
    }
    if (status == 429) {
        ESP_LOGE(TAG, "Login rate-limited (5/min); backing off");
        return ESP_ERR_INVALID_STATE;
    }
    if (status != 200) {
        ESP_LOGE(TAG, "Login returned HTTP %d", status);
        return ESP_FAIL;
    }

    std::string token;
    if (!extractSessionCookie(capture.setCookie, token)) {
        ESP_LOGE(TAG, "Login succeeded but no %s cookie was returned", SESSION_COOKIE_NAME);
        return ESP_ERR_NOT_FOUND;
    }

    if (lockState(pdMS_TO_TICKS(200))) {
        m_sessionToken = token;
        unlockState();
    } else {
        m_sessionToken = token;
    }

    ESP_LOGI(TAG, "Authenticated; session cookie acquired");
    return ESP_OK;
}

bool OrchestratorClient::extractSessionCookie(const std::string& setCookieHeader,
                                                  std::string& outToken)
{
    // Set-Cookie: layout_session=<token>; Path=/; HttpOnly; SameSite=Lax
    // Only the first name=value pair is the cookie; everything after the first
    // ';' is attributes and must not be mistaken for part of the token.
    const std::string needle = std::string(SESSION_COOKIE_NAME) + "=";
    const size_t start = setCookieHeader.find(needle);
    if (start == std::string::npos) {
        return false;
    }

    const size_t valueStart = start + needle.size();
    size_t valueEnd = setCookieHeader.find(';', valueStart);
    if (valueEnd == std::string::npos) {
        valueEnd = setCookieHeader.size();
    }

    outToken = setCookieHeader.substr(valueStart, valueEnd - valueStart);
    return !outToken.empty();
}

esp_err_t OrchestratorClient::openSocket(const std::string& host, uint16_t port)
{
    m_uri = "ws://" + host + ":" + std::to_string(port) + "/ws";

    // esp_websocket_client keeps the pointer rather than copying, so this must
    // outlive the socket -- hence a member, not a local.
    m_cookieHeader = "Cookie: " + std::string(SESSION_COOKIE_NAME) + "=" + m_sessionToken + "\r\n";

    esp_websocket_client_config_t cfg = {};
    cfg.uri = m_uri.c_str();
    cfg.headers = m_cookieHeader.c_str();
    cfg.reconnect_timeout_ms = 10000;
    cfg.network_timeout_ms = 10000;
    // The server sends an application-level HEARTBEAT of its own; this is the
    // protocol-level keepalive that stops a NAT dropping an idle socket.
    cfg.ping_interval_sec = 20;
    cfg.disable_auto_reconnect = false;
    cfg.task_stack = 6144;
    // Frame buffer only. It does NOT size the HTTP Upgrade handshake -- that
    // is CONFIG_WS_BUFFER_SIZE in sdkconfig.defaults, and it is what
    // "transport_ws: Header size exceeded buffer size" is complaining about.
    // Raising this instead does nothing for the handshake.
    //
    // A whole-layout STATE_SNAPSHOT is larger than this and arrives split
    // across several events; m_rxBuffer reassembles it.
    cfg.buffer_size = 4096;

    m_client = esp_websocket_client_init(&cfg);
    if (!m_client) {
        ESP_LOGE(TAG, "Failed to create WebSocket client");
        return ESP_FAIL;
    }

    esp_websocket_register_events(m_client, WEBSOCKET_EVENT_ANY, websocketEventHandler, this);

    ESP_LOGI(TAG, "Opening control plane at %s", m_uri.c_str());
    esp_err_t err = esp_websocket_client_start(m_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WebSocket client: %s", esp_err_to_name(err));
        esp_websocket_client_destroy(m_client);
        m_client = nullptr;
        return err;
    }
    return ESP_OK;
}

void OrchestratorClient::disconnect()
{
    if (m_client) {
        ESP_LOGI(TAG, "Disconnecting from orchestrator");
        esp_websocket_client_stop(m_client);
        esp_websocket_client_destroy(m_client);
        m_client = nullptr;
    }

    if (lockState(pdMS_TO_TICKS(200))) {
        m_rxBuffer.clear();
        m_sessionToken.clear();
        unlockState();
    } else {
        m_rxBuffer.clear();
        m_sessionToken.clear();
    }

    setState(ConnectionState::DISCONNECTED);
}

// --- Receiving -------------------------------------------------------------

void OrchestratorClient::websocketEventHandler(void* handlerArgs,
                                               const char* /*base*/,
                                               int32_t eventId,
                                               void* eventData)
{
    auto* self = static_cast<OrchestratorClient*>(handlerArgs);
    auto* data = static_cast<esp_websocket_event_data_t*>(eventData);
    if (!self || !data) {
        return;
    }

    switch (eventId) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Control plane connected");
            self->setState(ConnectionState::CONNECTED);
            break;

        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Control plane disconnected");
            self->setState(ConnectionState::DISCONNECTED);
            break;

        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "Control plane error");
            self->setState(ConnectionState::FAILED);
            break;

        case WEBSOCKET_EVENT_DATA: {
            // Opcode 1 is text. Ignore ping/pong/close frames, and ignore
            // binary: the control plane is JSON text only.
            if (data->op_code != 0x01 && data->op_code != 0x00) {
                break;
            }
            if (data->data_len <= 0 || !data->data_ptr) {
                break;
            }

            // A frame larger than the receive buffer arrives in several events,
            // with payload_offset telling us where each piece belongs.
            if (data->payload_offset == 0) {
                self->m_rxBuffer.clear();
            }

            if (self->m_rxBuffer.size() + data->data_len > MAX_FRAME_BYTES) {
                ESP_LOGW(TAG, "Dropping oversized frame (>%u bytes)",
                         static_cast<unsigned>(MAX_FRAME_BYTES));
                self->m_rxBuffer.clear();
                break;
            }

            self->m_rxBuffer.append(data->data_ptr, data->data_len);

            // Only parse once the whole payload has arrived.
            if (data->payload_len > 0 &&
                self->m_rxBuffer.size() < static_cast<size_t>(data->payload_len)) {
                break;
            }

            std::string frame;
            frame.swap(self->m_rxBuffer);
            self->handleMessage(frame);
            break;
        }

        default:
            break;
    }
}

void OrchestratorClient::handleMessage(const std::string& json)
{
    if (lockState(pdMS_TO_TICKS(50))) {
        m_lastMessageUs = esp_timer_get_time();
        unlockState();
    }

    cJSON* root = cJSON_Parse(json.c_str());
    if (!root) {
        // Refused, not guessed at. A frame we cannot parse tells us nothing,
        // and half-reading one is how a garbled message becomes a speed command.
        ESP_LOGW(TAG, "Refusing unparseable frame (%u bytes)",
                 static_cast<unsigned>(json.size()));
        return;
    }

    std::string type;
    if (!jsonString(root, "type", type)) {
        ESP_LOGW(TAG, "Refusing frame with no type field");
        cJSON_Delete(root);
        return;
    }

    const cJSON* payload = cJSON_GetObjectItemCaseSensitive(root, "payload");

    if (type == "HEARTBEAT") {
        // Liveness only; the timestamp above is the whole point of it.
    } else if (type == "STATE_SNAPSHOT") {
        handleStateSnapshot(payload);
    } else if (type == "LOCO_STATE") {
        handleLocoState(payload);
    } else if (type == "SYSTEM_STATUS") {
        handleSystemStatus(payload);
    } else if (type == "DCC_LINK") {
        handleDccLink(payload);
    } else if (type == "ERROR") {
        std::string message;
        if (payload && jsonString(payload, "message", message)) {
            ESP_LOGE(TAG, "Orchestrator refused a command: %s", message.c_str());
        } else {
            ESP_LOGE(TAG, "Orchestrator reported an error with no message");
        }
    } else {
        // Blocks, points, routes, sensors and faults are all real messages this
        // device has no use for. Not an error -- just not ours.
        ESP_LOGD(TAG, "Ignoring %s", type.c_str());
    }

    cJSON_Delete(root);
}

void OrchestratorClient::handleStateSnapshot(const void* payloadPtr)
{
    const cJSON* payload = static_cast<const cJSON*>(payloadPtr);
    if (!cJSON_IsObject(payload)) {
        ESP_LOGW(TAG, "Refusing STATE_SNAPSHOT with no payload object");
        return;
    }

    std::string statusWire;
    if (jsonString(payload, "systemStatus", statusWire)) {
        SystemStatus status = SystemStatus::UNKNOWN;
        if (wireToSystemStatus(statusWire, status)) {
            std::string reason;
            const cJSON* reasonItem =
                cJSON_GetObjectItemCaseSensitive(payload, "safeStopReason");
            if (cJSON_IsString(reasonItem) && reasonItem->valuestring) {
                reason = reasonItem->valuestring;
            }

            SystemStatusCallback callback;
            if (lockState(pdMS_TO_TICKS(50))) {
                m_systemStatus = status;
                callback = m_systemStatusCallback;
                unlockState();
            }
            if (callback) {
                callback(status, reason);
            }
        }
    }

    // Track power rides in on the snapshot too, so the button is right from
    // the first frame rather than waiting for the next DCC_LINK.
    const cJSON* dccLink = cJSON_GetObjectItemCaseSensitive(payload, "dccLink");
    if (cJSON_IsObject(dccLink)) {
        handleDccLink(dccLink);
    }

    // `locos` is an object keyed by address. This is the layout's belief about
    // what is already moving -- it updates what we display, and is never
    // replayed outward as a command.
    const cJSON* locos = cJSON_GetObjectItemCaseSensitive(payload, "locos");
    if (!cJSON_IsObject(locos)) {
        return;
    }

    int applied = 0;
    const cJSON* entry = nullptr;
    cJSON_ArrayForEach(entry, locos) {
        handleLocoState(entry);
        applied++;
    }
    ESP_LOGI(TAG, "Snapshot applied: %d loco states, system %s", applied, statusWire.c_str());
}

void OrchestratorClient::handleLocoState(const void* payloadPtr)
{
    const cJSON* payload = static_cast<const cJSON*>(payloadPtr);
    if (!cJSON_IsObject(payload)) {
        ESP_LOGW(TAG, "Refusing LOCO_STATE with no payload object");
        return;
    }

    // Every field is validated before any of it is applied. A LocoState that
    // fails halfway is discarded whole.
    LocoState state;

    if (!jsonInt(payload, "address", state.address) || state.address <= 0) {
        ESP_LOGW(TAG, "Refusing LOCO_STATE with a missing or invalid address");
        return;
    }

    if (!jsonInt(payload, "speed", state.speed)) {
        ESP_LOGW(TAG, "Refusing LOCO_STATE for loco %d: no valid speed", state.address);
        return;
    }
    if (state.speed < 0 || state.speed > 126) {
        ESP_LOGW(TAG, "Refusing LOCO_STATE for loco %d: speed %d out of range 0-126",
                 state.address, state.speed);
        return;
    }

    std::string directionWire;
    if (!jsonString(payload, "direction", directionWire) ||
        !wireToDirection(directionWire, state.direction)) {
        ESP_LOGW(TAG, "Refusing LOCO_STATE for loco %d: bad direction '%s'",
                 state.address, directionWire.c_str());
        return;
    }

    const cJSON* functions = cJSON_GetObjectItemCaseSensitive(payload, "functions");
    if (cJSON_IsObject(functions)) {
        const cJSON* fn = nullptr;
        cJSON_ArrayForEach(fn, functions) {
            if (!fn->string || !cJSON_IsBool(fn)) {
                continue;
            }
            char* end = nullptr;
            const long number = strtol(fn->string, &end, 10);
            if (end == fn->string || *end != '\0' || number < 0 || number > 28) {
                continue;
            }
            state.functions[static_cast<int>(number)] = cJSON_IsTrue(fn);
        }
    }

    LocoStateCallback callback;
    if (lockState(pdMS_TO_TICKS(50))) {
        callback = m_locoStateCallback;
        unlockState();
    } else {
        callback = m_locoStateCallback;
    }

    if (callback) {
        callback(state);
    }
}

void OrchestratorClient::handleDccLink(const void* payloadPtr)
{
    const cJSON* payload = static_cast<const cJSON*>(payloadPtr);
    if (!cJSON_IsObject(payload)) {
        ESP_LOGW(TAG, "Refusing DCC_LINK with no payload object");
        return;
    }

    // `mainPowerOn` is boolean-or-null by contract: null means the command
    // station has not said. That is UNKNOWN, not off -- reporting it as off
    // would tell the operator the rails are dead when nobody knows.
    const cJSON* mainPower = cJSON_GetObjectItemCaseSensitive(payload, "mainPowerOn");

    TrackPower power = TrackPower::UNKNOWN;
    if (cJSON_IsBool(mainPower)) {
        power = cJSON_IsTrue(mainPower) ? TrackPower::ON : TrackPower::OFF;
    } else if (!cJSON_IsNull(mainPower) && mainPower != nullptr) {
        ESP_LOGW(TAG, "Refusing DCC_LINK: mainPowerOn is neither boolean nor null");
        return;
    }

    TrackPowerCallback callback;
    bool changed = false;
    if (lockState(pdMS_TO_TICKS(50))) {
        changed = (m_trackPower != power);
        m_trackPower = power;
        callback = m_trackPowerCallback;
        unlockState();
    } else {
        changed = (m_trackPower != power);
        m_trackPower = power;
        callback = m_trackPowerCallback;
    }

    if (changed) {
        ESP_LOGI(TAG, "Track power: %s",
                 power == TrackPower::ON ? "on"
                     : power == TrackPower::OFF ? "off" : "unknown");
        if (callback) {
            callback(power);
        }
    }
}

OrchestratorClient::TrackPower OrchestratorClient::getTrackPower() const
{
    TrackPower power = TrackPower::UNKNOWN;
    if (lockState(pdMS_TO_TICKS(50))) {
        power = m_trackPower;
        unlockState();
    }
    return power;
}

esp_err_t OrchestratorClient::setTrackPower(bool on)
{
    std::string token;
    std::string host;
    uint16_t port = DEFAULT_PORT;
    if (lockState(pdMS_TO_TICKS(200))) {
        token = m_sessionToken;
        host = m_host;
        port = m_port;
        unlockState();
    }

    if (token.empty() || host.empty()) {
        ESP_LOGW(TAG, "Cannot set track power before authenticating");
        return ESP_ERR_INVALID_STATE;
    }

    const std::string layoutId = getLayoutId();
    if (layoutId.empty()) {
        ESP_LOGE(TAG, "Cannot set track power: no layout id");
        return ESP_ERR_INVALID_STATE;
    }

    const std::string url = "http://" + host + ":" + std::to_string(port) +
                            "/api/layouts/" + layoutId + "/dcc-link/power";
    const std::string cookie = std::string(SESSION_COOKIE_NAME) + "=" + token;
    const std::string body = on ? "{\"on\":true}" : "{\"on\":false}";

    HttpCapture capture;
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = HTTP_TIMEOUT_MS;
    cfg.event_handler = httpEventHandler;
    cfg.user_data = &capture;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return ESP_FAIL;
    }
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Cookie", cookie.c_str());
    esp_http_client_set_post_field(client, body.c_str(), static_cast<int>(body.size()));

    esp_err_t err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Track power request failed: %s", esp_err_to_name(err));
        return err;
    }
    if (status == 403) {
        ESP_LOGE(TAG, "Track power refused: this credential may not drive");
        return ESP_ERR_INVALID_STATE;
    }
    if (status != 200) {
        ESP_LOGE(TAG, "Track power returned HTTP %d", status);
        return ESP_FAIL;
    }

    // The reply body is deliberately ignored. The DCC_LINK event pushed the
    // moment this lands is what tells us the truth, and it arrives on the
    // socket like any other state change.
    ESP_LOGI(TAG, "Track power %s requested", on ? "on" : "off");
    return ESP_OK;
}

std::string OrchestratorClient::getLayoutId()
{
    if (lockState(pdMS_TO_TICKS(200))) {
        if (!m_layoutId.empty()) {
            const std::string cached = m_layoutId;
            unlockState();
            return cached;
        }
        unlockState();
    }

    std::string token;
    std::string host;
    uint16_t port = DEFAULT_PORT;
    if (lockState(pdMS_TO_TICKS(200))) {
        token = m_sessionToken;
        host = m_host;
        port = m_port;
        unlockState();
    }
    if (token.empty() || host.empty()) {
        return std::string();
    }

    const std::string url = "http://" + host + ":" + std::to_string(port) + "/api/layouts";
    const std::string cookie = std::string(SESSION_COOKIE_NAME) + "=" + token;

    HttpCapture capture;
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.method = HTTP_METHOD_GET;
    cfg.timeout_ms = HTTP_TIMEOUT_MS;
    cfg.event_handler = httpEventHandler;
    cfg.user_data = &capture;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return std::string();
    }
    esp_http_client_set_header(client, "Cookie", cookie.c_str());
    esp_err_t err = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "Failed to list layouts (HTTP %d)", status);
        return std::string();
    }

    cJSON* layouts = cJSON_Parse(capture.body.c_str());
    if (!cJSON_IsArray(layouts) || cJSON_GetArraySize(layouts) == 0) {
        ESP_LOGE(TAG, "Refusing layout list: not a non-empty array");
        if (layouts) cJSON_Delete(layouts);
        return std::string();
    }

    std::string layoutId;
    const bool haveId = jsonString(cJSON_GetArrayItem(layouts, 0), "id", layoutId);
    cJSON_Delete(layouts);
    if (!haveId || layoutId.empty()) {
        ESP_LOGE(TAG, "Refusing layout list: first entry has no id");
        return std::string();
    }

    if (lockState(pdMS_TO_TICKS(200))) {
        m_layoutId = layoutId;
        unlockState();
    }
    return layoutId;
}

void OrchestratorClient::handleSystemStatus(const void* payloadPtr)
{
    const cJSON* payload = static_cast<const cJSON*>(payloadPtr);
    if (!cJSON_IsObject(payload)) {
        ESP_LOGW(TAG, "Refusing SYSTEM_STATUS with no payload object");
        return;
    }

    std::string statusWire;
    SystemStatus status = SystemStatus::UNKNOWN;
    if (!jsonString(payload, "status", statusWire) ||
        !wireToSystemStatus(statusWire, status)) {
        ESP_LOGW(TAG, "Refusing SYSTEM_STATUS with unknown status '%s'", statusWire.c_str());
        return;
    }

    std::string reason;
    const cJSON* reasonItem = cJSON_GetObjectItemCaseSensitive(payload, "reason");
    if (cJSON_IsString(reasonItem) && reasonItem->valuestring) {
        reason = reasonItem->valuestring;
    }

    SystemStatusCallback callback;
    if (lockState(pdMS_TO_TICKS(50))) {
        m_systemStatus = status;
        callback = m_systemStatusCallback;
        unlockState();
    }

    ESP_LOGI(TAG, "System status: %s%s%s", statusWire.c_str(),
             reason.empty() ? "" : " -- ", reason.c_str());

    if (callback) {
        callback(status, reason);
    }
}

#if CONFIG_THROTTLE_TESTS
void OrchestratorClient::testHandleMessage(const std::string& json)
{
    handleMessage(json);
}
#endif

// --- Sending ---------------------------------------------------------------

esp_err_t OrchestratorClient::sendJson(const std::string& json)
{
    if (!m_client || !isConnected()) {
        ESP_LOGW(TAG, "Not connected to the orchestrator");
        return ESP_ERR_INVALID_STATE;
    }

    const int sent = esp_websocket_client_send_text(
        m_client, json.c_str(), static_cast<int>(json.size()), pdMS_TO_TICKS(1000));

    if (sent < 0) {
        ESP_LOGE(TAG, "Failed to send frame");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t OrchestratorClient::sendThrottleCommand(int locoAddress, int speed, Direction direction)
{
    if (locoAddress <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    // Clamp rather than refuse: a knob that has run past the end of its range
    // should still stop the loco, not silently drop the command.
    if (speed < 0) speed = 0;
    if (speed > 126) speed = 126;

    cJSON* root = cJSON_CreateObject();
    cJSON* payload = cJSON_CreateObject();
    if (!root || !payload) {
        if (root) cJSON_Delete(root);
        if (payload) cJSON_Delete(payload);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "type", "THROTTLE_COMMAND");
    cJSON_AddNumberToObject(payload, "locoAddress", locoAddress);
    cJSON_AddNumberToObject(payload, "speed", speed);
    cJSON_AddStringToObject(payload, "direction", directionToWire(direction));
    cJSON_AddItemToObject(root, "payload", payload);

    char* raw = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!raw) {
        return ESP_ERR_NO_MEM;
    }
    const std::string json = raw;
    cJSON_free(raw);

    ESP_LOGD(TAG, "THROTTLE_COMMAND loco %d speed %d %s",
             locoAddress, speed, directionToWire(direction));
    return sendJson(json);
}

esp_err_t OrchestratorClient::sendFunctionCommand(int locoAddress, int function, bool state)
{
    if (locoAddress <= 0 || function < 0 || function > 28) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON* payload = cJSON_CreateObject();
    if (!root || !payload) {
        if (root) cJSON_Delete(root);
        if (payload) cJSON_Delete(payload);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "type", "FUNCTION_COMMAND");
    cJSON_AddNumberToObject(payload, "locoAddress", locoAddress);
    cJSON_AddNumberToObject(payload, "fn", function);
    cJSON_AddBoolToObject(payload, "state", state);
    cJSON_AddItemToObject(root, "payload", payload);

    char* raw = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!raw) {
        return ESP_ERR_NO_MEM;
    }
    const std::string json = raw;
    cJSON_free(raw);

    return sendJson(json);
}

esp_err_t OrchestratorClient::sendEmergencyStop()
{
    // No payload by contract. Sent even when the link looks unhealthy -- the
    // one command where trying and failing beats not trying.
    ESP_LOGW(TAG, "Sending EMERGENCY_STOP");
    return sendJson("{\"type\":\"EMERGENCY_STOP\"}");
}

// --- Roster ----------------------------------------------------------------

esp_err_t OrchestratorClient::refreshRoster()
{
    std::string token;
    std::string host;
    uint16_t port = DEFAULT_PORT;
    if (lockState(pdMS_TO_TICKS(200))) {
        token = m_sessionToken;
        host = m_host;
        port = m_port;
        unlockState();
    }

    if (token.empty() || host.empty()) {
        ESP_LOGW(TAG, "Cannot fetch roster before authenticating");
        return ESP_ERR_INVALID_STATE;
    }

    const std::string base = "http://" + host + ":" + std::to_string(port);
    const std::string cookie = std::string(SESSION_COOKIE_NAME) + "=" + token;

    // The control plane carries loco state keyed by address but no names, so
    // the roster is a REST read. It needs a layout id, which getLayoutId()
    // fetches once and caches -- the power POST needs the same id.
    const std::string layoutId = getLayoutId();
    if (layoutId.empty()) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    esp_http_client_handle_t client = nullptr;
    esp_err_t err = ESP_OK;
    int status = 0;

    HttpCapture locosCapture;
    const std::string locosUrl = base + "/api/layouts/" + layoutId + "/locos";
    esp_http_client_config_t locosCfg = {};
    locosCfg.url = locosUrl.c_str();
    locosCfg.method = HTTP_METHOD_GET;
    locosCfg.timeout_ms = HTTP_TIMEOUT_MS;
    locosCfg.event_handler = httpEventHandler;
    locosCfg.user_data = &locosCapture;

    client = esp_http_client_init(&locosCfg);
    if (!client) {
        return ESP_FAIL;
    }
    esp_http_client_set_header(client, "Cookie", cookie.c_str());
    err = esp_http_client_perform(client);
    status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "Failed to fetch roster (HTTP %d)", status);
        return (err != ESP_OK) ? err : ESP_FAIL;
    }
    if (locosCapture.truncated) {
        ESP_LOGE(TAG, "Refusing truncated roster response");
        return ESP_ERR_INVALID_SIZE;
    }

    cJSON* locos = cJSON_Parse(locosCapture.body.c_str());
    if (!cJSON_IsArray(locos)) {
        ESP_LOGE(TAG, "Refusing roster: not an array");
        if (locos) cJSON_Delete(locos);
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Built aside and swapped in, so a partly-built roster is never visible to
    // the carousel.
    std::vector<RosterEntry> roster;
    const cJSON* item = nullptr;
    cJSON_ArrayForEach(item, locos) {
        RosterEntry entry;
        if (!jsonInt(item, "address", entry.address) || entry.address <= 0) {
            continue;
        }
        if (!jsonString(item, "name", entry.name) || entry.name.empty()) {
            entry.name = "Loco " + std::to_string(entry.address);
        }
        roster.push_back(entry);
    }
    cJSON_Delete(locos);

    RosterCallback callback;
    if (lockState(pdMS_TO_TICKS(200))) {
        m_roster.swap(roster);
        callback = m_rosterCallback;
        unlockState();
    } else {
        ESP_LOGW(TAG, "Could not lock to publish roster");
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGI(TAG, "Roster loaded: %u locomotives", static_cast<unsigned>(getRosterSize()));

    if (callback) {
        std::vector<RosterEntry> copy;
        if (lockState(pdMS_TO_TICKS(100))) {
            copy = m_roster;
            unlockState();
        }
        callback(copy);
    }
    return ESP_OK;
}

size_t OrchestratorClient::getRosterSize() const
{
    size_t size = 0;
    if (lockState(pdMS_TO_TICKS(50))) {
        size = m_roster.size();
        unlockState();
    }
    return size;
}

bool OrchestratorClient::getRosterEntry(int index, RosterEntry& outEntry) const
{
    bool found = false;
    if (lockState(pdMS_TO_TICKS(50))) {
        if (index >= 0 && static_cast<size_t>(index) < m_roster.size()) {
            outEntry = m_roster[index];
            found = true;
        }
        unlockState();
    }
    return found;
}

// --- Callbacks -------------------------------------------------------------

void OrchestratorClient::setConnectionStateCallback(ConnectionStateCallback callback)
{
    if (lockState(pdMS_TO_TICKS(100))) {
        m_connectionCallback = std::move(callback);
        unlockState();
    }
}

void OrchestratorClient::setLocoStateCallback(LocoStateCallback callback)
{
    if (lockState(pdMS_TO_TICKS(100))) {
        m_locoStateCallback = std::move(callback);
        unlockState();
    }
}

void OrchestratorClient::setSystemStatusCallback(SystemStatusCallback callback)
{
    if (lockState(pdMS_TO_TICKS(100))) {
        m_systemStatusCallback = std::move(callback);
        unlockState();
    }
}

void OrchestratorClient::setRosterCallback(RosterCallback callback)
{
    if (lockState(pdMS_TO_TICKS(100))) {
        m_rosterCallback = std::move(callback);
        unlockState();
    }
}

void OrchestratorClient::setTrackPowerCallback(TrackPowerCallback callback)
{
    if (lockState(pdMS_TO_TICKS(100))) {
        m_trackPowerCallback = std::move(callback);
        unlockState();
    } else {
        m_trackPowerCallback = std::move(callback);
    }
}

const char* OrchestratorClient::stateName(ConnectionState state)
{
    switch (state) {
        case ConnectionState::DISCONNECTED:   return "disconnected";
        case ConnectionState::AUTHENTICATING: return "authenticating";
        case ConnectionState::CONNECTING:     return "connecting";
        case ConnectionState::CONNECTED:      return "connected";
        case ConnectionState::FAILED:         return "failed";
        default:                              return "unknown";
    }
}

const char* OrchestratorClient::directionName(Direction direction)
{
    return directionToWire(direction);
}
