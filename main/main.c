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
#include "ui/jmri_config_wrapper.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char* TAG = "Main";

static void jmri_connect_task(void* arg)
{
    // Wait for WiFi to connect (max 30 seconds)
    for (int i = 0; i < 60; i++) {
        if (is_wifi_connected()) {
            ESP_LOGI(TAG, "WiFi connected, attempting JMRI auto-connect");
            vTaskDelay(pdMS_TO_TICKS(1000)); // Wait 1 more second for stability
            jmri_auto_connect();
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    vTaskDelete(NULL);
}

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
    
    // Start task to wait for WiFi then connect to JMRI
    xTaskCreate(jmri_connect_task, "jmri_connect", 4096, NULL, 5, NULL);
}
