#include "Roster.h"

Roster::Roster()
    : m_locos()
{
    m_locos.reserve(MAX_LOCOS);
}

bool Roster::addLocomotive(const std::string& name, uint16_t address, Locomotive::AddressType addressType)
{
    if (m_locos.size() >= MAX_LOCOS) {
        return false;
    }

    m_locos.emplace_back(name, address, addressType);
    return true;
}

const Locomotive* Roster::getLocomotive(size_t index) const
{
    if (index >= m_locos.size()) {
        return nullptr;
    }
    return &m_locos[index];
}

std::unique_ptr<Locomotive> Roster::createLocomotiveCopy(size_t index) const
{
    const Locomotive* loco = getLocomotive(index);
    if (!loco) {
        return nullptr;
    }

    // Create a new locomotive with same properties
    auto copy = std::make_unique<Locomotive>(
        loco->getName(),
        loco->getAddress(),
        loco->getAddressType()
    );

    // Copy current state
    copy->setSpeed(loco->getSpeed());
    copy->setDirection(loco->getDirection());
    copy->setSpeedStepMode(loco->getSpeedStepMode());

    // Copy function states and labels
    for (uint8_t i = 0; i < 29; ++i) {
        copy->setFunctionState(i, loco->getFunctionState(i));
        copy->setFunctionLabel(i, loco->getFunctionLabel(i));
    }

    return copy;
}

int Roster::findByName(const std::string& name) const
{
    for (size_t i = 0; i < m_locos.size(); ++i) {
        if (m_locos[i].getName() == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int Roster::findByAddress(uint16_t address, Locomotive::AddressType addressType) const
{
    for (size_t i = 0; i < m_locos.size(); ++i) {
        if (m_locos[i].getAddress() == address && m_locos[i].getAddressType() == addressType) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void Roster::clear()
{
    m_locos.clear();
}

size_t Roster::getNextIndex(size_t currentIndex) const
{
    if (m_locos.empty()) {
        return 0;
    }

    return (currentIndex + 1) % m_locos.size();
}

size_t Roster::getPreviousIndex(size_t currentIndex) const
{
    if (m_locos.empty()) {
        return 0;
    }

    if (currentIndex == 0) {
        return m_locos.size() - 1;
    }

    return currentIndex - 1;
}
