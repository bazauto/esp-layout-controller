#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize WiFi manager and attempt auto-connect
 * 
 * Should be called once at application startup.
 * Initializes the WiFi system and attempts to connect using stored credentials.
 */
void init_wifi_manager(void);

/**
 * @brief Show the WiFi configuration screen
 * 
 * This is a C wrapper for the C++ WiFiConfigScreen class.
 * Creates and displays the WiFi configuration UI.
 */
void show_wifi_config_screen(void);

/**
 * @brief Close the WiFi configuration screen and return to previous screen
 * 
 * Safely closes the WiFi config screen and restores the previous screen.
 */
void close_wifi_config_screen(void);

/**
 * @brief Check if WiFi is connected
 * @return true if WiFi is connected, false otherwise
 */
bool is_wifi_connected(void);

#ifdef __cplusplus
}
#endif
