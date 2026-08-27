#include "AppController.h"
#include "../ui/MainScreen.h"
#include "../ui/WiFiConfigScreen.h"
#include "../ui/JmriConfigScreen.h"
#include "../ui/OrchestratorConfigScreen.h"
#include "../communication/WiThrottleClient.h"
#include "../communication/WiThrottleBackend.h"
#include "../communication/OrchestratorClient.h"
#include "../communication/OrchestratorBackend.h"
#include "../communication/JmriJsonClient.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "AppController";
#include "ThrottleController.h"
#include "WiFiController.h"
#include "JmriConnectionController.h"
#include "../hardware/RotaryEncoderHal.h"

AppController& AppController::instance()
{
    static AppController instance;
    return instance;
}

AppController::AppController()
    : m_mainScreen(nullptr)
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
            m_wifiController.get());
    }

    // Only the selected transport's stack is brought up. Auto-connecting
    // WiThrottle and the JMRI JSON client while the operator has chosen the
    // orchestrator would sit there retrying a server they deliberately are not
    // using, and would light the JMRI status indicators for a link nothing
    // drives.
    if (m_jmriConnectionController && !useOrchestrator) {
        m_jmriConnectionController->startAutoConnectTask();
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

    m_initialised = true;
}

void AppController::showMainScreen()
{
    initialise();

    if (m_mainScreen) {
        m_mainScreen.reset();
    }

    m_mainScreen = std::make_unique<MainScreen>();
    m_mainScreen->create(m_wiThrottleClient.get(), m_jmriClient.get(), m_throttleController.get());
}

void AppController::showWiFiConfigScreen()
{
    initialise();

    WiFiManager* manager = m_wifiController->getManager();
    if (!manager) {
        return;
    }

    if (!m_wifiConfigScreen) {
        m_wifiConfigScreen = std::make_unique<WiFiConfigScreen>(*manager);
    }
    m_wifiConfigScreen->create();
}

void AppController::showJmriConfigScreen()
{
    initialise();

    if (!m_jmriConfigScreen) {
        m_jmriConfigScreen = std::make_unique<JmriConfigScreen>(*m_jmriClient,
                                           *m_wiThrottleClient,
                                           m_wifiController.get(),
                                           m_rotaryEncoderHal.get(),
                                           m_throttleController.get());
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
            m_orchestratorClient.get(), m_wifiController.get());
    }
    m_orchestratorConfigScreen->create();
}

void AppController::autoConnectJmri()
{
    initialise();
    if (m_jmriConnectionController) {
        m_jmriConnectionController->loadSettingsAndAutoConnect();
    }
}

JmriJsonClient* AppController::getJmriClient() const
{
    return m_jmriClient.get();
}

void AppController::orchestratorConnectTask(void* arg)
{
    auto* self = static_cast<AppController*>(arg);

    // Wait for WiFi before the login POST. Up to 30 s, matching what
    // JmriConnectionController's auto-connect task allows.
    for (int i = 0; i < 60; ++i) {
        if (self->m_wifiController && self->m_wifiController->isConnected()) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    if (!self->m_wifiController || !self->m_wifiController->isConnected()) {
        ESP_LOGW(TAG, "No WiFi; orchestrator connect abandoned");
        self->m_orchestratorTask = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    const TransportSettings& s = self->m_transportSettings;
    esp_err_t err = self->m_orchestratorClient->connect(s.host, s.port, s.username, s.password);

    if (err == ESP_OK) {
        // The roster is a REST read and needs the session cookie the login just
        // produced, so it happens here rather than on the socket's event task.
        self->m_orchestratorClient->refreshRoster();
    } else {
        ESP_LOGE(TAG, "Orchestrator connect failed: %s", esp_err_to_name(err));
    }

    self->m_orchestratorTask = nullptr;
    vTaskDelete(nullptr);
}

void AppController::startOrchestratorConnectTask()
{
    if (m_orchestratorTask != nullptr) {
        ESP_LOGW(TAG, "Orchestrator connect task already running");
        return;
    }

    // Its own task because the login is a blocking HTTP round trip and the
    // roster is two more. None of that may happen on the LVGL task (F-05).
    // 6 KB covers the TLS-capable HTTP client's stack use.
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
