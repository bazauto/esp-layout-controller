#pragma once

#include "ThrottleBackend.h"
#include "WiThrottleClient.h"

/**
 * @brief Adapts WiThrottleClient to the ThrottleBackend port.
 *
 * Owns nothing: the client is injected and outlives this adapter, exactly as
 * AppController arranges it. All this type does is translate -- throttle
 * indices to WiThrottle's character ids, roster entries to the port's shape,
 * and the two separate speed/direction queries to one refresh -- plus answer
 * the capability queries for a session-oriented, polled, roster-bearing,
 * label-bearing protocol.
 */
class WiThrottleBackend : public ThrottleBackend {
public:
    explicit WiThrottleBackend(WiThrottleClient* client);
    ~WiThrottleBackend() override = default;

    WiThrottleBackend(const WiThrottleBackend&) = delete;
    WiThrottleBackend& operator=(const WiThrottleBackend&) = delete;

    // WiThrottle sessions own a loco, answer queries rather than pushing, and
    // carry both a roster and JMRI's decoder-database function labels.
    bool requiresAcquisition() const override { return true; }
    bool providesRoster() const override { return true; }
    bool providesFunctionLabels() const override { return true; }
    bool requiresPolling() const override { return true; }

    bool isConnected() const override;
    ConnectionState getState() const override;

    esp_err_t acquireLocomotive(int throttleId, int address, bool longAddress) override;
    esp_err_t releaseLocomotive(int throttleId) override;
    esp_err_t setSpeed(int throttleId, int speed) override;
    esp_err_t setDirection(int throttleId, bool forward) override;
    esp_err_t setFunction(int throttleId, int function, bool state) override;
    esp_err_t refreshThrottleState(int throttleId) override;

    size_t getRosterSize() const override;
    bool getRosterEntry(int index, RosterEntry& outEntry) const override;

    void setThrottleStateCallback(ThrottleStateCallback callback) override;
    void setFunctionLabelsCallback(FunctionLabelsCallback callback) override;
    void setConnectionStateCallback(ConnectionStateCallback callback) override;

private:
    /** Throttle index to WiThrottle's character id, or 0 when out of range.
     * Callers check validity with isValidThrottle first. */
    static char toWireId(int throttleId) { return static_cast<char>('0' + throttleId); }
    static bool isValidThrottle(int throttleId) {
        return throttleId >= 0 && throttleId < MAX_THROTTLES;
    }

    WiThrottleClient* m_client;

    // Held so the lambdas registered on the client stay valid, and so a second
    // setThrottleStateCallback replaces the first rather than stacking.
    ThrottleStateCallback m_throttleStateCallback;
    FunctionLabelsCallback m_functionLabelsCallback;
    ConnectionStateCallback m_connectionStateCallback;
};
