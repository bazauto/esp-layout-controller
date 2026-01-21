#include "Throttle.h"

Throttle::Throttle()
    : m_throttleId(0)
    , m_state(State::UNALLOCATED)
    , m_assignedKnob(KNOB_NONE)
    , m_locomotive(nullptr)
    , m_currentSpeed(0)
    , m_direction(true)
{
}

Throttle::Throttle(int throttleId)
    : m_throttleId(throttleId)
    , m_state(State::UNALLOCATED)
    , m_assignedKnob(KNOB_NONE)
    , m_locomotive(nullptr)
    , m_currentSpeed(0)
    , m_direction(true)
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
        m_state = State::SELECTING;
    } else {
        // Already have a loco, now have knob control
        m_state = State::ALLOCATED_WITH_KNOB;
    }
    
    return true;
}

void Throttle::unassignKnob()
{
    m_assignedKnob = KNOB_NONE;
    
    // If we were just selecting, go back to unallocated
    if (m_state == State::SELECTING) {
        m_state = State::UNALLOCATED;
    } else if (m_state == State::ALLOCATED_WITH_KNOB) {
        // Have loco but no knob
        m_state = State::ALLOCATED_NO_KNOB;
    }
}

bool Throttle::assignLocomotive(std::unique_ptr<Locomotive> loco)
{
    // Should only assign loco when in selecting state
    if (m_state != State::SELECTING) {
        return false;
    }
    
    if (!loco) {
        return false;
    }
    
    m_locomotive = std::move(loco);
    m_state = State::ALLOCATED_WITH_KNOB;  // Has knob since we were in SELECTING
    m_currentSpeed = 0;
    m_direction = true;
    m_functions.clear();
    
    return true;
}

std::unique_ptr<Locomotive> Throttle::releaseLocomotive()
{
    auto loco = std::move(m_locomotive);
    m_locomotive = nullptr;
    m_state = State::UNALLOCATED;
    m_assignedKnob = KNOB_NONE;
    m_currentSpeed = 0;
    m_direction = true;
    m_functions.clear();
    
    return loco;
}

bool Throttle::isControlledByKnob(int knobId) const
{
    return m_assignedKnob == knobId;
}

void Throttle::setSpeed(int speed)
{
    if (speed < 0) speed = 0;
    if (speed > 126) speed = 126;
    m_currentSpeed = speed;
}

void Throttle::setDirection(bool forward)
{
    m_direction = forward;
}

void Throttle::setFunctionState(int functionNumber, bool state)
{
    for (auto& func : m_functions) {
        if (func.number == functionNumber) {
            func.state = state;
            return;
        }
    }

    m_functions.emplace_back(functionNumber, "", state);
}

void Throttle::addFunction(const Function& function)
{
    for (auto& existing : m_functions) {
        if (existing.number == function.number) {
            existing.label = function.label;
            existing.state = function.state;
            return;
        }
    }

    m_functions.push_back(function);
}

void Throttle::clearFunctions()
{
    m_functions.clear();
}
