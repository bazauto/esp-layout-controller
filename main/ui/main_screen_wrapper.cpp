#include "main_screen_wrapper.h"
#include "MainScreen.h"
#include "../communication/WiThrottleClient.h"
#include "../communication/JmriJsonClient.h"
#include "../controller/ThrottleController.h"

// Global main screen instance
static MainScreen* g_mainScreen = nullptr;

// Global WiThrottle client instance
static WiThrottleClient* g_wiThrottleClient = nullptr;

// Global JMRI JSON client instance
static JmriJsonClient* g_jmriClient = nullptr;

// Global throttle controller instance (application layer, independent of UI)
static ThrottleController* g_throttleController = nullptr;

void show_main_screen(void)
{
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
    
    // Create throttle controller if not already created (application layer - persists across UI changes)
    if (g_throttleController == nullptr) {
        g_throttleController = new ThrottleController(g_wiThrottleClient);
        g_throttleController->initialize();
    }
    
    // Create or recreate main screen (UI can be destroyed/recreated without losing state)
    if (g_mainScreen != nullptr) {
        delete g_mainScreen;  // Safe to delete UI now that state is in controller
    }
    
    g_mainScreen = new MainScreen();
    g_mainScreen->create(g_wiThrottleClient, g_jmriClient, g_throttleController);
}

extern "C" JmriJsonClient* get_jmri_client(void)
{
    return g_jmriClient;
}

extern "C" WiThrottleClient* get_withrottle_client(void)
{
    return g_wiThrottleClient;
}
