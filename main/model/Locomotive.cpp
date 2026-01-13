#include "Locomotive.h"
#include <sstream>
#include <iomanip>

const std::string Locomotive::EMPTY_STRING = "";

Locomotive::Locomotive()
    : m_name("")
    , m_address(0)
    , m_addressType(AddressType::SHORT)
    , m_speed(0)
    , m_direction(Direction::FORWARD)
    , m_speedStepMode(SpeedStepMode::STEPS_128)
    , m_functionStates{}
    , m_functionLabels{}
{
}

Locomotive::Locomotive(const std::string& name, uint16_t address, AddressType addressType)
    : m_name(name)
    , m_address(address)
    , m_addressType(addressType)
    , m_speed(0)
    , m_direction(Direction::FORWARD)
    , m_speedStepMode(SpeedStepMode::STEPS_128)
    , m_functionStates{}
    , m_functionLabels{}
{
}

bool Locomotive::getFunctionState(uint8_t functionNum) const
{
    if (functionNum >= MAX_FUNCTIONS) {
        return false;
    }
    return m_functionStates[functionNum];
}

const std::string& Locomotive::getFunctionLabel(uint8_t functionNum) const
{
    if (functionNum >= MAX_FUNCTIONS) {
        return EMPTY_STRING;
    }
    return m_functionLabels[functionNum];
}

std::string Locomotive::getAddressString() const
{
    std::ostringstream oss;
    if (m_addressType == AddressType::SHORT) {
        oss << "S" << m_address;
    } else {
        oss << "L" << m_address;
    }
    return oss.str();
}

void Locomotive::setSpeed(uint8_t speed)
{
    // Clamp to valid range for 128 speed steps (0-126)
    if (speed > 126) {
        speed = 126;
    }
    m_speed = speed;
}

void Locomotive::setDirection(Direction direction)
{
    m_direction = direction;
}

void Locomotive::setSpeedStepMode(SpeedStepMode mode)
{
    m_speedStepMode = mode;
}

void Locomotive::setFunctionState(uint8_t functionNum, bool state)
{
    if (functionNum < MAX_FUNCTIONS) {
        m_functionStates[functionNum] = state;
    }
}

void Locomotive::setFunctionLabel(uint8_t functionNum, const std::string& label)
{
    if (functionNum < MAX_FUNCTIONS) {
        m_functionLabels[functionNum] = label;
    }
}
