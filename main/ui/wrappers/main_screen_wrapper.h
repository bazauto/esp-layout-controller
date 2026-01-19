#pragma once

// Forward declarations for C++
#ifdef __cplusplus
class JmriJsonClient;
class WiThrottleClient;
#else
typedef struct JmriJsonClient JmriJsonClient;
typedef struct WiThrottleClient WiThrottleClient;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise application services (C++ AppController)
 */
void init_app_controller(void);

/**
 * @brief Show the main application screen
 * 
 * This is a C wrapper for the C++ MainScreen class.
 * Creates the main screen with throttle controls.
 */
void show_main_screen(void);

#ifdef __cplusplus
}
#endif
// (No public client accessors exposed)
