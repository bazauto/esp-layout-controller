#include "unity.h"

extern void register_throttle_tests(void);
extern "C" void register_controller_tests(void);
extern "C" void register_roster_tests(void);
extern "C" void register_locomotive_tests(void);
extern "C" void register_protocol_tests(void);

extern "C" void run_throttle_tests(void)
{
    UNITY_BEGIN();
    register_throttle_tests();
    register_controller_tests();
    register_roster_tests();
    register_locomotive_tests();
    register_protocol_tests();
    UNITY_END();
}
