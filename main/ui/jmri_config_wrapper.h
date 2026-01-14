#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Show the JMRI configuration screen
 * 
 * This is a C wrapper for the C++ JmriConfigScreen class.
 */
void show_jmri_config_screen(void);

/**
 * @brief Check if JMRI settings are saved and attempt auto-connect
 * 
 * Call this at startup to automatically connect to JMRI if settings exist.
 * Will not crash if connection fails - just logs error and leaves disconnected.
 * 
 * @return true if auto-connect was attempted, false if no settings found
 */
bool jmri_auto_connect(void);

#ifdef __cplusplus
}
#endif
