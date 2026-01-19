#include "jmri_config_wrapper.h"
#include "main_screen_wrapper.h"
#include "../../controller/AppController.h"

extern "C" void show_jmri_config_screen(void)
{
    AppController::instance().showJmriConfigScreen();
}

extern "C" bool jmri_auto_connect(void)
{
    AppController::instance().autoConnectJmri();
    return true;
}
