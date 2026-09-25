#include "JmriConnectionController.h"
#include "SettingsWriter.h"
#include "WiFiController.h"
#include "../communication/JmriJsonClient.h"
#include "../communication/WiThrottleClient.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"

#include <cstdlib>
#include <string>

static const char* TAG = "JmriConnCtrl";

static const char* NVS_NAMESPACE = "jmri";
static const char* NVS_KEY_SERVER_IP = "server_ip";
static const char* NVS_KEY_JSON_PORT = "json_port";
static const char* NVS_KEY_WITHROTTLE_PORT = "wt_port";
static const char* NVS_KEY_POWER_MANAGER = "power_mgr";

namespace {

/** Checks the links this often, and at once when woken by a request. */
constexpr uint32_t WORKER_TICK_MS = 1000;

/** Settle time after WiFi comes up before the first attempt. */
constexpr int64_t WIFI_SETTLE_US = 1000 * 1000;

/** Backoff between reconnect attempts: 5 s, doubling, capped at a minute. */
constexpr uint32_t RETRY_BASE_S = 5;
constexpr uint32_t RETRY_MAX_S = 60;

/** The worker connects over TCP and WebSocket, runs the clients' connection
 * callbacks -- which repaint the main screen under the LVGL lock -- and writes
 * NVS. Sized as the config screen's own connect task was. */
constexpr uint32_t WORKER_STACK_BYTES = 6144;

uint16_t parsePort(const char* text, uint16_t fallback)
{
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (end == text || value <= 0 || value > 65535) {
        return fallback;
    }
    return static_cast<uint16_t>(value);
}

}  // namespace

JmriConnectionController::JmriConnectionController(JmriJsonClient* jsonClient,
                                                   WiThrottleClient* wtClient,
                                                   WiFiController* wifiController,
                                                   SettingsWriter* settingsWriter)
    : m_jsonClient(jsonClient)
    , m_wtClient(wtClient)
    , m_wifiController(wifiController)
    , m_settingsWriter(settingsWriter)
    , m_mutex(nullptr)
    , m_autoReconnect(false)
    , m_request(Request::NONE)
    , m_discoveredJsonPort(0)
    , m_busy(false)
    , m_workerTask(nullptr)
{
    m_mutex = xSemaphoreCreateMutex();
    if (!m_mutex) {
        ESP_LOGE(TAG, "Failed to create settings mutex");
    }
}

JmriConnectionController::~JmriConnectionController()
{
    // Owned by the AppController singleton, so in practice never destroyed
    // while the worker runs.
    if (m_workerTask) {
        vTaskDelete(m_workerTask);
        m_workerTask = nullptr;
    }
    if (m_mutex) {
        vSemaphoreDelete(m_mutex);
        m_mutex = nullptr;
    }
}

bool JmriConnectionController::lock() const
{
    return m_mutex && xSemaphoreTake(m_mutex, pdMS_TO_TICKS(1000)) == pdTRUE;
}

void JmriConnectionController::unlock() const
{
    if (m_mutex) {
        xSemaphoreGive(m_mutex);
    }
}

// --- Settings ---------------------------------------------------------------

JmriConnectionController::Settings JmriConnectionController::loadSettings()
{
    Settings settings;

    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        ESP_LOGD(TAG, "No saved JMRI settings");
        return settings;
    }

    char text[64];
    size_t length = sizeof(text);
    if (nvs_get_str(handle, NVS_KEY_SERVER_IP, text, &length) == ESP_OK) {
        settings.serverIp = text;
    }

    char port[8];
    length = sizeof(port);
    if (nvs_get_str(handle, NVS_KEY_JSON_PORT, port, &length) == ESP_OK) {
        settings.jsonPort = parsePort(port, DEFAULT_JSON_PORT);
    }
    length = sizeof(port);
    if (nvs_get_str(handle, NVS_KEY_WITHROTTLE_PORT, port, &length) == ESP_OK) {
        settings.wtPort = parsePort(port, DEFAULT_WT_PORT);
    }

    length = sizeof(text);
    if (nvs_get_str(handle, NVS_KEY_POWER_MANAGER, text, &length) == ESP_OK && text[0] != '\0') {
        settings.powerManager = text;
    }

    nvs_close(handle);
    return settings;
}

void JmriConnectionController::saveSettings(const Settings& settings)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return;
    }

    // Ports stay strings: that is how they have always been stored.
    nvs_set_str(handle, NVS_KEY_SERVER_IP, settings.serverIp.c_str());
    nvs_set_str(handle, NVS_KEY_WITHROTTLE_PORT, std::to_string(settings.wtPort).c_str());
    nvs_set_str(handle, NVS_KEY_POWER_MANAGER, settings.powerManager.c_str());
    nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "JMRI settings saved (%s, WiThrottle %u, power manager '%s')",
             settings.serverIp.c_str(), settings.wtPort, settings.powerManager.c_str());
}

void JmriConnectionController::saveJsonPort(uint16_t port)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    nvs_set_str(handle, NVS_KEY_JSON_PORT, std::to_string(port).c_str());
    nvs_commit(handle);
    nvs_close(handle);
}

// --- Requests ---------------------------------------------------------------

void JmriConnectionController::start()
{
    if (m_workerTask) {
        return;
    }
    if (!m_jsonClient || !m_wtClient) {
        ESP_LOGE(TAG, "Clients not initialised; JMRI connection not started");
        return;
    }

    // An NVS read, but start() runs from AppController::initialise(), not from
    // an LVGL event handler.
    const Settings saved = loadSettings();
    if (lock()) {
        m_settings = saved;
        m_autoReconnect = !saved.serverIp.empty();
        unlock();
    }

    // PW announces the JSON server's port. The receive task only records it:
    // connecting the JSON client from there raced the reconnect task into a
    // double destroy of the WebSocket client (F-34).
    m_wtClient->setWebPortCallback([this](uint16_t port) {
        m_discoveredJsonPort = port;
        wakeWorker();
    });

    if (xTaskCreate(workerTask, "jmri_conn", WORKER_STACK_BYTES, this, 4, &m_workerTask) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create JMRI connection task");
        m_workerTask = nullptr;
        return;
    }

    if (saved.serverIp.empty()) {
        ESP_LOGI(TAG, "No JMRI server saved; waiting for one from the config screen");
    } else {
        ESP_LOGI(TAG, "JMRI: %s (JSON %u, WiThrottle %u, power manager '%s'); connecting once WiFi is up",
                 saved.serverIp.c_str(), saved.jsonPort, saved.wtPort, saved.powerManager.c_str());
    }
}

void JmriConnectionController::requestConnect(const std::string& serverIp,
                                              uint16_t wtPort,
                                              const std::string& powerManager)
{
    Settings settings;
    settings.serverIp = serverIp;
    settings.wtPort = (wtPort != 0) ? wtPort : DEFAULT_WT_PORT;
    settings.powerManager = powerManager.empty() ? DEFAULT_POWER_MANAGER : powerManager;

    if (!m_workerTask) {
        // The orchestrator is the selected transport, and the device must not
        // sit retrying a server the operator did not choose. Keep the settings
        // for when WiThrottle is selected. Saved off the calling task, which is
        // the LVGL task (F-05), and in order with every other UI save (F-39).
        ESP_LOGW(TAG, "WiThrottle is not the selected transport; saving JMRI settings only");
        if (m_settingsWriter) {
            m_settingsWriter->post([settings]() { saveSettings(settings); });
        } else {
            ESP_LOGE(TAG, "No settings writer; JMRI settings not saved");
        }
        return;
    }

    if (!lock()) {
        ESP_LOGW(TAG, "Could not lock to request a JMRI connect");
        return;
    }
    // Keep the JSON port learned from PW; the new server's PW corrects it.
    settings.jsonPort = m_settings.jsonPort;
    m_requestedSettings = settings;
    m_request = Request::CONNECT;
    unlock();

    m_busy = true;
    wakeWorker();
}

void JmriConnectionController::requestDisconnect()
{
    if (!m_workerTask) {
        ESP_LOGI(TAG, "JMRI connection not running; nothing to disconnect");
        return;
    }

    if (!lock()) {
        ESP_LOGW(TAG, "Could not lock to request a JMRI disconnect");
        return;
    }
    m_request = Request::DISCONNECT;
    unlock();

    m_busy = true;
    wakeWorker();
}

void JmriConnectionController::wakeWorker()
{
    if (m_workerTask) {
        xTaskNotifyGive(m_workerTask);
    }
}

// --- Worker -----------------------------------------------------------------

void JmriConnectionController::workerTask(void* arg)
{
    static_cast<JmriConnectionController*>(arg)->runWorker();
}

void JmriConnectionController::runWorker()
{
    uint32_t failedAttempts = 0;
    int64_t nextAttemptUs = 0;
    bool wifiWasUp = false;

    for (;;) {
        Request request = Request::NONE;
        Settings requested;
        if (lock()) {
            request = m_request;
            m_request = Request::NONE;
            requested = m_requestedSettings;
            unlock();
        }

        if (request == Request::DISCONNECT) {
            // Sticks: nothing reconnects until the operator asks again.
            if (lock()) {
                m_autoReconnect = false;
                unlock();
            }
            ESP_LOGI(TAG, "Disconnecting from JMRI on request; not reconnecting until asked");
            m_jsonClient->disconnect();
            m_wtClient->disconnect();
        } else if (request == Request::CONNECT) {
            saveSettings(requested);
            if (lock()) {
                m_settings = requested;
                m_autoReconnect = true;
                unlock();
            }
            ESP_LOGI(TAG, "Connecting to JMRI at %s on request", requested.serverIp.c_str());
            // Start clean: it may be a different server.
            m_jsonClient->disconnect();
            m_wtClient->disconnect();
            failedAttempts = 0;
            nextAttemptUs = 0;
        }

        Settings settings;
        bool autoReconnect = false;
        if (lock()) {
            settings = m_settings;
            autoReconnect = m_autoReconnect;
            unlock();
        }

        const bool wifiUp = m_wifiController && m_wifiController->isConnected();
        const int64_t now = esp_timer_get_time();
        if (wifiUp && !wifiWasUp) {
            // Waits for WiFi as long as it takes. The old auto-connect gave up
            // after 30 s and never started reconnecting (F-24).
            nextAttemptUs = now + WIFI_SETTLE_US;
            failedAttempts = 0;
        }
        wifiWasUp = wifiUp;

        if (autoReconnect && wifiUp && !settings.serverIp.empty()) {
            // A JSON port learned from PW: kept for the next boot, and the JSON
            // client moved to it.
            const uint16_t discovered = m_discoveredJsonPort.exchange(0);
            if (discovered != 0 && discovered != settings.jsonPort) {
                ESP_LOGI(TAG, "JMRI announced JSON port %u (was %u)", discovered, settings.jsonPort);
                saveJsonPort(discovered);
                settings.jsonPort = discovered;
                if (lock()) {
                    m_settings.jsonPort = discovered;
                    unlock();
                }
                m_jsonClient->disconnect();
            }

            const bool wtUp = m_wtClient->isConnected();
            const bool jsonUp = m_jsonClient->isConnected();

            if (wtUp && jsonUp) {
                if (failedAttempts > 0) {
                    ESP_LOGI(TAG, "JMRI connected: WiThrottle and JSON both up");
                }
                failedAttempts = 0;
                nextAttemptUs = 0;
            } else if (request == Request::CONNECT || esp_timer_get_time() >= nextAttemptUs) {
                if (!wtUp) {
                    ESP_LOGI(TAG, "Connecting WiThrottle to %s:%u", settings.serverIp.c_str(),
                             settings.wtPort);
                    m_wtClient->connect(settings.serverIp, settings.wtPort);
                }
                // Not while its own socket is still coming up: re-creating the
                // client then only restarts the attempt.
                if (!jsonUp &&
                    m_jsonClient->getState() != JmriJsonClient::ConnectionState::CONNECTING) {
                    m_jsonClient->setConfiguredPowerName(settings.powerManager);
                    m_jsonClient->connect(settings.serverIp, settings.jsonPort);
                }

                const uint32_t shift = failedAttempts < 4 ? failedAttempts : 4;
                uint32_t backoffS = RETRY_BASE_S << shift;
                if (backoffS > RETRY_MAX_S) {
                    backoffS = RETRY_MAX_S;
                }
                failedAttempts++;
                nextAttemptUs = esp_timer_get_time() + static_cast<int64_t>(backoffS) * 1000000;
            }
        }

        if (request != Request::NONE) {
            m_busy = false;
        }

        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(WORKER_TICK_MS));
    }
}
