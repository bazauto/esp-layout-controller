#pragma once

#include <array>

#include "OrchestratorClient.h"
#include "ThrottleBackend.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/**
 * @brief Adapts OrchestratorClient to the ThrottleBackend port.
 *
 * Owns nothing: the client is injected and outlives this adapter.
 *
 * The two protocols differ in a way this class exists to absorb. WiThrottle has
 * sessions -- a throttle acquires a loco and thereafter commands are addressed
 * to the throttle. The orchestrator's control plane has none: every command
 * names a loco address outright, and any operator may drive any loco at any
 * time. So the throttle-to-loco mapping that WiThrottle keeps server-side is
 * kept here instead, and "acquire" is local bookkeeping rather than a handshake.
 */
class OrchestratorBackend : public ThrottleBackend {
public:
    explicit OrchestratorBackend(OrchestratorClient* client);
    ~OrchestratorBackend() override;

    OrchestratorBackend(const OrchestratorBackend&) = delete;
    OrchestratorBackend& operator=(const OrchestratorBackend&) = delete;

    /** No handshake: commands name a loco address directly. */
    bool requiresAcquisition() const override { return false; }

    /** Fetched over REST after connecting, since the control plane carries loco
     * state keyed by address but no names. */
    bool providesRoster() const override { return true; }

    /** The orchestrator's locos table stores no function labels yet, so the UI
     * falls back to F0...F28. Being fixed orchestrator-side. */
    bool providesFunctionLabels() const override { return false; }

    /** State arrives unprompted as LOCO_STATE, so no polling task is created. */
    bool requiresPolling() const override { return false; }

    bool isConnected() const override;
    ConnectionState getState() const override;

    esp_err_t acquireLocomotive(int throttleId, int address, bool longAddress) override;
    esp_err_t releaseLocomotive(int throttleId) override;
    esp_err_t setSpeed(int throttleId, int speed) override;
    esp_err_t setDirection(int throttleId, bool forward) override;
    esp_err_t setSpeedAndDirection(int throttleId, int speed, bool forward) override;
    esp_err_t setFunction(int throttleId, int function, bool state) override;
    esp_err_t refreshThrottleState(int throttleId) override;

    size_t getRosterSize() const override;
    bool getRosterEntry(int index, RosterEntry& outEntry) const override;

    void setThrottleStateCallback(ThrottleStateCallback callback) override;
    void setFunctionLabelsCallback(FunctionLabelsCallback callback) override;

private:
    /**
     * @brief What this device currently has on each throttle.
     *
     * `speed` and `direction` are shadow copies of what we last commanded, kept
     * because THROTTLE_COMMAND carries both together and a caller changing one
     * still has to supply the other.
     */
    struct Assignment {
        int address = 0;          ///< 0 = nothing assigned
        int speed = 0;
        bool forward = true;
    };

    static bool isValidThrottle(int throttleId) {
        return throttleId >= 0 && throttleId < MAX_THROTTLES;
    }

    /** @return the loco address on that throttle, or 0 if none. */
    int addressFor(int throttleId) const;

    /** Routes an incoming LocoState to whichever throttles hold that address. */
    void onLocoState(const OrchestratorClient::LocoState& state);

    bool lock(TickType_t timeout) const;
    void unlock() const;

    OrchestratorClient* m_client;
    std::array<Assignment, MAX_THROTTLES> m_assignments;

    ThrottleStateCallback m_throttleStateCallback;

    mutable SemaphoreHandle_t m_mutex;
};
