#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>
#include "esp_err.h"

/**
 * @brief Transport-neutral port for driving locomotives.
 *
 * ThrottleController depends on this and never on a concrete client, so a
 * second transport can be added without the controller learning anything about
 * either protocol's wire format.
 *
 * The interface is drawn at "throttle N drives loco A at speed S". Everything a
 * transport may or may not offer -- a roster, an acquire/release handshake,
 * function labels, polling -- is a capability query the backend answers in its
 * own terms. Nothing here asks one protocol to impersonate the other:
 * WiThrottle is session-oriented and the layout orchestrator's control plane is
 * not, and that difference surfaces as requiresAcquisition() rather than as a
 * fake session.
 *
 * Threading: implementations are called from the LVGL task (through the
 * controller's event handlers) and from the polling task, so every method must
 * be safe to call from more than one task. Callbacks are invoked on whichever
 * task the transport receives on -- never assume the LVGL task, and take
 * lvgl_port_lock before touching a widget from one.
 */
class ThrottleBackend {
public:
    /** Throttle identifiers are plain indices here. WiThrottle's "'0' + id"
     * character encoding is a wire detail and stays behind the adapter. */
    static constexpr int MAX_THROTTLES = 4;

    enum class ConnectionState {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        FAILED
    };

    /**
     * @brief One selectable locomotive, as the roster carousel sees it.
     *
     * A bool for the address type rather than WiThrottle's 'S'/'L' character,
     * for the same reason throttle ids are ints: the carousel has no business
     * knowing how a protocol spells an address type.
     */
    struct RosterEntry {
        int address = 0;
        std::string name;
        bool longAddress = false;
    };

    /**
     * @brief An unsolicited change to a throttle's state, from the transport.
     *
     * Fields are individually optional because both transports report partial
     * updates: speed, direction and function are -1 when the message did not
     * carry them, and functionState is meaningful only when function >= 0.
     */
    struct ThrottleUpdate {
        int throttleId = -1;
        int address = 0;
        int speed = -1;
        int direction = -1;      // 0 = reverse, 1 = forward
        int function = -1;
        bool functionState = false;
    };

    /**
     * @brief Track power, as the layout reports it.
     *
     * UNKNOWN is not "off": it means nothing has told us yet. The UI must show
     * it as its own state rather than implying the rails are dead.
     */
    enum class TrackPower {
        OFF,
        ON,
        UNKNOWN
    };

    using ConnectionStateCallback = std::function<void(ConnectionState state)>;
    using TrackPowerCallback = std::function<void(TrackPower state)>;
    using ThrottleStateCallback = std::function<void(const ThrottleUpdate& update)>;
    using FunctionLabelsCallback =
        std::function<void(int throttleId, const std::vector<std::string>& labels)>;

    virtual ~ThrottleBackend() = default;

    // --- Capabilities ------------------------------------------------------

    /**
     * @brief Whether a loco must be acquired before it can be driven.
     *
     * True for WiThrottle, whose sessions own a loco for the length of the
     * connection. False for a transport that addresses locos directly, where
     * acquireLocomotive and releaseLocomotive are local bookkeeping only.
     */
    virtual bool requiresAcquisition() const = 0;

    /**
     * @brief Whether the transport supplies a selectable roster.
     *
     * When false the carousel has nothing to show and the controller must not
     * offer loco selection.
     */
    virtual bool providesRoster() const = 0;

    /**
     * @brief Whether the transport names functions.
     *
     * When false the UI falls back to F0...F28.
     */
    virtual bool providesFunctionLabels() const = 0;

    /**
     * @brief Whether remote state must be pulled rather than pushed.
     *
     * True for WiThrottle, which answers queries. A transport that pushes state
     * changes unprompted returns false, and the controller then creates no
     * polling task at all rather than running one that does nothing.
     */
    virtual bool requiresPolling() const = 0;

    // --- Connection --------------------------------------------------------

    virtual bool isConnected() const = 0;
    virtual ConnectionState getState() const = 0;

    // --- Driving -----------------------------------------------------------

    /**
     * @param throttleId Throttle index, 0 to MAX_THROTTLES - 1.
     * @param address Locomotive DCC address.
     * @param longAddress True for a long (extended) DCC address.
     */
    virtual esp_err_t acquireLocomotive(int throttleId, int address, bool longAddress) = 0;
    virtual esp_err_t releaseLocomotive(int throttleId) = 0;

    /** @param speed 0-126, where 0 is stop. */
    virtual esp_err_t setSpeed(int throttleId, int speed) = 0;
    virtual esp_err_t setDirection(int throttleId, bool forward) = 0;
    virtual esp_err_t setFunction(int throttleId, int function, bool state) = 0;

    /**
     * @brief Change speed and direction as one movement.
     *
     * Callers must use this rather than setSpeed followed by setDirection
     * whenever both change at once. Sending the new speed against the old
     * direction first commands the loco *faster the way it was already going*
     * before reversing it, which on a fast knob spin is a real jolt on the
     * layout rather than a theoretical one.
     *
     * The default sends direction first and speed second, which is the safe
     * ordering for a transport that has no combined command. A transport whose
     * wire format already carries the pair -- the orchestrator's
     * THROTTLE_COMMAND does -- overrides this to send exactly one message.
     */
    virtual esp_err_t setSpeedAndDirection(int throttleId, int speed, bool forward)
    {
        const esp_err_t dirErr = setDirection(throttleId, forward);
        const esp_err_t speedErr = setSpeed(throttleId, speed);
        return (dirErr != ESP_OK) ? dirErr : speedErr;
    }

    /**
     * @brief Ask the transport to restate a throttle's speed and direction.
     *
     * Called only when requiresPolling() is true. The answer arrives through
     * the throttle-state callback like any other update, not as a return value.
     */
    virtual esp_err_t refreshThrottleState(int throttleId) = 0;

    // --- Roster ------------------------------------------------------------

    virtual size_t getRosterSize() const = 0;

    /** @return true when index was in range and outEntry was written. */
    virtual bool getRosterEntry(int index, RosterEntry& outEntry) const = 0;

    // --- Notifications -----------------------------------------------------

    virtual void setThrottleStateCallback(ThrottleStateCallback callback) = 0;
    virtual void setFunctionLabelsCallback(FunctionLabelsCallback callback) = 0;

    /**
     * @brief Notified whenever the link comes up or goes down.
     *
     * The UI gates the knobs on this: a knob that still turned while the link
     * was down would move a model that no longer tracks anything real. It must
     * therefore come from the *active* backend, not from whichever concrete
     * client the UI happens to know about.
     */
    virtual void setConnectionStateCallback(ConnectionStateCallback callback) = 0;

    // --- Track power -------------------------------------------------------
    //
    // Strictly this is a layout command rather than a throttle one, but it
    // rides the same connection and the UI needs one place to ask. Keeping it
    // here rather than inventing a second port with two more adapters.

    /** False means the UI must not offer a power button at all. */
    virtual bool supportsTrackPower() const = 0;

    virtual esp_err_t setTrackPower(bool on) = 0;
    virtual TrackPower getTrackPower() const = 0;
    virtual void setTrackPowerCallback(TrackPowerCallback callback) = 0;
};
