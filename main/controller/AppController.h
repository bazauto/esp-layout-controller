#pragma once

#include <memory>

class WiThrottleClient;
class JmriJsonClient;
class ThrottleController;
class MainScreen;
class WiFiController;
class JmriConnectionController;

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
    void autoConnectJmri();

    JmriJsonClient* getJmriClient() const;
    WiThrottleClient* getWiThrottleClient() const;
    WiFiController* getWiFiController() const;
    JmriConnectionController* getJmriConnectionController() const;

private:
    AppController();

    std::unique_ptr<MainScreen> m_mainScreen;
    std::unique_ptr<WiThrottleClient> m_wiThrottleClient;
    std::unique_ptr<JmriJsonClient> m_jmriClient;
    std::unique_ptr<ThrottleController> m_throttleController;
    std::unique_ptr<WiFiController> m_wifiController;
    std::unique_ptr<JmriConnectionController> m_jmriConnectionController;
    bool m_initialised;
};
