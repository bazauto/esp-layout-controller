#include "SettingsWriter.h"

#include <utility>

#include "esp_log.h"

static const char* TAG = "SettingsWriter";

SettingsWriter::SettingsWriter()
    : m_queue(nullptr)
    , m_task(nullptr)
{
}

SettingsWriter::~SettingsWriter()
{
    // Owned by the AppController singleton, which is never destroyed; this is
    // for completeness. Queued work still pending is dropped.
    if (m_task) {
        vTaskDelete(m_task);
        m_task = nullptr;
    }
    if (m_queue) {
        Work* pending = nullptr;
        while (xQueueReceive(m_queue, &pending, 0) == pdTRUE) {
            delete pending;
        }
        vQueueDelete(m_queue);
        m_queue = nullptr;
    }
}

esp_err_t SettingsWriter::start()
{
    if (m_task) {
        return ESP_OK;
    }

    // Holds pointers: a std::function is not trivially copyable, and a queue
    // copies its items byte for byte.
    m_queue = xQueueCreate(QUEUE_DEPTH, sizeof(Work*));
    if (!m_queue) {
        ESP_LOGE(TAG, "Failed to create queue");
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(taskFunc, "settings_writer", STACK_BYTES, this, PRIORITY, &m_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task");
        vQueueDelete(m_queue);
        m_queue = nullptr;
        m_task = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void SettingsWriter::post(Work work)
{
    if (!work) {
        return;
    }

    if (m_queue) {
        auto* queued = new Work(std::move(work));
        if (xQueueSend(m_queue, &queued, 0) == pdTRUE) {
            return;
        }
        ESP_LOGW(TAG, "Queue full; writing on the caller's task");
        work = std::move(*queued);
        delete queued;
    } else {
        ESP_LOGW(TAG, "Not started; writing on the caller's task");
    }
    work();
}

void SettingsWriter::taskFunc(void* arg)
{
    auto* self = static_cast<SettingsWriter*>(arg);
    for (;;) {
        Work* work = nullptr;
        if (xQueueReceive(self->m_queue, &work, portMAX_DELAY) != pdTRUE || !work) {
            continue;
        }
        (*work)();
        delete work;
    }
}
