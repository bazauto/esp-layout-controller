#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Show the device settings screen.
 *
 * C wrapper for the C++ SettingsScreen class. This is the screen the main
 * screen's settings button opens; the per-transport config screens hang off it.
 */
void show_settings_screen(void);

#ifdef __cplusplus
}
#endif
