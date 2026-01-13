#pragma once

#include "lvgl.h"
#include "../communication/WiFiManager.h"
#include <string>
#include <vector>

/**
 * @brief WiFi configuration screen for network setup
 * 
 * Provides UI for:
 * - Viewing current WiFi connection status and IP address
 * - Scanning for available networks
 * - Selecting a network from scan results
 * - Entering WiFi password
 * - Connecting to WiFi
 * - Disconnecting and forgetting credentials
 */
class WiFiConfigScreen {
public:
    WiFiConfigScreen(WiFiManager& wifiManager);
    ~WiFiConfigScreen();
    
    // Delete copy/move constructors
    WiFiConfigScreen(const WiFiConfigScreen&) = delete;
    WiFiConfigScreen& operator=(const WiFiConfigScreen&) = delete;
    
    /**
     * @brief Create and show the WiFi configuration screen
     * @return The LVGL screen object
     */
    lv_obj_t* create();
    
    /**
     * @brief Update the display with current WiFi status
     */
    void updateStatus();
    
    /**
     * @brief Close and clean up the screen
     */
    void close();
    
private:
    // LVGL UI components
    lv_obj_t* m_screen;
    lv_obj_t* m_statusLabel;
    lv_obj_t* m_ssidLabel;
    lv_obj_t* m_ipLabel;
    lv_obj_t* m_networkList;
    lv_obj_t* m_ssidInput;
    lv_obj_t* m_passwordInput;
    lv_obj_t* m_scanButton;
    lv_obj_t* m_connectButton;
    lv_obj_t* m_disconnectButton;
    lv_obj_t* m_forgetButton;
    lv_obj_t* m_backButton;
    lv_obj_t* m_keyboard;
    lv_obj_t* m_keyboardLabel;  // Label to show which field is being edited
    
    // WiFi manager reference
    WiFiManager& m_wifiManager;
    
    // Cached scan results
    std::vector<std::string> m_scanResults;
    
    // Event handlers
    static void onScanButtonClicked(lv_event_t* e);
    static void onConnectButtonClicked(lv_event_t* e);
    static void onDisconnectButtonClicked(lv_event_t* e);
    static void onForgetButtonClicked(lv_event_t* e);
    static void onBackButtonClicked(lv_event_t* e);
    static void onNetworkSelected(lv_event_t* e);
    static void onWiFiStateChanged(void* userData, WiFiManager::State state);
    static void onTextAreaFocused(lv_event_t* e);
    static void onTextAreaDefocused(lv_event_t* e);
    
    // UI builders
    void createStatusSection(lv_obj_t* parent);
    void createNetworkListSection(lv_obj_t* parent);
    void createInputSection(lv_obj_t* parent);
    void createButtonSection(lv_obj_t* parent);
    void createKeyboard();
    
    // Keyboard helpers
    void showKeyboard(lv_obj_t* textarea);
    void hideKeyboard();
    
    // Helper methods
    void performScan();
    void updateNetworkList();
    void connectToWiFi();
    void disconnectWiFi();
    void forgetWiFi();
    std::string getPasswordText() const;
    std::string getSsidText() const;
};
