#pragma once

#include "lvgl.h"
#include "../communication/JmriJsonClient.h"
#include "../communication/WiThrottleClient.h"
#include <string>

/**
 * @brief JMRI Server configuration screen
 * 
 * Allows user to:
 * - Configure JMRI server IP address
 * - Configure JSON API port (default 12080)
 * - Configure WiThrottle port (default 12090)
 * - View connection status
 * - Connect/Disconnect from server
 * 
 * Settings are persisted in NVS and auto-connect at startup
 */
class WiFiController;
class RotaryEncoderHal;

class JmriConfigScreen {
public:
    explicit JmriConfigScreen(JmriJsonClient& jsonClient,
                              WiThrottleClient& wiThrottleClient,
                              WiFiController* wifiController,
                              RotaryEncoderHal* encoderHal);
    ~JmriConfigScreen();
    
    // Delete copy/move
    JmriConfigScreen(const JmriConfigScreen&) = delete;
    JmriConfigScreen& operator=(const JmriConfigScreen&) = delete;
    
    /**
     * @brief Create and show the JMRI config screen
     * @return The LVGL screen object
     */
    lv_obj_t* create();
    
    /**
     * @brief Update connection status display
     */
    void updateStatus();
    
private:
    void createStatusSection(lv_obj_t* parent);
    void createConfigSection(lv_obj_t* parent);
    void createSystemStatusSection(lv_obj_t* parent);
    void createButtonSection(lv_obj_t* parent);
    void createKeyboard();

    void addStatusRow(lv_obj_t* parent, const char* label, lv_obj_t** valueLabel);
    
    void showKeyboard(lv_obj_t* textarea);
    void hideKeyboard();
    
    void connectToJmri();
    void disconnectFromJmri();
    void saveSettings();
    void loadSettings();
    
    std::string getServerIpText() const;
    std::string getWiThrottlePortText() const;
    std::string getPowerManagerText() const;
    
    // Event handlers
    static void onConnectButtonClicked(lv_event_t* e);
    static void onDisconnectButtonClicked(lv_event_t* e);
    static void onBackButtonClicked(lv_event_t* e);
    static void onTextAreaFocused(lv_event_t* e);
    static void onTextAreaDefocused(lv_event_t* e);
    
    // LVGL objects
    lv_obj_t* m_screen;
    lv_obj_t* m_serverIpInput;
    lv_obj_t* m_wiThrottlePortInput;
    lv_obj_t* m_powerManagerInput;
    lv_obj_t* m_speedStepsInput;
    lv_obj_t* m_statusWifiValue;
    lv_obj_t* m_statusWiThrottleValue;
    lv_obj_t* m_statusJsonValue;
    lv_obj_t* m_statusEncoder1Value;
    lv_obj_t* m_statusEncoder2Value;
    lv_obj_t* m_statusSoftwareValue;
    lv_obj_t* m_statusHardwareValue;
    lv_obj_t* m_connectButton;
    lv_obj_t* m_disconnectButton;
    lv_obj_t* m_backButton;
    lv_obj_t* m_keyboard;
    lv_obj_t* m_keyboardLabel;
    
    // Client references
    JmriJsonClient& m_jsonClient;
    WiThrottleClient& m_wiThrottleClient;
    WiFiController* m_wifiController;
    RotaryEncoderHal* m_encoderHal;
    
    // Constants
    static constexpr int SCREEN_WIDTH = 800;
    static constexpr int SCREEN_HEIGHT = 480;
    static constexpr int PADDING = 10;
    static constexpr int BUTTON_HEIGHT = 50;
    static constexpr int STATUS_ROW_HEIGHT = 26;
};
