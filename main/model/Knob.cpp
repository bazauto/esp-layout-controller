#include "Knob.h"
#include "esp_log.h"

static const char* TAG = "Knob";

Knob::Knob(int id)
    : m_id(id)
    , m_state(State::IDLE)
    , m_assignedThrottleId(-1)
    , m_rosterIndex(0)
{
}

void Knob::assignToThrottle(int throttleId)
{
    if (m_state != State::IDLE) {
        ESP_LOGW(TAG, "Knob %d already assigned, releasing first", m_id);
        release();
    }
    
    m_assignedThrottleId = throttleId;
    m_state = State::SELECTING;
    m_rosterIndex = 0;
    
    ESP_LOGI(TAG, "Knob %d assigned to throttle %d (SELECTING)", m_id, throttleId);
}

void Knob::startControlling()
{
    if (m_state != State::SELECTING) {
        ESP_LOGW(TAG, "Knob %d not in SELECTING state, cannot start controlling", m_id);
        return;
    }
    
    m_state = State::CONTROLLING;
    ESP_LOGI(TAG, "Knob %d now CONTROLLING throttle %d", m_id, m_assignedThrottleId);
}

void Knob::release()
{
    ESP_LOGI(TAG, "Knob %d released from throttle %d", m_id, m_assignedThrottleId);
    
    m_state = State::IDLE;
    m_assignedThrottleId = -1;
    m_rosterIndex = 0;
}

void Knob::handleRotation(int delta, int rosterSize)
{
    if (m_state != State::SELECTING) {
        // Rotation only affects roster selection, ignore in other states
        return;
    }
    
    if (rosterSize == 0) {
        return;
    }
    
    // Update roster index with wrapping
    m_rosterIndex += delta;
    
    // Wrap around
    while (m_rosterIndex < 0) {
        m_rosterIndex += rosterSize;
    }
    while (m_rosterIndex >= rosterSize) {
        m_rosterIndex -= rosterSize;
    }
    
    ESP_LOGD(TAG, "Knob %d roster index: %d (delta=%d)", m_id, m_rosterIndex, delta);
}

void Knob::resetRosterIndex()
{
    m_rosterIndex = 0;
}
