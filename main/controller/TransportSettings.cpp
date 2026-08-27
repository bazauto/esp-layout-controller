#include "TransportSettings.h"

#include <cstring>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char* TAG = "TransportSettings";

namespace {

constexpr const char* NVS_NAMESPACE = "orch";
constexpr const char* NVS_KEY_TRANSPORT = "transport";
constexpr const char* NVS_KEY_HOST = "host";
constexpr const char* NVS_KEY_PORT = "port";
constexpr const char* NVS_KEY_USERNAME = "user";
constexpr const char* NVS_KEY_PASSWORD = "pass";

/** Longest value we will read back; NVS strings are bounded anyway. */
constexpr size_t MAX_VALUE_LEN = 128;

std::string readString(nvs_handle_t handle, const char* key)
{
    char buffer[MAX_VALUE_LEN] = {};
    size_t length = sizeof(buffer);
    if (nvs_get_str(handle, key, buffer, &length) != ESP_OK) {
        return std::string();
    }
    return std::string(buffer);
}

}  // namespace

TransportSettings TransportSettings::load()
{
    TransportSettings settings;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        // Nothing saved yet is the normal first-boot case, not a fault.
        ESP_LOGI(TAG, "No stored transport settings; defaulting to WiThrottle");
        return settings;
    }

    uint8_t transport = static_cast<uint8_t>(ThrottleTransport::WITHROTTLE);
    nvs_get_u8(handle, NVS_KEY_TRANSPORT, &transport);
    // An unrecognised stored value falls back to WiThrottle rather than being
    // cast blindly into the enum.
    settings.transport = (transport == static_cast<uint8_t>(ThrottleTransport::ORCHESTRATOR))
                             ? ThrottleTransport::ORCHESTRATOR
                             : ThrottleTransport::WITHROTTLE;

    settings.host = readString(handle, NVS_KEY_HOST);
    settings.username = readString(handle, NVS_KEY_USERNAME);
    settings.password = readString(handle, NVS_KEY_PASSWORD);

    uint16_t port = 3000;
    if (nvs_get_u16(handle, NVS_KEY_PORT, &port) == ESP_OK && port != 0) {
        settings.port = port;
    }

    nvs_close(handle);

    // Never log the password.
    ESP_LOGI(TAG, "Transport: %s (orchestrator %s:%u, user '%s')",
             transportName(settings.transport),
             settings.host.empty() ? "<unset>" : settings.host.c_str(),
             static_cast<unsigned>(settings.port),
             settings.username.empty() ? "<unset>" : settings.username.c_str());

    // Refuse to select a transport that cannot possibly connect. Booting into
    // an unconfigured orchestrator would leave every knob dead with no
    // explanation; WiThrottle at least has its own settings to fall back on.
    if (settings.transport == ThrottleTransport::ORCHESTRATOR &&
        !settings.isOrchestratorConfigured()) {
        ESP_LOGW(TAG, "Orchestrator selected but not fully configured; using WiThrottle");
        settings.transport = ThrottleTransport::WITHROTTLE;
    }

    return settings;
}

esp_err_t TransportSettings::save() const
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing: %s", esp_err_to_name(err));
        return err;
    }

    nvs_set_u8(handle, NVS_KEY_TRANSPORT, static_cast<uint8_t>(transport));
    nvs_set_str(handle, NVS_KEY_HOST, host.c_str());
    nvs_set_u16(handle, NVS_KEY_PORT, port);
    nvs_set_str(handle, NVS_KEY_USERNAME, username.c_str());
    nvs_set_str(handle, NVS_KEY_PASSWORD, password.c_str());

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit transport settings: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Saved transport settings (%s)", transportName(transport));
    return ESP_OK;
}

const char* TransportSettings::transportName(ThrottleTransport transport)
{
    switch (transport) {
        case ThrottleTransport::ORCHESTRATOR: return "orchestrator";
        case ThrottleTransport::WITHROTTLE:
        default:                              return "withrottle";
    }
}
