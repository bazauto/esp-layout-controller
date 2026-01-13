#include "main_screen_wrapper.h"
#include "MainScreen.h"

// Global main screen instance
static MainScreen* g_mainScreen = nullptr;

void show_main_screen(void)
{
    // Clean up old screen if exists
    if (g_mainScreen != nullptr) {
        delete g_mainScreen;
    }
    
    // Create and show main screen
    g_mainScreen = new MainScreen();
    g_mainScreen->create();
}
