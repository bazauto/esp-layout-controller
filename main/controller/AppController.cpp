#include "AppController.h"
#include "../ui/MainScreen.h"
#include "../ui/WiFiConfigScreen.h"
#include "../ui/JmriConfigScreen.h"
#include "../communication/WiThrottleClient.h"
#include "../communication/JmriJsonClient.h"
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
    , m_wiThrottleClient(nullptr)
    , m_jmriClient(nullptr)
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

    if (m_jmriConnectionController) {
        m_jmriConnectionController->startAutoConnectTask();
    }

    if (!m_throttleController) {
        m_throttleController = std::make_unique<ThrottleController>(m_wiThrottleClient.get());
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

    auto* screen = new WiFiConfigScreen(*manager);
    screen->create();
}

void AppController::showJmriConfigScreen()
{
    initialise();

    auto* screen = new JmriConfigScreen(*m_jmriClient,
                                       *m_wiThrottleClient,
                                       m_wifiController.get(),
                                       m_rotaryEncoderHal.get());
    screen->create();
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

WiThrottleClient* AppController::getWiThrottleClient() const
{
    return m_wiThrottleClient.get();
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
