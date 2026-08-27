#include "settings_wrapper.h"
#include "../../controller/AppController.h"

extern "C" void show_settings_screen(void)
{
    AppController::instance().showSettingsScreen();
}
