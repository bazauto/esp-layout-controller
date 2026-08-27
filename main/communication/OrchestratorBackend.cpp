#include "OrchestratorBackend.h"

#include "esp_log.h"

static const char* TAG = "OrchestratorBackend";

namespace {

ThrottleBackend::ConnectionState toPortState(OrchestratorClient::ConnectionState state)
{
    switch (state) {
        case OrchestratorClient::ConnectionState::AUTHENTICATING:
        case OrchestratorClient::ConnectionState::CONNECTING:
            return ThrottleBackend::ConnectionState::CONNECTING;
        case OrchestratorClient::ConnectionState::CONNECTED:
            return ThrottleBackend::ConnectionState::CONNECTED;
        case OrchestratorClient::ConnectionState::FAILED:
            return ThrottleBackend::ConnectionState::FAILED;
        case OrchestratorClient::ConnectionState::DISCONNECTED:
        default:
            // Anything unrecognised reads as disconnected, never connected: an
            // unknown link state must not enable a knob.
            return ThrottleBackend::ConnectionState::DISCONNECTED;
    }
}

}  // namespace

OrchestratorBackend::OrchestratorBackend(OrchestratorClient* client)
    : m_client(client)
    , m_mutex(nullptr)
{
    m_mutex = xSemaphoreCreateMutex();
    if (!m_mutex) {
        ESP_LOGE(TAG, "Failed to create assignment mutex");
    }

    if (!m_client) {
        ESP_LOGE(TAG, "Constructed with a null client; every command will be refused");
        return;
    }

    m_client->setLocoStateCallback(
        [this](const OrchestratorClient::LocoState& state) {
            this->onLocoState(state);
        });
}

OrchestratorBackend::~OrchestratorBackend()
{
    // Drop the client's reference to this object before the mutex goes, or a
    // frame arriving mid-teardown lands in a half-destroyed callback.
    if (m_client) {
        m_client->setLocoStateCallback(nullptr);
    }
    if (m_mutex) {
        vSemaphoreDelete(m_mutex);
        m_mutex = nullptr;
    }
}

bool OrchestratorBackend::lock(TickType_t timeout) const
{
    return m_mutex && xSemaphoreTake(m_mutex, timeout) == pdTRUE;
}

void OrchestratorBackend::unlock() const
{
    if (m_mutex) {
        xSemaphoreGive(m_mutex);
    }
}

bool OrchestratorBackend::isConnected() const
{
    return m_client && m_client->isConnected();
}

ThrottleBackend::ConnectionState OrchestratorBackend::getState() const
{
    if (!m_client) {
        return ConnectionState::DISCONNECTED;
    }
    return toPortState(m_client->getState());
}

int OrchestratorBackend::addressFor(int throttleId) const
{
    int address = 0;
    if (lock(pdMS_TO_TICKS(50))) {
        if (isValidThrottle(throttleId)) {
            address = m_assignments[throttleId].address;
        }
        unlock();
    }
    return address;
}

esp_err_t OrchestratorBackend::acquireLocomotive(int throttleId, int address, bool /*longAddress*/)
{
    // The orchestrator addresses locos by number and draws no distinction
    // between short and long DCC addresses, so longAddress is deliberately
    // unused rather than being smuggled into the address.
    if (!isValidThrottle(throttleId) || address <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!lock(pdMS_TO_TICKS(100))) {
        return ESP_ERR_TIMEOUT;
    }
    m_assignments[throttleId].address = address;
    // Assume nothing about what the loco is doing. The next LOCO_STATE tells us,
    // and until then these shadow values only matter if the operator commands
    // something, which overwrites them anyway.
    m_assignments[throttleId].speed = 0;
    m_assignments[throttleId].forward = true;
    unlock();

    ESP_LOGI(TAG, "Throttle %d now drives loco %d (no handshake; the orchestrator has no sessions)",
             throttleId, address);
    return ESP_OK;
}

esp_err_t OrchestratorBackend::releaseLocomotive(int throttleId)
{
    if (!isValidThrottle(throttleId)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!lock(pdMS_TO_TICKS(100))) {
        return ESP_ERR_TIMEOUT;
    }
    const int address = m_assignments[throttleId].address;
    m_assignments[throttleId] = Assignment{};
    unlock();

    // Deliberately sends nothing. There is no session to hand back, and this
    // device is not the only thing that can drive that loco -- an automation run
    // or another operator may be in charge of it. Stopping it here because one
    // throttle stopped displaying it would be a movement nobody commanded.
    ESP_LOGI(TAG, "Throttle %d released loco %d (left running; not ours to stop)",
             throttleId, address);
    return ESP_OK;
}

esp_err_t OrchestratorBackend::setSpeed(int throttleId, int speed)
{
    if (!m_client || !isValidThrottle(throttleId)) {
        return ESP_ERR_INVALID_ARG;
    }

    int address = 0;
    bool forward = true;
    if (!lock(pdMS_TO_TICKS(100))) {
        return ESP_ERR_TIMEOUT;
    }
    address = m_assignments[throttleId].address;
    forward = m_assignments[throttleId].forward;
    if (address != 0) {
        m_assignments[throttleId].speed = speed;
    }
    unlock();

    if (address == 0) {
        ESP_LOGW(TAG, "Refusing speed for throttle %d: no loco assigned", throttleId);
        return ESP_ERR_INVALID_STATE;
    }

    return m_client->sendThrottleCommand(
        address, speed,
        forward ? OrchestratorClient::Direction::FORWARD
                : OrchestratorClient::Direction::REVERSE);
}

esp_err_t OrchestratorBackend::setDirection(int throttleId, bool forward)
{
    if (!m_client || !isValidThrottle(throttleId)) {
        return ESP_ERR_INVALID_ARG;
    }

    int address = 0;
    int speed = 0;
    if (!lock(pdMS_TO_TICKS(100))) {
        return ESP_ERR_TIMEOUT;
    }
    address = m_assignments[throttleId].address;
    speed = m_assignments[throttleId].speed;
    if (address != 0) {
        m_assignments[throttleId].forward = forward;
    }
    unlock();

    if (address == 0) {
        ESP_LOGW(TAG, "Refusing direction for throttle %d: no loco assigned", throttleId);
        return ESP_ERR_INVALID_STATE;
    }

    return m_client->sendThrottleCommand(
        address, speed,
        forward ? OrchestratorClient::Direction::FORWARD
                : OrchestratorClient::Direction::REVERSE);
}

esp_err_t OrchestratorBackend::setSpeedAndDirection(int throttleId, int speed, bool forward)
{
    if (!m_client || !isValidThrottle(throttleId)) {
        return ESP_ERR_INVALID_ARG;
    }

    int address = 0;
    if (!lock(pdMS_TO_TICKS(100))) {
        return ESP_ERR_TIMEOUT;
    }
    address = m_assignments[throttleId].address;
    if (address != 0) {
        m_assignments[throttleId].speed = speed;
        m_assignments[throttleId].forward = forward;
    }
    unlock();

    if (address == 0) {
        ESP_LOGW(TAG, "Refusing throttle command for throttle %d: no loco assigned", throttleId);
        return ESP_ERR_INVALID_STATE;
    }

    // One message. This is the case the port's combined call exists for: the
    // wire format already carries the pair, so there is no intermediate state
    // in which the loco has the new speed and the old direction.
    return m_client->sendThrottleCommand(
        address, speed,
        forward ? OrchestratorClient::Direction::FORWARD
                : OrchestratorClient::Direction::REVERSE);
}

esp_err_t OrchestratorBackend::setFunction(int throttleId, int function, bool state)
{
    if (!m_client || !isValidThrottle(throttleId)) {
        return ESP_ERR_INVALID_ARG;
    }

    const int address = addressFor(throttleId);
    if (address == 0) {
        ESP_LOGW(TAG, "Refusing function for throttle %d: no loco assigned", throttleId);
        return ESP_ERR_INVALID_STATE;
    }

    return m_client->sendFunctionCommand(address, function, state);
}

esp_err_t OrchestratorBackend::refreshThrottleState(int /*throttleId*/)
{
    // Nothing to do: the control plane pushes LOCO_STATE unprompted, which is
    // why requiresPolling() is false and this is never called in practice.
    return ESP_OK;
}

size_t OrchestratorBackend::getRosterSize() const
{
    return m_client ? m_client->getRosterSize() : 0;
}

bool OrchestratorBackend::getRosterEntry(int index, RosterEntry& outEntry) const
{
    if (!m_client) {
        return false;
    }

    OrchestratorClient::RosterEntry entry;
    if (!m_client->getRosterEntry(index, entry)) {
        return false;
    }

    outEntry.address = entry.address;
    outEntry.name = entry.name;
    // The orchestrator makes no short/long distinction. Report the DCC
    // convention so the model layer's address type stays meaningful.
    outEntry.longAddress = (entry.address > 127);
    return true;
}

void OrchestratorBackend::onLocoState(const OrchestratorClient::LocoState& state)
{
    ThrottleStateCallback callback;
    if (lock(pdMS_TO_TICKS(50))) {
        callback = m_throttleStateCallback;
        unlock();
    } else {
        callback = m_throttleStateCallback;
    }

    if (!callback) {
        return;
    }

    // A loco can legitimately sit on more than one of this device's throttles,
    // so this is a loop rather than a lookup that stops at the first match.
    for (int throttleId = 0; throttleId < MAX_THROTTLES; ++throttleId) {
        int address = 0;
        if (lock(pdMS_TO_TICKS(50))) {
            address = m_assignments[throttleId].address;
            if (address == state.address) {
                // Keep the shadow in step, so a later speed-only change is
                // paired with the direction the layout actually has.
                m_assignments[throttleId].speed = state.speed;
                if (state.direction != OrchestratorClient::Direction::STOP) {
                    m_assignments[throttleId].forward =
                        (state.direction == OrchestratorClient::Direction::FORWARD);
                }
            }
            unlock();
        }

        if (address != state.address) {
            continue;
        }

        ThrottleUpdate update;
        update.throttleId = throttleId;
        update.address = state.address;
        update.speed = state.speed;

        // 'stop' is not a heading, so it leaves the displayed direction alone
        // rather than being flattened into "reverse".
        switch (state.direction) {
            case OrchestratorClient::Direction::FORWARD: update.direction = 1;  break;
            case OrchestratorClient::Direction::REVERSE: update.direction = 0;  break;
            case OrchestratorClient::Direction::STOP:
            default:                                    update.direction = -1; break;
        }

        callback(update);

        // Functions travel one per update, matching the port's shape.
        for (const auto& fn : state.functions) {
            ThrottleUpdate fnUpdate;
            fnUpdate.throttleId = throttleId;
            fnUpdate.address = state.address;
            fnUpdate.function = fn.first;
            fnUpdate.functionState = fn.second;
            callback(fnUpdate);
        }
    }
}

void OrchestratorBackend::setThrottleStateCallback(ThrottleStateCallback callback)
{
    if (lock(pdMS_TO_TICKS(100))) {
        m_throttleStateCallback = std::move(callback);
        unlock();
    } else {
        m_throttleStateCallback = std::move(callback);
    }
}

void OrchestratorBackend::setFunctionLabelsCallback(FunctionLabelsCallback /*callback*/)
{
    // Never invoked: providesFunctionLabels() is false, so ThrottleController
    // does not register one. Stored nowhere rather than kept and ignored.
    ESP_LOGD(TAG, "Function labels are not available from the orchestrator");
}
