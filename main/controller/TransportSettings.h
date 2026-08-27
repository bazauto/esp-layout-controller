#pragma once

#include <cstdint>
#include <string>

#include "esp_err.h"

/**
 * @brief Which transport drives locomotives.
 *
 * A runtime choice, persisted in NVS, deliberately not a Kconfig option: with
 * no OTA partition a compile-time switch would mean a cable at the layout to
 * A/B a bug, and it would fork an already-forked build matrix.
 */
enum class ThrottleTransport : uint8_t {
    WITHROTTLE = 0,   ///< WiThrottle to JMRI. The default, and still a peer.
    ORCHESTRATOR = 1  ///< The layout orchestrator's WebSocket control plane.
};

/**
 * @brief Orchestrator connection settings, and the active transport choice.
 *
 * Stored in NVS namespace `orch`. The credential is a dedicated `operator`
 * account, held in plaintext -- the same accepted and documented risk as the
 * WiFi password (F-18), not an oversight.
 */
struct TransportSettings {
    ThrottleTransport transport = ThrottleTransport::WITHROTTLE;
    std::string host;
    uint16_t port = 3000;
    std::string username;
    std::string password;

    /** True when there is enough here to attempt a connection. */
    bool isOrchestratorConfigured() const {
        return !host.empty() && !username.empty() && !password.empty();
    }

    /**
     * @brief Load from NVS, falling back to defaults for anything unset.
     *
     * Never fails in a way that matters: an unreadable namespace yields the
     * defaults, which select WiThrottle. Failing towards the transport that
     * needs no credentials is the safe direction.
     */
    static TransportSettings load();

    /** Persist to NVS. */
    esp_err_t save() const;

    static const char* transportName(ThrottleTransport transport);
};
