#pragma once

#include <memory>
#include <cstdint>
#include "Locomotive.h"

/**
 * @brief Represents a single throttle instance with its state and assigned locomotive.
 * 
 * Each throttle can be in one of several states:
 * - UNALLOCATED: No loco assigned, no knob assigned
 * - SELECTING_LOCO: Knob assigned, user is scrolling through roster
 * - ALLOCATED: Loco assigned, knob controls speed
 * 
 * The throttle tracks which physical knob (0 or 1) is currently controlling it.
 */
class Throttle {
public:
    /**
     * @brief Throttle state machine states
     */
    enum class State {
        UNALLOCATED,     // No loco, no knob
        SELECTING_LOCO,  // Knob assigned, selecting from roster
        ALLOCATED        // Loco assigned and active
    };

    /**
     * @brief Physical knob identifiers
     */
    static constexpr int KNOB_NONE = -1;
    static constexpr int KNOB_1 = 0;
    static constexpr int KNOB_2 = 1;

    // Constructors
    Throttle();
    explicit Throttle(int throttleId);
    
    // Delete copy
    Throttle(const Throttle&) = delete;
    Throttle& operator=(const Throttle&) = delete;
    
    // Allow move
    Throttle(Throttle&&) = default;
    Throttle& operator=(Throttle&&) = default;
    
    ~Throttle() = default;

    // Getters
    int getThrottleId() const { return m_throttleId; }
    State getState() const { return m_state; }
    int getAssignedKnob() const { return m_assignedKnob; }
    Locomotive* getLocomotive() const { return m_locomotive.get(); }
    bool hasLocomotive() const { return m_locomotive != nullptr; }

    // State transitions
    /**
     * @brief Assign a knob to this throttle for loco selection
     * @param knobId KNOB_1 or KNOB_2
     * @return true if successful, false if invalid knob ID
     */
    bool assignKnob(int knobId);
    
    /**
     * @brief Remove knob assignment and return to unallocated state
     */
    void unassignKnob();
    
    /**
     * @brief Assign a locomotive to this throttle
     * @param loco Unique pointer to locomotive (ownership transferred)
     * @return true if successful (must be in SELECTING_LOCO state)
     */
    bool assignLocomotive(std::unique_ptr<Locomotive> loco);
    
    /**
     * @brief Release the locomotive and return to unallocated state
     * @return Unique pointer to released locomotive (nullptr if none)
     */
    std::unique_ptr<Locomotive> releaseLocomotive();

    // Convenience methods
    /**
     * @brief Check if this throttle is controlled by the specified knob
     * @param knobId KNOB_1 or KNOB_2
     * @return true if this knob controls this throttle
     */
    bool isControlledByKnob(int knobId) const;

private:
    int m_throttleId;                        // 0-3
    State m_state;
    int m_assignedKnob;                      // KNOB_NONE, KNOB_1, or KNOB_2
    std::unique_ptr<Locomotive> m_locomotive;
};
