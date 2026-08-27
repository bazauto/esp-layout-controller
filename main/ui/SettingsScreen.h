#pragma once

#include <string>

#include "lvgl.h"
#include "../controller/TransportSettings.h"

class ThrottleController;
class WiFiController;
class RotaryEncoderHal;
class JmriJsonClient;
class WiThrottleClient;

/**
 * @brief Device settings, and the front door to each transport's own config.
 *
 * Exists because the JMRI config screen had grown into two unrelated things: it
 * was titled "JMRI Server Configuration" while owning the *global* choice of
 * transport, and its System Status section knew nothing about the orchestrator.
 *
 * The split now is:
 *
 *   - This screen: which transport to use, device-wide settings, system status,
 *     and a button through to each transport's own configuration.
 *   - JmriConfigScreen: JMRI server address, ports and power manager. Only JMRI.
 *   - OrchestratorConfigScreen: orchestrator host and operator credential.
 *
 * The status rows follow the *selected* transport, so the screen never reports
 * on a link the device is not bringing up.
 */
class SettingsScreen {
public:
    SettingsScreen(ThrottleController* throttleController,
                   WiFiController* wifiController,
                   RotaryEncoderHal* encoderHal,
                   JmriJsonClient* jsonClient,
                   WiThrottleClient* wiThrottleClient);
    ~SettingsScreen();

    SettingsScreen(const SettingsScreen&) = delete;
    SettingsScreen& operator=(const SettingsScreen&) = delete;

    lv_obj_t* create();
    void updateStatus();

private:
    void createTitle(lv_obj_t* parent);
    void createTransportSection(lv_obj_t* parent);
    void createDeviceSection(lv_obj_t* parent);
    void createSystemStatusSection(lv_obj_t* parent);
    void createButtonSection(lv_obj_t* parent);
    void createKeyboard();

    void addStatusRow(lv_obj_t* parent, const char* label, lv_obj_t** valueLabel);

    void showKeyboard(lv_obj_t* textarea);
    void hideKeyboard();

    void loadSettings();
    void saveSpeedSteps();
    void clearUiPointers();
    void leaveFor(void (*navigate)());

    static void onTransportChanged(lv_event_t* e);
    static void onJmriSettingsClicked(lv_event_t* e);
    static void onOrchestratorSettingsClicked(lv_event_t* e);
    static void onBackClicked(lv_event_t* e);
    static void onTextAreaFocused(lv_event_t* e);
    static void onTextAreaDefocused(lv_event_t* e);
    static void statusTimerCb(lv_timer_t* timer);
    void stopStatusTimer();

    lv_obj_t* m_screen;
    lv_obj_t* m_transportDropdown;
    lv_obj_t* m_transportNoteLabel;
    lv_obj_t* m_speedStepsInput;
    lv_obj_t* m_keyboard;
    lv_timer_t* m_statusTimer;

    lv_obj_t* m_statusSoftwareValue;
    lv_obj_t* m_statusHardwareValue;
    lv_obj_t* m_statusWifiValue;
    lv_obj_t* m_statusTransportValue;
    /** Row two of the transport block: WiThrottle, or the orchestrator link. */
    lv_obj_t* m_statusLinkValue;
    /** Only created under JMRI, which has a second connection. */
    lv_obj_t* m_statusJsonValue;
    lv_obj_t* m_statusEncoder1Value;
    lv_obj_t* m_statusEncoder2Value;

    ThrottleController* m_throttleController;
    WiFiController* m_wifiController;
    RotaryEncoderHal* m_encoderHal;
    JmriJsonClient* m_jsonClient;
    WiThrottleClient* m_wiThrottleClient;

    /** Captured at create() so the rows built match the rows updated. */
    ThrottleTransport m_shownTransport;

    static constexpr int SCREEN_WIDTH = 800;
    static constexpr int SCREEN_HEIGHT = 480;
    static constexpr int PADDING = 10;
    static constexpr int BUTTON_HEIGHT = 50;
};
