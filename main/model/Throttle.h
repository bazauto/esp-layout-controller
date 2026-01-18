#pragma once

#include <memory>
#include <cstdint>
#include <vector>
#include <string>
#include "Locomotive.h"

/**
 * @brief Function state and metadata
 */
struct Function {
    int number;              // 0-28
    std::string label;       // "Headlight", "Bell", etc. (empty if unlabeled)
    bool state;              // Current on/off state
    
    Function() : number(0), state(false) {}
    Function(int num, const std::string& lbl, bool st) 
        : number(num), label(lbl), state(st) {}
};

/**
 * @brief Represents a single throttle instance with its state and assigned locomotive.
 * 
 * Each throttle can be in one of several states:
 * - UNALLOCATED: No loco, no knob assigned
 * - SELECTING: Knob assigned, user is scrolling through roster
 * - ALLOCATED_WITH_KNOB: Loco assigned, knob controlling it
 * - ALLOCATED_NO_KNOB: Loco assigned, but knob moved elsewhere
 * 
 * The throttle tracks which physical knob (0 or 1) is currently controlling it.
 */
class Throttle {
public:
    /**
     * @brief Throttle state machine states
     */
    enum class State {
        UNALLOCATED,          // No loco, no knob
        SELECTING,            // Knob assigned, selecting from roster
        ALLOCATED_WITH_KNOB,  // Loco assigned, knob controls it
        ALLOCATED_NO_KNOB     // Loco assigned, but no knob
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
    int getCurrentSpeed() const { return m_currentSpeed; }
    bool getDirection() const { return m_direction; }
    const std::vector<Function>& getFunctions() const { return m_functions; }

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
    
    /**
     * @brief Update speed from throttle change notification
     * @param speed New speed value (0-126)
     */
    void setSpeed(int speed);
    
    /**
     * @brief Update direction from throttle change notification
     * @param forward true for forward, false for reverse
     */
    void setDirection(bool forward);
    
    /**
     * @brief Update function state from throttle change notification
     * @param functionNumber Function number (0-28)
     * @param state New state (true=on, false=off)
     */
    void setFunctionState(int functionNumber, bool state);
    
    /**
     * @brief Add function to the list (from acquire response)
     * @param function Function to add
     */
    void addFunction(const Function& function);
    
    /**
     * @brief Clear all functions
     */
    void clearFunctions();

private:
    int m_throttleId;                        // 0-3
    State m_state;
    int m_assignedKnob;                      // KNOB_NONE, KNOB_1, or KNOB_2
    std::unique_ptr<Locomotive> m_locomotive;
    
    // Loco control state (when allocated)
    int m_currentSpeed;                      // 0-126
    bool m_direction;                        // true=forward, false=reverse
    std::vector<Function> m_functions;       // Available functions for this loco
};
