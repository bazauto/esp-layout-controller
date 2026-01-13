/*
 * ESP Layout Controller - Main Application Entry
 * 
 * This file contains the minimal C entry point for the application.
 * All business logic is implemented in C++ classes.
 * 
 * Responsibilities:
 * - Initialize hardware (LCD, touch, LVGL)
 * - Initialize WiFi and auto-connect
 * - Launch main screen
 * 
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: CC0-1.0
 */

#include "waveshare_rgb_lcd_port.h"
#include "ui/main_screen_wrapper.h"
#include "ui/wifi_config_wrapper.h"

void app_main()
{
    // Initialize hardware
    waveshare_esp32_s3_rgb_lcd_init();
    
    // Initialize WiFi and attempt auto-connect
    init_wifi_manager();
    
    // Create and display the main screen
    if (lvgl_port_lock(-1)) {
        show_main_screen();
        lvgl_port_unlock();
    }
}
