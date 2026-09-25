#pragma once

#include <functional>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

/**
 * @brief Carries out the UI's NVS writes off the LVGL task, in the order asked.
 *
 * An LVGL event handler must not block (F-05), and an NVS write can when it
 * has to erase a page first. The screens hand their writes here instead
 * (F-39).
 *
 * One task and a queue rather than a task per write: two saves of the same
 * setting, made in quick succession, then land in the order they were made.
 * Work that must follow a save -- waking the orchestrator supervisor to log
 * in with it -- is posted after the save for the same reason.
 *
 * The task's stack is internal RAM, as it must be: flash writes disable the
 * cache, and PSRAM with it.
 */
class SettingsWriter {
public:
    using Work = std::function<void()>;

    SettingsWriter();
    ~SettingsWriter();

    SettingsWriter(const SettingsWriter&) = delete;
    SettingsWriter& operator=(const SettingsWriter&) = delete;

    /** Creates the queue and the task. Idempotent. */
    esp_err_t start();

    /**
     * @brief Queues work to run on the writer task.
     *
     * If the writer never started or its queue is full, the work runs here
     * instead, with a warning: a stalled frame is better than a lost save.
     */
    void post(Work work);

private:
    static void taskFunc(void* arg);

    static constexpr int QUEUE_DEPTH = 8;
    static constexpr uint32_t STACK_BYTES = 4096;
    static constexpr UBaseType_t PRIORITY = 3;

    QueueHandle_t m_queue;
    TaskHandle_t m_task;
};
