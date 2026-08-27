#pragma once

#include "JmriJsonClient.h"
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
    /**
     * @param client WiThrottle client. Not owned; must outlive this adapter.
     * @param jsonClient JMRI JSON client, used **only** for track power.
     *        Under JMRI, power goes over the JSON API and not the WiThrottle
     *        `PPA` command -- that is what the power button has always done,
     *        and routing it through WiThrottle here would quietly change
     *        behaviour for every existing JMRI user. May be null, in which
     *        case supportsTrackPower() is false.
     */
    WiThrottleBackend(WiThrottleClient* client, JmriJsonClient* jsonClient);
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

    bool supportsTrackPower() const override { return m_jsonClient != nullptr; }
    esp_err_t setTrackPower(bool on) override;
    TrackPower getTrackPower() const override;
    void setTrackPowerCallback(TrackPowerCallback callback) override;

private:
    /** Throttle index to WiThrottle's character id, or 0 when out of range.
     * Callers check validity with isValidThrottle first. */
    static char toWireId(int throttleId) { return static_cast<char>('0' + throttleId); }
    static bool isValidThrottle(int throttleId) {
        return throttleId >= 0 && throttleId < MAX_THROTTLES;
    }

    WiThrottleClient* m_client;
    JmriJsonClient* m_jsonClient;

    // Held so the lambdas registered on the client stay valid, and so a second
    // setThrottleStateCallback replaces the first rather than stacking.
    ThrottleStateCallback m_throttleStateCallback;
    FunctionLabelsCallback m_functionLabelsCallback;
    ConnectionStateCallback m_connectionStateCallback;
    TrackPowerCallback m_trackPowerCallback;
};
