#include "main_screen_wrapper.h"
#include "MainScreen.h"
#include "../communication/WiThrottleClient.h"
#include "../communication/JmriJsonClient.h"
#include "../controller/ThrottleController.h"
#include <memory>

// Global main screen instance
static std::unique_ptr<MainScreen> g_mainScreen;

// Global WiThrottle client instance
static std::unique_ptr<WiThrottleClient> g_wiThrottleClient;

// Global JMRI JSON client instance
static std::unique_ptr<JmriJsonClient> g_jmriClient;

// Global throttle controller instance (application layer, independent of UI)
static std::unique_ptr<ThrottleController> g_throttleController;

void show_main_screen(void)
{
    // Create WiThrottle client if not already created
    if (!g_wiThrottleClient) {
        g_wiThrottleClient = std::make_unique<WiThrottleClient>();
        g_wiThrottleClient->initialize();
        
        // TODO: Connect to JMRI WiThrottle server for locomotive control
        // g_wiThrottleClient->connect("192.168.1.100", 12090);
    }
    
    // Create JMRI JSON client if not already created
    if (!g_jmriClient) {
        g_jmriClient = std::make_unique<JmriJsonClient>();
        g_jmriClient->initialize();
        
        // TODO: Connect to JMRI JSON server for power control
        // g_jmriClient->connect("192.168.1.100", 12080);
    }
    
    // Create throttle controller if not already created (application layer - persists across UI changes)
    if (!g_throttleController) {
        g_throttleController = std::make_unique<ThrottleController>(g_wiThrottleClient.get());
        g_throttleController->initialize();
    }
    
    // Create or recreate main screen (UI can be destroyed/recreated without losing state)
    if (g_mainScreen) {
        g_mainScreen.reset();  // Safe to delete UI now that state is in controller
    }
    
    g_mainScreen = std::make_unique<MainScreen>();
    g_mainScreen->create(g_wiThrottleClient.get(), g_jmriClient.get(), g_throttleController.get());
}

extern "C" JmriJsonClient* get_jmri_client(void)
{
    return g_jmriClient.get();
}

extern "C" WiThrottleClient* get_withrottle_client(void)
{
    return g_wiThrottleClient.get();
}
