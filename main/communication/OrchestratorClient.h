#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "esp_err.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "CallbackSlot.h"

/**
 * @brief Client for the layout orchestrator's WebSocket control plane.
 *
 * Speaks the ClientMessage / ServerMessage vocabulary defined in
 * bazauto/layout-orchestration -> packages/backend/src/domain/types.ts, which is
 * authoritative for this firmware. This is an operator device and the WebSocket
 * is the operator control plane; MQTT is the hardware telemetry bus and is not
 * this device's transport.
 *
 * Connecting is two steps, because the orchestrator authenticates with a session
 * cookie rather than a bearer token:
 *
 *   1. POST /api/auth/login with the credentials, and capture the session token
 *      from the Set-Cookie response header.
 *   2. Open /ws with that token sent back as a Cookie request header.
 *
 * Auth is enforced only at the upgrade. Once the socket is open nothing tears it
 * down for an auth reason, which is deliberate on the server side: a session
 * expiring must never drop a connection while a train is moving.
 *
 * Threading: the WebSocket runs its own task and callbacks fire on it. Take
 * lvgl_port_lock before touching a widget from one. Reads of connection state
 * and of the cached roster are mutex-guarded and safe from any task.
 */
class OrchestratorClient {
public:
    static constexpr uint16_t DEFAULT_PORT = 3000;

    /** Session cookie name, fixed by the orchestrator's config.ts. */
    static constexpr const char* SESSION_COOKIE_NAME = "layout_session";

    enum class ConnectionState {
        DISCONNECTED,
        AUTHENTICATING,   ///< POSTing credentials, no socket yet
        CONNECTING,       ///< Have a cookie, upgrading
        CONNECTED,
        FAILED
    };

    /** Mirrors the orchestrator's Direction: 'fwd' | 'rev' | 'stop'. */
    enum class Direction {
        FORWARD,
        REVERSE,
        STOP
    };

    /** Main-track power, from `DccLinkView.mainPowerOn` (null = unknown). */
    enum class TrackPower {
        OFF,
        ON,
        UNKNOWN
    };

    /** Mirrors SystemStatus: 'online' | 'safe-stop' | 'offline'. */
    enum class SystemStatus {
        ONLINE,
        SAFE_STOP,
        OFFLINE,
        UNKNOWN
    };

    /**
     * @brief A loco's state as the orchestrator believes it to be.
     *
     * Commanded, never confirmed -- there is no loco feedback channel. This is
     * what the layout thinks it told the loco, which is the best truth available
     * and is emphatically not an instruction to this device.
     */
    struct LocoState {
        int address = 0;
        int speed = 0;                       ///< DCC step 0-126
        Direction direction = Direction::STOP;
        std::map<int, bool> functions;
    };

    /** One roster entry, from GET /api/layouts/{id}/locos. */
    struct RosterEntry {
        int address = 0;
        std::string name;
    };

    using ConnectionStateCallback = std::function<void(ConnectionState state)>;
    using LocoStateCallback = std::function<void(const LocoState& state)>;
    using SystemStatusCallback = std::function<void(SystemStatus status, const std::string& reason)>;
    using RosterCallback = std::function<void(const std::vector<RosterEntry>& roster)>;
    using TrackPowerCallback = std::function<void(TrackPower state)>;
    /** An ERROR frame: the orchestrator refused a command. Not tied to any one. */
    using CommandRefusedCallback = std::function<void(const std::string& message)>;

    OrchestratorClient();
    ~OrchestratorClient();

    OrchestratorClient(const OrchestratorClient&) = delete;
    OrchestratorClient& operator=(const OrchestratorClient&) = delete;

    esp_err_t initialize();

    /**
     * @brief Log in and open the control-plane socket.
     *
     * Blocking on the HTTP login (a second or so), then asynchronous: the socket
     * reports readiness through the connection-state callback. Must not be
     * called from an LVGL event handler.
     */
    esp_err_t connect(const std::string& host,
                      uint16_t port,
                      const std::string& username,
                      const std::string& password);

    void disconnect();

    bool isConnected() const;
    ConnectionState getState() const;

    /** Empty until a STATE_SNAPSHOT or SYSTEM_STATUS has arrived. */
    SystemStatus getSystemStatus() const;

    // --- Outbound: ClientMessage ------------------------------------------

    /**
     * @brief THROTTLE_COMMAND.
     *
     * Speed and direction travel together in one message -- the contract has no
     * speed-only command -- so callers must supply both, every time.
     */
    esp_err_t sendThrottleCommand(int locoAddress, int speed, Direction direction);

    /** FUNCTION_COMMAND. */
    esp_err_t sendFunctionCommand(int locoAddress, int function, bool state);

    /**
     * @brief EMERGENCY_STOP.
     *
     * Deliberately the one command with no loco address: it halts the layout,
     * not a loco. Every role may send it.
     */
    esp_err_t sendEmergencyStop();

    // --- Track power ------------------------------------------------------

    /**
     * @brief Turn main-track power on or off.
     *
     * `POST /api/layouts/{id}/dcc-link/power` — a REST call, because the
     * control plane's `ClientMessage` union has no track-power member. The
     * reply body is a courtesy; what tells us the truth is the `DCC_LINK`
     * event pushed the moment it lands, so this does not parse the response.
     *
     * Blocking HTTP: never call from an LVGL event handler (F-05).
     */
    esp_err_t setTrackPower(bool on);

    /** UNKNOWN until a DCC_LINK or snapshot has said otherwise. */
    TrackPower getTrackPower() const;

    // --- Roster (REST, not WebSocket) -------------------------------------

    /**
     * @brief Fetch the roster over HTTP and cache it.
     *
     * The control plane carries loco *state* keyed by address but no names, so
     * the roster is a separate REST read. Called once after connecting.
     */
    esp_err_t refreshRoster();

    size_t getRosterSize() const;
    bool getRosterEntry(int index, RosterEntry& outEntry) const;

    // --- Last reported loco state -------------------------------------------

    /**
     * @brief The layout's last reported state for a loco, if any.
     *
     * Filled from STATE_SNAPSHOT (which replaces it wholesale) and LOCO_STATE,
     * emptied when the link drops. Read when a throttle takes a loco over, so
     * the display -- and the knob arithmetic that starts from it -- begins from
     * what the loco is doing rather than from zero (F-20).
     *
     * Display-side only, like the snapshot it comes from: nothing read here is
     * ever sent back out as a command.
     *
     * @return false when the layout has not reported that loco.
     */
    bool getLastLocoState(int address, LocoState& outState) const;

    // --- Notifications ----------------------------------------------------

    void setConnectionStateCallback(ConnectionStateCallback callback);
    void setLocoStateCallback(LocoStateCallback callback);
    void setSystemStatusCallback(SystemStatusCallback callback);
    void setRosterCallback(RosterCallback callback);
    void setTrackPowerCallback(TrackPowerCallback callback);

    /**
     * @brief Notified of every ERROR frame.
     *
     * The orchestrator answers a refused command -- one sent while the system
     * is offline, say -- with an ERROR that names no command. The display may
     * already show what was asked for, so the listener's job is to put back
     * what the layout last reported (F-25).
     */
    void setCommandRefusedCallback(CommandRefusedCallback callback);

    /** Seconds since the last message of any kind. Large means a stale link. */
    uint32_t secondsSinceLastMessage() const;

    /**
     * @brief Pull the session token out of a Set-Cookie header value.
     *
     * Public and always compiled, not test-only: `authenticate` is its real
     * caller. A pure function, which is also what makes it worth testing
     * directly.
     *
     * @return false when the header carries no session cookie.
     */
    static bool extractSessionCookie(const std::string& setCookieHeader, std::string& outToken);

#if CONFIG_THROTTLE_TESTS
    /** Test-only: feed a raw ServerMessage frame through the parser. */
    void testHandleMessage(const std::string& json);
#endif

    static const char* stateName(ConnectionState state);
    static const char* directionName(Direction direction);

private:
    bool lockState(TickType_t timeout) const;
    void unlockState() const;
    void setState(ConnectionState newState);

    /** POST /api/auth/login. Fills m_sessionToken on success. */
    esp_err_t authenticate(const std::string& host,
                           uint16_t port,
                           const std::string& username,
                           const std::string& password);

    esp_err_t openSocket(const std::string& host, uint16_t port);

    /**
     * @brief Parse and dispatch one ServerMessage frame.
     *
     * Refuses a malformed frame outright rather than half-applying it. A partly
     * parsed message reaching the throttle models is how a garbled frame becomes
     * a speed command.
     */
    void handleMessage(const std::string& json);
    void handleStateSnapshot(const void* payload);
    void handleLocoState(const void* payload);
    void handleSystemStatus(const void* payload);
    void handleDccLink(const void* payload);

    /** Cached so the roster fetch and the power POST can both reach it. */
    std::string getLayoutId();

    /**
     * @brief One loco's last reported state, kept compact.
     *
     * LocoState's function map costs a heap node per function; a pair of
     * bitmasks holds F0-F28 in eight bytes.
     */
    struct CachedLocoState {
        int speed = 0;
        Direction direction = Direction::STOP;
        uint32_t functionsKnown = 0;   ///< bit n set: Fn was reported
        uint32_t functionsOn = 0;      ///< bit n set: Fn is on
    };

    /** Bounds the cache against a server reporting ever more addresses. */
    static constexpr size_t MAX_CACHED_LOCOS = 128;

    /** Records a validated LocoState. Takes the state mutex. */
    void cacheLocoState(const LocoState& state);
    void clearLocoStates();

    esp_err_t sendJson(const std::string& json);

    static void websocketEventHandler(void* handlerArgs,
                                      const char* base,
                                      int32_t eventId,
                                      void* eventData);

    /**
     * The WebSocket handle. Read by senders on the encoder and LVGL tasks, and
     * stopped and destroyed by a re-login on the supervisor's -- so every use
     * of it holds m_clientMutex, or a re-login frees it under a send (F-26).
     * Nothing on the WebSocket's own task may take that mutex: disconnect()
     * holds it while waiting for that task to stop.
     */
    esp_websocket_client_handle_t m_client;
    mutable SemaphoreHandle_t m_clientMutex;

    /** Atomic rather than under the state mutex, so no lock timeout can lose
     * a change: whether the link is up gates the knobs (F-42). */
    std::atomic<ConnectionState> m_state;

    std::string m_host;
    uint16_t m_port;
    std::string m_sessionToken;
    /** Held for the socket's lifetime: esp_websocket_client keeps the pointer. */
    std::string m_cookieHeader;
    std::string m_uri;

    /** Reassembly buffer for a message split across events or frames.
     * Touched only on the WebSocket task, and by disconnect() once that task
     * has been destroyed. */
    std::string m_rxBuffer;
    /** A text frame has opened a message that its FIN frame has not closed. */
    bool m_rxInMessage;

    std::atomic<SystemStatus> m_systemStatus;
    std::atomic<TrackPower> m_trackPower;
    std::string m_layoutId;
    std::vector<RosterEntry> m_roster;
    std::map<int, CachedLocoState> m_locoStates;
    int64_t m_lastMessageUs;

    // Set on the main or LVGL task, invoked on the WebSocket and supervisor
    // tasks. Each copies under its own lock and calls the copy (F-30).
    CallbackSlot<void(ConnectionState)> m_connectionCallback;
    CallbackSlot<void(const LocoState&)> m_locoStateCallback;
    CallbackSlot<void(SystemStatus, const std::string&)> m_systemStatusCallback;
    CallbackSlot<void(const std::vector<RosterEntry>&)> m_rosterCallback;
    CallbackSlot<void(TrackPower)> m_trackPowerCallback;
    CallbackSlot<void(const std::string&)> m_commandRefusedCallback;

    mutable SemaphoreHandle_t m_stateMutex;
};
