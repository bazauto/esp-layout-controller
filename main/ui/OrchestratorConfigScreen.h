#pragma once

#include <atomic>
#include <string>

#include "lvgl.h"
#include "../controller/TransportSettings.h"

class OrchestratorClient;
class WiFiController;

/**
 * @brief Layout orchestrator connection settings.
 *
 * Its own screen rather than a section of the JMRI one: the two transports are
 * peers, and the orchestrator needs four fields of its own including a
 * credential. The transport *choice* lives on the JMRI/settings screen; this
 * screen only configures the orchestrator side of it.
 *
 * The password is stored in plaintext NVS -- the same accepted and documented
 * risk as the WiFi password (F-18).
 */
class OrchestratorConfigScreen {
public:
    OrchestratorConfigScreen(OrchestratorClient* client, WiFiController* wifiController);
    ~OrchestratorConfigScreen();

    OrchestratorConfigScreen(const OrchestratorConfigScreen&) = delete;
    OrchestratorConfigScreen& operator=(const OrchestratorConfigScreen&) = delete;

    lv_obj_t* create();
    void updateStatus();

private:
    void createHeader(lv_obj_t* parent);
    void createFields(lv_obj_t* parent);
    void createButtons(lv_obj_t* parent);
    void createKeyboard();

    void showKeyboard(lv_obj_t* textarea);
    void hideKeyboard();

    void loadSettings();
    void saveSettings();
    void clearUiPointers();

    std::string textOf(lv_obj_t* textarea) const;

    static void onSaveClicked(lv_event_t* e);
    static void onConnectClicked(lv_event_t* e);
    static void onBackClicked(lv_event_t* e);
    static void onTextAreaFocused(lv_event_t* e);
    static void onTextAreaDefocused(lv_event_t* e);

    /** Login is a blocking HTTP round trip, so it never runs on the LVGL task (F-05). */
    static void connectTask(void* arg);

    static void statusTimerCb(lv_timer_t* timer);
    void stopStatusTimer();

    lv_obj_t* m_screen;
    lv_obj_t* m_hostInput;
    lv_obj_t* m_portInput;
    lv_obj_t* m_usernameInput;
    lv_obj_t* m_passwordInput;
    lv_obj_t* m_statusValue;
    lv_obj_t* m_connectButton;
    lv_obj_t* m_keyboard;
    lv_timer_t* m_statusTimer;

    std::atomic<bool> m_connectInProgress;

    OrchestratorClient* m_client;
    WiFiController* m_wifiController;

    static constexpr int PADDING = 10;
    static constexpr int BUTTON_HEIGHT = 50;
};
