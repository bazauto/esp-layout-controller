#include "AppController.h"
#include "../ui/MainScreen.h"
#include "../ui/WiFiConfigScreen.h"
#include "../ui/JmriConfigScreen.h"
#include "../ui/OrchestratorConfigScreen.h"
#include "../ui/SettingsScreen.h"
#include "../communication/WiThrottleClient.h"
#include "../communication/WiThrottleBackend.h"
#include "../communication/OrchestratorClient.h"
#include "../communication/OrchestratorBackend.h"
#include "../communication/JmriJsonClient.h"
#include "ThrottleController.h"
#include "WiFiController.h"
#include "JmriConnectionController.h"
#include "SettingsWriter.h"
#include "../utils/StackReport.h"
#include "../hardware/RotaryEncoderHal.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "AppController";

namespace {

/** How often the orchestrator supervisor looks at the link. A Connect press
 * wakes it at once. */
constexpr uint32_t ORCH_SUPERVISE_TICK_MS = 1000;

/** How long a dropped socket's own reconnect -- which reuses the session
 * cookie -- gets before the supervisor logs in afresh. */
constexpr int64_t ORCH_RELOGIN_AFTER_US = 30LL * 1000 * 1000;

/** Backoff between failed logins: 5 s, doubling, capped at a minute. The
 * orchestrator allows five logins a minute; this stays under it. */
constexpr uint32_t ORCH_RETRY_MIN_MS = 5000;
constexpr uint32_t ORCH_RETRY_MAX_MS = 60000;

/** How often a roster read that failed after a good login is retried. */
constexpr int64_t ORCH_ROSTER_RETRY_US = 30LL * 1000 * 1000;

}  // namespace

AppController& AppController::instance()
{
    static AppController instance;
    return instance;
}

AppController::AppController()
    : m_settingsWriter(nullptr)
    , m_mainScreen(nullptr)
    , m_wifiConfigScreen(nullptr)
    , m_jmriConfigScreen(nullptr)
    , m_wiThrottleClient(nullptr)
    , m_orchestratorClient(nullptr)
    , m_jmriClient(nullptr)
    , m_throttleBackend(nullptr)
    , m_throttleController(nullptr)
    , m_wifiController(nullptr)
    , m_jmriConnectionController(nullptr)
    , m_rotaryEncoderHal(nullptr)
    , m_initialised(false)
{
}

void AppController::initialise()
{
    if (m_initialised) {
        return;
    }

    // First: every screen and the JMRI controller hand their NVS writes to it.
    if (!m_settingsWriter) {
        m_settingsWriter = std::make_unique<SettingsWriter>();
        m_settingsWriter->start();
    }

    if (!m_wifiController) {
        m_wifiController = std::make_unique<WiFiController>();
        m_wifiController->autoConnect();
    }

    // Loaded before anything connects: it decides which network stack is
    // allowed to come up at all, not merely which one drives locos.
    m_transportSettings = TransportSettings::load();
    const bool useOrchestrator =
        (m_transportSettings.transport == ThrottleTransport::ORCHESTRATOR);

    if (!m_wiThrottleClient) {
        m_wiThrottleClient = std::make_unique<WiThrottleClient>();
        m_wiThrottleClient->initialize();
    }

    if (!m_jmriClient) {
        m_jmriClient = std::make_unique<JmriJsonClient>();
        m_jmriClient->initialize();
    }

    if (!m_jmriConnectionController) {
        m_jmriConnectionController = std::make_unique<JmriConnectionController>(
            m_jmriClient.get(),
            m_wiThrottleClient.get(),
            m_wifiController.get(),
            m_settingsWriter.get());
    }

    // Only the selected transport's stack is brought up. Auto-connecting
    // WiThrottle and the JMRI JSON client while the operator has chosen the
    // orchestrator would sit there retrying a server they deliberately are not
    // using, and would light the JMRI status indicators for a link nothing
    // drives.
    if (m_jmriConnectionController && !useOrchestrator) {
        m_jmriConnectionController->start();
    } else if (useOrchestrator) {
        ESP_LOGI(TAG, "Orchestrator transport selected; JMRI auto-connect not started");
    }

    // The adapter must outlive the controller that holds a raw pointer to it,
    // and both are destroyed with this singleton, so declaration order in the
    // header is what guarantees it.
    if (!m_throttleBackend) {
        if (useOrchestrator) {
            m_orchestratorClient = std::make_unique<OrchestratorClient>();
            m_orchestratorClient->initialize();
            m_throttleBackend = std::make_unique<OrchestratorBackend>(m_orchestratorClient.get());
            startOrchestratorConnectTask();
        } else {
            // The JSON client comes in for track power only: under JMRI that
            // has always gone over the JSON API rather than WiThrottle's PPA.
            m_throttleBackend = std::make_unique<WiThrottleBackend>(m_wiThrottleClient.get(),
                                                                    m_jmriClient.get());
        }

        ESP_LOGI(TAG, "Throttle transport: %s",
                 TransportSettings::transportName(m_transportSettings.transport));
    }

    if (!m_throttleController) {
        m_throttleController = std::make_unique<ThrottleController>(m_throttleBackend.get());
        m_throttleController->initialize();
    }

    if (!m_rotaryEncoderHal) {
        m_rotaryEncoderHal = std::make_unique<RotaryEncoderHal>();
        m_rotaryEncoderHal->initialise();
        m_rotaryEncoderHal->setRotationCallback(
            [this](int knobId, int delta) {
                if (m_throttleController) {
                    m_throttleController->onKnobRotation(knobId, delta);
                }
            }
        );
        m_rotaryEncoderHal->setPressCallback(
            [this](int knobId, bool pressed) {
                if (pressed && m_throttleController) {
                    m_throttleController->onKnobPress(knobId);
                }
            }
        );
        m_rotaryEncoderHal->startPollingTask();
    }

    // Nothing unless CONFIG_THROTTLE_STACK_REPORT is set (F-33).
    StackReport::start();

    m_initialised = true;
}

void AppController::showMainScreen()
{
    initialise();

    // Built once and re-shown, never rebuilt. Rebuilding destroyed the old
    // screen from inside an LVGL handler, while an encoder or network task
    // could already hold its pointer and be waiting on the LVGL lock to repaint
    // it -- a use-after-free with trains running (F-21). It also leaked the
    // whole LVGL tree each time, since lv_scr_load frees nothing (F-32).
    if (m_mainScreen) {
        m_mainScreen->show();
        return;
    }

    m_mainScreen = std::make_unique<MainScreen>();
    m_mainScreen->create(m_throttleController.get());
}

void AppController::showWiFiConfigScreen()
{
    initialise();

    WiFiManager* manager = m_wifiController->getManager();
    if (!manager) {
        return;
    }

    if (!m_wifiConfigScreen) {
        m_wifiConfigScreen = std::make_unique<WiFiConfigScreen>(*manager,
                                                                m_settingsWriter.get());
    }
    m_wifiConfigScreen->create();
}

void AppController::showSettingsScreen()
{
    initialise();

    // Rebuilt each time rather than kept: the status rows it shows depend on
    // the selected transport, and that can change while the device runs.
    m_settingsScreen = std::make_unique<SettingsScreen>(m_throttleController.get(),
                                                        m_wifiController.get(),
                                                        m_rotaryEncoderHal.get(),
                                                        m_jmriClient.get(),
                                                        m_wiThrottleClient.get(),
                                                        m_settingsWriter.get());
    m_settingsScreen->create();
}

void AppController::showJmriConfigScreen()
{
    initialise();

    if (!m_jmriConfigScreen) {
        m_jmriConfigScreen = std::make_unique<JmriConfigScreen>(*m_jmriClient,
                                                                *m_wiThrottleClient,
                                                                m_jmriConnectionController.get());
    }
    m_jmriConfigScreen->create();
}

void AppController::showOrchestratorConfigScreen()
{
    initialise();

    if (!m_orchestratorConfigScreen) {
        // The client is null unless the orchestrator is the selected transport.
        // The screen handles that and still lets the settings be edited, so a
        // switch can be configured before it is switched to.
        m_orchestratorConfigScreen = std::make_unique<OrchestratorConfigScreen>(
            m_orchestratorClient.get(), m_wifiController.get(), m_settingsWriter.get());
    }
    m_orchestratorConfigScreen->create();
}

void AppController::autoConnectJmri()
{
    initialise();
    // Idempotent, and only ever for the transport the operator chose.
    if (m_jmriConnectionController &&
        m_transportSettings.transport == ThrottleTransport::WITHROTTLE) {
        m_jmriConnectionController->start();
    }
}

void AppController::requestOrchestratorReconnect()
{
    if (m_orchestratorTask) {
        xTaskNotifyGive(static_cast<TaskHandle_t>(m_orchestratorTask));
    } else {
        ESP_LOGW(TAG, "Orchestrator is not the selected transport; nothing to reconnect");
    }
}

JmriJsonClient* AppController::getJmriClient() const
{
    return m_jmriClient.get();
}

void AppController::orchestratorConnectTask(void* arg)
{
    auto* self = static_cast<AppController*>(arg);
    OrchestratorClient* client = self->m_orchestratorClient.get();

    // Read on this task only. Reloaded when the operator presses Connect,
    // which is how changed settings arrive.
    TransportSettings settings = self->m_transportSettings;

    int64_t nextLoginUs = 0;          // due at once
    uint32_t backoffMs = ORCH_RETRY_MIN_MS;
    bool forceLogin = false;          // set by a Connect press
    bool rosterLoaded = false;
    int64_t nextRosterUs = 0;

    // Never exits. It used to try once and give up: an orchestrator host that
    // booted after the ESP -- the normal case with the layout on one switch --
    // left every throttle dead until someone went into settings (F-24).
    for (;;) {
        const bool wifiUp = self->m_wifiController && self->m_wifiController->isConnected();
        const bool linkUp = client->isConnected();
        const int64_t now = esp_timer_get_time();

        if (linkUp && !forceLogin) {
            // Healthy. Keep the fresh-login deadline a grace period out, so a
            // dropped socket first gets its own reconnect before it is replaced.
            nextLoginUs = now + ORCH_RELOGIN_AFTER_US;
            backoffMs = ORCH_RETRY_MIN_MS;

            if (!rosterLoaded && now >= nextRosterUs) {
                rosterLoaded = (client->refreshRoster() == ESP_OK);
                nextRosterUs = esp_timer_get_time() + ORCH_ROSTER_RETRY_US;
            }
        } else if (wifiUp && (forceLogin || now >= nextLoginUs)) {
            if (forceLogin) {
                settings = TransportSettings::load();
                forceLogin = false;
            }

            ESP_LOGI(TAG, "Logging in to the orchestrator at %s:%u",
                     settings.host.c_str(), static_cast<unsigned>(settings.port));
            const esp_err_t err =
                client->connect(settings.host, settings.port, settings.username, settings.password);

            if (err == ESP_OK) {
                // A REST read that needs the cookie the login just produced, so
                // it happens here rather than on the socket's event task.
                rosterLoaded = (client->refreshRoster() == ESP_OK);
                nextRosterUs = esp_timer_get_time() + ORCH_ROSTER_RETRY_US;
                backoffMs = ORCH_RETRY_MIN_MS;
                nextLoginUs = esp_timer_get_time() + ORCH_RELOGIN_AFTER_US;
            } else {
                ESP_LOGW(TAG, "Orchestrator connect failed (%s); retrying in %lu s",
                         esp_err_to_name(err), static_cast<unsigned long>(backoffMs / 1000));
                nextLoginUs = esp_timer_get_time() + static_cast<int64_t>(backoffMs) * 1000;
                backoffMs = (backoffMs * 2 > ORCH_RETRY_MAX_MS) ? ORCH_RETRY_MAX_MS : backoffMs * 2;
            }
        }

        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ORCH_SUPERVISE_TICK_MS)) > 0) {
            forceLogin = true;
        }
    }
}

void AppController::startOrchestratorConnectTask()
{
    if (m_orchestratorTask != nullptr) {
        ESP_LOGW(TAG, "Orchestrator connect task already running");
        return;
    }

    // Its own task because the login is a blocking HTTP round trip and the
    // roster is two more. None of that may happen on the LVGL task (F-05).
    // 6 KB covers the TLS-capable HTTP client's stack use. It runs for the
    // life of the application.
    TaskHandle_t handle = nullptr;
    BaseType_t ret = xTaskCreate(orchestratorConnectTask, "orch_connect", 6144, this, 5, &handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create orchestrator connect task");
        m_orchestratorTask = nullptr;
        return;
    }
    m_orchestratorTask = handle;
}

WiThrottleClient* AppController::getWiThrottleClient() const
{
    return m_wiThrottleClient.get();
}

OrchestratorClient* AppController::getOrchestratorClient() const
{
    return m_orchestratorClient.get();
}

const TransportSettings& AppController::getTransportSettings() const
{
    return m_transportSettings;
}

WiFiController* AppController::getWiFiController() const
{
    return m_wifiController.get();
}

JmriConnectionController* AppController::getJmriConnectionController() const
{
    return m_jmriConnectionController.get();
}

RotaryEncoderHal* AppController::getRotaryEncoderHal() const
{
    return m_rotaryEncoderHal.get();
}

SettingsWriter* AppController::getSettingsWriter() const
{
    return m_settingsWriter.get();
}
