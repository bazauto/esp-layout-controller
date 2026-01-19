#include "main_screen_wrapper.h"
#include "../../controller/AppController.h"

extern "C" void init_app_controller(void)
{
    AppController::instance().initialise();
}

extern "C" void show_main_screen(void)
{
    AppController::instance().showMainScreen();
}
