#pragma once

#include <string>

class JmriJsonClient;
class WiThrottleClient;
class WiFiController;

/**
 * @brief Manages JMRI connection settings and auto-reconnect.
 */
class JmriConnectionController {
public:
    JmriConnectionController(JmriJsonClient* jsonClient, WiThrottleClient* wtClient, WiFiController* wifiController);
    ~JmriConnectionController();

    JmriConnectionController(const JmriConnectionController&) = delete;
    JmriConnectionController& operator=(const JmriConnectionController&) = delete;

    void loadSettingsAndAutoConnect();
    void enableAutoReconnect(bool enable);
    void startAutoConnectTask();

private:
    void startReconnectTask();
    static void reconnectTask(void* arg);

    JmriJsonClient* m_jsonClient;
    WiThrottleClient* m_wtClient;
    WiFiController* m_wifiController;

    bool m_autoReconnectEnabled;
    std::string m_savedServerIp;
    uint16_t m_savedJsonPort;
    uint16_t m_savedWtPort;
    std::string m_savedPowerMgr;
    void* m_reconnectTaskHandle;
    void* m_autoConnectTaskHandle;
};
