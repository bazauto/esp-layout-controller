/*
 * ESP Layout Controller - Main Application Entry
 * 
 * This file contains the minimal C entry point for the application.
 * All business logic is implemented in C++ classes.
 * 
 * Responsibilities:
 * - Initialize hardware (LCD, touch, LVGL)
 * - Launch application UI (C++ AppController)
 * 
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: CC0-1.0
 */

#include "waveshare_rgb_lcd_port.h"
#include "ui/wrappers/main_screen_wrapper.h"
#include "esp_log.h"


#if CONFIG_THROTTLE_TESTS
extern void run_throttle_tests(void);

void app_main()
{
    run_throttle_tests();
}
#else
void app_main()
{
    // Initialize hardware
    waveshare_esp32_s3_rgb_lcd_init();

    // Initialise application services outside LVGL lock
    init_app_controller();

    // Create and display the main screen
    if (lvgl_port_lock(-1)) {
        show_main_screen();
        lvgl_port_unlock();
    }
    
}
#endif
