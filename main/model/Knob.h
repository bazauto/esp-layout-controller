#pragma once

/**
 * @file Knob.h
 * @brief Rotary encoder knob model for throttle control
 * 
 * Represents a physical rotary encoder that can be assigned to throttles.
 * Handles state machine for: IDLE → SELECTING → CONTROLLING
 */

class Knob
{
public:
    /**
     * @brief Knob state in the assignment workflow
     */
    enum class State {
        IDLE,          // Not assigned to any throttle
        SELECTING,     // Assigned to throttle, scrolling roster
        CONTROLLING    // Assigned to throttle with acquired loco
    };
    
    /**
     * @brief Constructor
     * @param id Knob identifier (0=left, 1=right)
     */
    explicit Knob(int id);
    
    /**
     * @brief Get knob ID
     */
    int getId() const { return m_id; }
    
    /**
     * @brief Get current state
     */
    State getState() const { return m_state; }
    
    /**
     * @brief Get assigned throttle ID (-1 if not assigned)
     */
    int getAssignedThrottleId() const { return m_assignedThrottleId; }
    
    /**
     * @brief Get current roster index (when SELECTING)
     */
    int getRosterIndex() const { return m_rosterIndex; }
    
    /**
     * @brief Assign knob to throttle for selection
     * @param throttleId Throttle ID (0-3)
     */
    void assignToThrottle(int throttleId);

    /**
     * @brief Reassign knob to a different throttle without forcing IDLE
     * @param throttleId Throttle ID (0-3)
     * @param newState Target knob state after reassignment
     * @param resetRosterIndex Reset roster index when entering SELECTING
     */
    void reassignToThrottle(int throttleId, State newState, bool resetRosterIndex = true);
    
    /**
     * @brief Transition from SELECTING to CONTROLLING
     * Called when loco is acquired
     */
    void startControlling();
    
    /**
     * @brief Release knob from throttle
     * Returns to IDLE state
     */
    void release();
    
    /**
     * @brief Handle rotation event (when SELECTING, scrolls roster)
     * @param delta Rotation amount (positive=CW, negative=CCW)
     * @param rosterSize Total roster size for wrapping
     */
    void handleRotation(int delta, int rosterSize);
    
    /**
     * @brief Reset roster index to 0
     */
    void resetRosterIndex();

private:
    int m_id;                    // 0 or 1 (left/right)
    State m_state;
    int m_assignedThrottleId;    // -1 if not assigned, 0-3 otherwise
    int m_rosterIndex;           // Current position when SELECTING
};
