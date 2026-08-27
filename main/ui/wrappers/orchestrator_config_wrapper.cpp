#include "orchestrator_config_wrapper.h"
#include "../../controller/AppController.h"

extern "C" void show_orchestrator_config_screen(void)
{
    AppController::instance().showOrchestratorConfigScreen();
}
