#pragma once

#include <memory>

#include "TransportSettings.h"

class WiThrottleClient;
class OrchestratorClient;
class ThrottleBackend;
class JmriJsonClient;
class ThrottleController;
class MainScreen;
class WiFiConfigScreen;
class JmriConfigScreen;
class OrchestratorConfigScreen;
class WiFiController;
class JmriConnectionController;
class RotaryEncoderHal;

/**
 * @brief Application-level controller that owns shared state and services.
 *
 * Keeps UI lifecycle separate from app state and networking.
 */
class AppController {
public:
    static AppController& instance();

    AppController(const AppController&) = delete;
    AppController& operator=(const AppController&) = delete;

    void initialise();
    void showMainScreen();
    void showWiFiConfigScreen();
    void showJmriConfigScreen();
    void showOrchestratorConfigScreen();
    void autoConnectJmri();

    JmriJsonClient* getJmriClient() const;
    WiThrottleClient* getWiThrottleClient() const;
    /** Null unless the orchestrator transport is the one selected. */
    OrchestratorClient* getOrchestratorClient() const;
    const TransportSettings& getTransportSettings() const;
    WiFiController* getWiFiController() const;
    JmriConnectionController* getJmriConnectionController() const;
    RotaryEncoderHal* getRotaryEncoderHal() const;

private:
    AppController();

    /** Logs in and fetches the roster off the LVGL task (F-05). */
    void startOrchestratorConnectTask();
    static void orchestratorConnectTask(void* arg);

    std::unique_ptr<MainScreen> m_mainScreen;
    std::unique_ptr<WiFiConfigScreen> m_wifiConfigScreen;
    std::unique_ptr<JmriConfigScreen> m_jmriConfigScreen;
    std::unique_ptr<OrchestratorConfigScreen> m_orchestratorConfigScreen;
    std::unique_ptr<WiThrottleClient> m_wiThrottleClient;
    /** Created only when the orchestrator transport is selected. */
    std::unique_ptr<OrchestratorClient> m_orchestratorClient;
    std::unique_ptr<JmriJsonClient> m_jmriClient;
    /** Held as the port, not the concrete adapter: the transport becomes a
     * runtime choice, and this is the pointer that will change. Declared after
     * the client it wraps and before the controller that borrows it, so
     * reverse-order destruction tears them down safely. */
    std::unique_ptr<ThrottleBackend> m_throttleBackend;
    std::unique_ptr<ThrottleController> m_throttleController;
    std::unique_ptr<WiFiController> m_wifiController;
    std::unique_ptr<JmriConnectionController> m_jmriConnectionController;
    std::unique_ptr<RotaryEncoderHal> m_rotaryEncoderHal;
    TransportSettings m_transportSettings;
    void* m_orchestratorTask = nullptr;
    bool m_initialised;
};
