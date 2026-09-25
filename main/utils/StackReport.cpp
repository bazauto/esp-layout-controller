#include "StackReport.h"

#include "sdkconfig.h"

#if CONFIG_THROTTLE_STACK_REPORT

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "StackReport";

namespace {

constexpr uint32_t REPORT_PERIOD_MS = 10000;

/**
 * Every long-lived task this application creates or runs code on. The IDF
 * ones are here because callbacks of ours execute on them: WiFi events on
 * sys_evt, the WiFi retry timer on esp_timer, both WebSocket clients on
 * websocket_task. A name not currently running is skipped.
 */
const char* const TASK_NAMES[] = {
    "main",           "lvgl",          "rotary_enc",     "withrottle_rx",
    "jmri_heartbeat", "jmri_conn",     "throttle_poll",  "orch_connect",
    "websocket_task", "settings_writer", "stack_report", "sys_evt",
    "esp_timer",      "tiT",
};

void reportTask(void*)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(REPORT_PERIOD_MS));
        for (const char* name : TASK_NAMES) {
            TaskHandle_t handle = xTaskGetHandle(name);
            if (!handle) {
                continue;
            }
            // Bytes, not words, on ESP-IDF: the least free stack the task has
            // ever had.
            ESP_LOGI(TAG, "%-16s %5u bytes never used", name,
                     static_cast<unsigned>(uxTaskGetStackHighWaterMark(handle)));
        }
    }
}

}  // namespace

void StackReport::start()
{
    static bool started = false;
    if (started) {
        return;
    }
    if (xTaskCreate(reportTask, "stack_report", 3072, nullptr, 1, nullptr) == pdPASS) {
        started = true;
        ESP_LOGW(TAG, "Stack report on (CONFIG_THROTTLE_STACK_REPORT)");
    } else {
        ESP_LOGE(TAG, "Failed to create stack report task");
    }
}

void StackReport::logSelf()
{
    ESP_LOGI(TAG, "%-16s %5u bytes never used (one-shot)", pcTaskGetName(nullptr),
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

#else

void StackReport::start() {}

void StackReport::logSelf() {}

#endif
