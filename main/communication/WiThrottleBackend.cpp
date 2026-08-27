#include "WiThrottleBackend.h"

#include "esp_log.h"

static const char* TAG = "WiThrottleBackend";

namespace {

ThrottleBackend::ConnectionState toPortState(WiThrottleClient::ConnectionState state)
{
    switch (state) {
        case WiThrottleClient::ConnectionState::CONNECTING:
            return ThrottleBackend::ConnectionState::CONNECTING;
        case WiThrottleClient::ConnectionState::CONNECTED:
            return ThrottleBackend::ConnectionState::CONNECTED;
        case WiThrottleClient::ConnectionState::FAILED:
            return ThrottleBackend::ConnectionState::FAILED;
        case WiThrottleClient::ConnectionState::DISCONNECTED:
        default:
            // Anything unrecognised reads as disconnected, not connected: an
            // unknown link state must never enable a knob.
            return ThrottleBackend::ConnectionState::DISCONNECTED;
    }
}

}  // namespace

WiThrottleBackend::WiThrottleBackend(WiThrottleClient* client, JmriJsonClient* jsonClient)
    : m_client(client)
    , m_jsonClient(jsonClient)
{
    if (!m_client) {
        ESP_LOGE(TAG, "Constructed with a null WiThrottleClient; every command will be refused");
    }
    if (!m_jsonClient) {
        ESP_LOGW(TAG, "No JSON client; track power will be unavailable");
    }
}

esp_err_t WiThrottleBackend::setTrackPower(bool on)
{
    if (!m_jsonClient) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return m_jsonClient->setPower(on);
}

ThrottleBackend::TrackPower WiThrottleBackend::getTrackPower() const
{
    if (!m_jsonClient) {
        return TrackPower::UNKNOWN;
    }

    switch (m_jsonClient->getPower()) {
        case JmriJsonClient::PowerState::ON:  return TrackPower::ON;
        case JmriJsonClient::PowerState::OFF: return TrackPower::OFF;
        default:                              return TrackPower::UNKNOWN;
    }
}

void WiThrottleBackend::setTrackPowerCallback(TrackPowerCallback callback)
{
    m_trackPowerCallback = std::move(callback);

    if (!m_jsonClient) {
        return;
    }

    if (!m_trackPowerCallback) {
        m_jsonClient->setPowerStateCallback(nullptr);
        return;
    }

    m_jsonClient->setPowerStateCallback(
        [this](const std::string& /*powerName*/, JmriJsonClient::PowerState state) {
            if (!m_trackPowerCallback) {
                return;
            }
            switch (state) {
                case JmriJsonClient::PowerState::ON:
                    m_trackPowerCallback(TrackPower::ON);
                    break;
                case JmriJsonClient::PowerState::OFF:
                    m_trackPowerCallback(TrackPower::OFF);
                    break;
                default:
                    m_trackPowerCallback(TrackPower::UNKNOWN);
                    break;
            }
        });
}

bool WiThrottleBackend::isConnected() const
{
    return m_client && m_client->isConnected();
}

ThrottleBackend::ConnectionState WiThrottleBackend::getState() const
{
    if (!m_client) {
        return ConnectionState::DISCONNECTED;
    }
    return toPortState(m_client->getState());
}

esp_err_t WiThrottleBackend::acquireLocomotive(int throttleId, int address, bool longAddress)
{
    if (!m_client || !isValidThrottle(throttleId)) {
        return ESP_ERR_INVALID_ARG;
    }
    return m_client->acquireLocomotive(toWireId(throttleId), address, longAddress);
}

esp_err_t WiThrottleBackend::releaseLocomotive(int throttleId)
{
    if (!m_client || !isValidThrottle(throttleId)) {
        return ESP_ERR_INVALID_ARG;
    }
    return m_client->releaseLocomotive(toWireId(throttleId));
}

esp_err_t WiThrottleBackend::setSpeed(int throttleId, int speed)
{
    if (!m_client || !isValidThrottle(throttleId)) {
        return ESP_ERR_INVALID_ARG;
    }
    return m_client->setSpeed(toWireId(throttleId), speed);
}

esp_err_t WiThrottleBackend::setDirection(int throttleId, bool forward)
{
    if (!m_client || !isValidThrottle(throttleId)) {
        return ESP_ERR_INVALID_ARG;
    }
    return m_client->setDirection(toWireId(throttleId), forward);
}

esp_err_t WiThrottleBackend::setFunction(int throttleId, int function, bool state)
{
    if (!m_client || !isValidThrottle(throttleId)) {
        return ESP_ERR_INVALID_ARG;
    }
    return m_client->setFunction(toWireId(throttleId), function, state);
}

esp_err_t WiThrottleBackend::refreshThrottleState(int throttleId)
{
    if (!m_client || !isValidThrottle(throttleId)) {
        return ESP_ERR_INVALID_ARG;
    }

    // Two queries behind one port call. The first error wins, but the second
    // query is issued either way: a refused speed query is no reason to leave
    // the displayed direction stale.
    const char wireId = toWireId(throttleId);
    esp_err_t speedErr = m_client->querySpeed(wireId);
    esp_err_t dirErr = m_client->queryDirection(wireId);
    return (speedErr != ESP_OK) ? speedErr : dirErr;
}

size_t WiThrottleBackend::getRosterSize() const
{
    return m_client ? m_client->getRosterSize() : 0;
}

bool WiThrottleBackend::getRosterEntry(int index, RosterEntry& outEntry) const
{
    if (!m_client) {
        return false;
    }

    WiThrottleClient::Locomotive entry;
    if (!m_client->getRosterEntry(index, entry)) {
        return false;
    }

    outEntry.address = entry.address;
    outEntry.name = entry.name;
    outEntry.longAddress = (entry.addressType == 'L');
    return true;
}

void WiThrottleBackend::setThrottleStateCallback(ThrottleStateCallback callback)
{
    m_throttleStateCallback = std::move(callback);

    if (!m_client) {
        return;
    }

    if (!m_throttleStateCallback) {
        m_client->setThrottleStateCallback(nullptr);
        return;
    }

    m_client->setThrottleStateCallback(
        [this](const WiThrottleClient::ThrottleUpdate& update) {
            if (!m_throttleStateCallback) {
                return;
            }

            ThrottleUpdate ported;
            ported.throttleId = update.throttleId - '0';
            ported.address = update.address;
            ported.speed = update.speed;
            ported.direction = update.direction;
            ported.function = update.function;
            ported.functionState = update.functionState;

            // A malformed throttle id is dropped here rather than passed up as
            // a negative index. The controller re-checks, but the wire-format
            // knowledge belongs on this side of the port.
            if (!isValidThrottle(ported.throttleId)) {
                ESP_LOGW(TAG, "Dropping update for out-of-range throttle id '%c'",
                         update.throttleId);
                return;
            }

            m_throttleStateCallback(ported);
        });
}

void WiThrottleBackend::setConnectionStateCallback(ConnectionStateCallback callback)
{
    m_connectionStateCallback = std::move(callback);

    if (!m_client) {
        return;
    }

    if (!m_connectionStateCallback) {
        m_client->setConnectionStateCallback(nullptr);
        return;
    }

    m_client->setConnectionStateCallback(
        [this](WiThrottleClient::ConnectionState state) {
            if (m_connectionStateCallback) {
                m_connectionStateCallback(toPortState(state));
            }
        });
}

void WiThrottleBackend::setFunctionLabelsCallback(FunctionLabelsCallback callback)
{
    m_functionLabelsCallback = std::move(callback);

    if (!m_client) {
        return;
    }

    if (!m_functionLabelsCallback) {
        m_client->setFunctionLabelsCallback(nullptr);
        return;
    }

    m_client->setFunctionLabelsCallback(
        [this](char throttleIdChar, const std::vector<std::string>& labels) {
            if (!m_functionLabelsCallback) {
                return;
            }

            const int throttleId = throttleIdChar - '0';
            if (!isValidThrottle(throttleId)) {
                ESP_LOGW(TAG, "Dropping function labels for out-of-range throttle id '%c'",
                         throttleIdChar);
                return;
            }

            m_functionLabelsCallback(throttleId, labels);
        });
}
