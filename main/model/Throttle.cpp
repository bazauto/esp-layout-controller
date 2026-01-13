#include "Throttle.h"

Throttle::Throttle()
    : m_throttleId(0)
    , m_state(State::UNALLOCATED)
    , m_assignedKnob(KNOB_NONE)
    , m_locomotive(nullptr)
{
}

Throttle::Throttle(int throttleId)
    : m_throttleId(throttleId)
    , m_state(State::UNALLOCATED)
    , m_assignedKnob(KNOB_NONE)
    , m_locomotive(nullptr)
{
}

bool Throttle::assignKnob(int knobId)
{
    // Validate knob ID
    if (knobId != KNOB_1 && knobId != KNOB_2) {
        return false;
    }

    m_assignedKnob = knobId;
    
    // If we don't have a loco, enter selection mode
    if (!m_locomotive) {
        m_state = State::SELECTING_LOCO;
    }
    // If we already have a loco, stay in allocated state
    
    return true;
}

void Throttle::unassignKnob()
{
    m_assignedKnob = KNOB_NONE;
    
    // If we were just selecting, go back to unallocated
    if (m_state == State::SELECTING_LOCO) {
        m_state = State::UNALLOCATED;
    }
    // If we have a loco, we stay allocated even without a knob
}

bool Throttle::assignLocomotive(std::unique_ptr<Locomotive> loco)
{
    // Should only assign loco when in selecting state
    if (m_state != State::SELECTING_LOCO) {
        return false;
    }
    
    if (!loco) {
        return false;
    }
    
    m_locomotive = std::move(loco);
    m_state = State::ALLOCATED;
    
    return true;
}

std::unique_ptr<Locomotive> Throttle::releaseLocomotive()
{
    auto loco = std::move(m_locomotive);
    m_locomotive = nullptr;
    m_state = State::UNALLOCATED;
    m_assignedKnob = KNOB_NONE;
    
    return loco;
}

bool Throttle::isControlledByKnob(int knobId) const
{
    return m_assignedKnob == knobId;
}
