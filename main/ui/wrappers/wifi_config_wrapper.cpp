#include "wifi_config_wrapper.h"
#include "main_screen_wrapper.h"
#include "../../controller/AppController.h"
#include "../../controller/WiFiController.h"
#include "lvgl.h"

void init_wifi_manager(void)
{
    AppController::instance().initialise();
}

void show_wifi_config_screen(void)
{
    AppController::instance().showWiFiConfigScreen();
}

void close_wifi_config_screen(void)
{
    show_main_screen();
}

bool is_wifi_connected(void)
{
    auto* controller = AppController::instance().getWiFiController();
    return controller ? controller->isConnected() : false;
}
