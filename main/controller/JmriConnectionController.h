#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

class JmriJsonClient;
class WiThrottleClient;
class WiFiController;
class SettingsWriter;

/**
 * @brief Owns the JMRI connection: its saved settings, its reconnection, and
 *        the one task that connects and disconnects both clients.
 *
 * Every connect and disconnect -- at boot, after an outage, and from the config
 * screen -- happens on the `jmri_conn` worker task (F-34). Before, the screen
 * connected on a task of its own and disconnected on the LVGL task, the JSON
 * client was connected from the WiThrottle receive task, and all of it raced a
 * separate reconnect task, which also undid a manual disconnect within five
 * seconds.
 *
 * WiThrottle transport only: under the orchestrator the worker is never
 * started, and the screen's Connect only saves settings for later.
 */
class JmriConnectionController {
public:
    JmriConnectionController(JmriJsonClient* jsonClient, WiThrottleClient* wtClient,
                             WiFiController* wifiController, SettingsWriter* settingsWriter);
    ~JmriConnectionController();

    JmriConnectionController(const JmriConnectionController&) = delete;
    JmriConnectionController& operator=(const JmriConnectionController&) = delete;

    /**
     * @brief Load the saved settings and start keeping both clients connected.
     *
     * Waits for WiFi for as long as it takes rather than giving up after 30 s,
     * and retries with backoff for as long as the settings exist (F-24).
     * Idempotent.
     */
    void start();

    /**
     * @brief Save these settings and (re)connect both clients to them.
     *
     * Returns at once; the save and the connect happen on the worker. When the
     * worker is not running -- the orchestrator is the selected transport --
     * the settings are saved and nothing connects.
     */
    void requestConnect(const std::string& serverIp, uint16_t wtPort, const std::string& powerManager);

    /**
     * @brief Disconnect both clients and stop reconnecting until the next
     *        requestConnect() or reboot. Returns at once.
     */
    void requestDisconnect();

    /** True while the worker is carrying out a connect or disconnect request. */
    bool isBusy() const { return m_busy; }

private:
    struct Settings {
        std::string serverIp;
        uint16_t jsonPort = DEFAULT_JSON_PORT;
        uint16_t wtPort = DEFAULT_WT_PORT;
        std::string powerManager = DEFAULT_POWER_MANAGER;
    };

    enum class Request { NONE, CONNECT, DISCONNECT };

    static constexpr uint16_t DEFAULT_JSON_PORT = 12080;
    static constexpr uint16_t DEFAULT_WT_PORT = 12090;
    static constexpr const char* DEFAULT_POWER_MANAGER = "DCC++";

    static Settings loadSettings();
    static void saveSettings(const Settings& settings);
    static void saveJsonPort(uint16_t port);

    static void workerTask(void* arg);
    void runWorker();
    void wakeWorker();

    bool lock() const;
    void unlock() const;

    JmriJsonClient* m_jsonClient;
    WiThrottleClient* m_wtClient;
    WiFiController* m_wifiController;
    SettingsWriter* m_settingsWriter;

    // Guarded by m_mutex: written by the requesting task, read by the worker.
    mutable SemaphoreHandle_t m_mutex;
    Settings m_settings;
    bool m_autoReconnect;
    Request m_request;
    Settings m_requestedSettings;

    /** Web port learned from WiThrottle's PW line, 0 when none is pending.
     * Set on the receive task, which must not connect anything itself. */
    std::atomic<uint16_t> m_discoveredJsonPort;
    std::atomic<bool> m_busy;
    TaskHandle_t m_workerTask;
};
