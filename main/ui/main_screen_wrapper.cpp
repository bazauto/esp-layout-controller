#include "main_screen_wrapper.h"
#include "MainScreen.h"
#include "../communication/WiThrottleClient.h"
#include "../communication/JmriJsonClient.h"

// Global main screen instance
static MainScreen* g_mainScreen = nullptr;

// Global WiThrottle client instance
static WiThrottleClient* g_wiThrottleClient = nullptr;

// Global JMRI JSON client instance
static JmriJsonClient* g_jmriClient = nullptr;

void show_main_screen(void)
{
    // Don't delete old screen here - let LVGL handle it
    // The screen objects will be cleaned up when new screen loads
    
    // Create WiThrottle client if not already created
    if (g_wiThrottleClient == nullptr) {
        g_wiThrottleClient = new WiThrottleClient();
        g_wiThrottleClient->initialize();
        
        // TODO: Connect to JMRI WiThrottle server for locomotive control
        // g_wiThrottleClient->connect("192.168.1.100", 12090);
    }
    
    // Create JMRI JSON client if not already created
    if (g_jmriClient == nullptr) {
        g_jmriClient = new JmriJsonClient();
        g_jmriClient->initialize();
        
        // TODO: Connect to JMRI JSON server for power control
        // g_jmriClient->connect("192.168.1.100", 12080);
    }
    
    // Create new main screen (old one will be deleted by LVGL when screen changes)
    g_mainScreen = new MainScreen();
    g_mainScreen->create(g_wiThrottleClient, g_jmriClient);
}

extern "C" JmriJsonClient* get_jmri_client(void)
{
    return g_jmriClient;
}

extern "C" WiThrottleClient* get_withrottle_client(void)
{
    return g_wiThrottleClient;
}
