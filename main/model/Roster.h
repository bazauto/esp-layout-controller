#pragma once

#include <vector>
#include <memory>
#include <string>
#include "Locomotive.h"

/**
 * @brief Manages the roster of available locomotives.
 * 
 * Stores up to 50 locomotives (matching command station capacity).
 * Provides navigation and search capabilities for loco selection.
 */
class Roster {
public:
    static constexpr size_t MAX_LOCOS = 50;

    Roster();
    ~Roster() = default;

    // Delete copy
    Roster(const Roster&) = delete;
    Roster& operator=(const Roster&) = delete;

    /**
     * @brief Add a locomotive to the roster
     * @param loco Locomotive data (name, address, type)
     * @return true if added successfully, false if roster is full
     */
    bool addLocomotive(const std::string& name, uint16_t address, Locomotive::AddressType addressType);

    /**
     * @brief Get the number of locomotives in the roster
     */
    size_t getCount() const { return m_locos.size(); }

    /**
     * @brief Check if roster is empty
     */
    bool isEmpty() const { return m_locos.empty(); }

    /**
     * @brief Get locomotive at specific index
     * @param index Index in roster (0-based)
     * @return Pointer to locomotive, or nullptr if index invalid
     */
    const Locomotive* getLocomotive(size_t index) const;

    /**
     * @brief Create a copy of a locomotive for assignment to a throttle
     * @param index Index in roster (0-based)
     * @return Unique pointer to loco copy, or nullptr if index invalid
     */
    std::unique_ptr<Locomotive> createLocomotiveCopy(size_t index) const;

    /**
     * @brief Find locomotive by name
     * @param name Locomotive name to search for
     * @return Index of locomotive, or -1 if not found
     */
    int findByName(const std::string& name) const;

    /**
     * @brief Find locomotive by address
     * @param address DCC address
     * @param addressType Short or Long
     * @return Index of locomotive, or -1 if not found
     */
    int findByAddress(uint16_t address, Locomotive::AddressType addressType) const;

    /**
     * @brief Clear all locomotives from roster
     */
    void clear();

    /**
     * @brief Get the next index for roster navigation (wraps around)
     * @param currentIndex Current index
     * @return Next index, or 0 if roster is empty
     */
    size_t getNextIndex(size_t currentIndex) const;

    /**
     * @brief Get the previous index for roster navigation (wraps around)
     * @param currentIndex Current index
     * @return Previous index, or last index if roster is empty
     */
    size_t getPreviousIndex(size_t currentIndex) const;

private:
    std::vector<Locomotive> m_locos;
};
