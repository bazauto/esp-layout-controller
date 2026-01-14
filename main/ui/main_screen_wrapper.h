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
 * @brief Initialize and show the main application screen
 * 
 * This is a C wrapper for the C++ MainScreen class.
 * Creates the main screen with throttle controls.
 */
void show_main_screen(void);

/**
 * @brief Get the global JMRI JSON client instance
 * @return Pointer to the JMRI JSON client or nullptr if not initialized
 */
JmriJsonClient* get_jmri_client(void);

/**
 * @brief Get the global WiThrottle client instance
 * @return Pointer to the WiThrottle client or nullptr if not initialized
 */
WiThrottleClient* get_withrottle_client(void);

#ifdef __cplusplus
}
#endif
