#pragma once

#include <cstdint>
#include <string>

/**
 * @brief Represents a single locomotive with its DCC address, name, and state.
 * 
 * This is a pure data model class with no UI or I/O dependencies.
 * Stores locomotive information from the WiThrottle roster and current state.
 */
class Locomotive {
public:
    /**
     * @brief Address length type (Short or Long DCC address)
     */
    enum class AddressType {
        SHORT,  // Short DCC address (1-127)
        LONG    // Long DCC address (128-10239)
    };

    /**
     * @brief Direction of travel
     */
    enum class Direction {
        REVERSE = 0,
        FORWARD = 1
    };

    /**
     * @brief Speed step mode
     */
    enum class SpeedStepMode {
        STEPS_14 = 8,
        STEPS_27 = 4,
        STEPS_28 = 2,
        STEPS_128 = 1
    };

    // Constructors
    Locomotive();
    Locomotive(const std::string& name, uint16_t address, AddressType addressType);
    
    // Prevent copying (can add if needed later)
    Locomotive(const Locomotive&) = delete;
    Locomotive& operator=(const Locomotive&) = delete;
    
    // Allow moving
    Locomotive(Locomotive&&) = default;
    Locomotive& operator=(Locomotive&&) = default;
    
    ~Locomotive() = default;

    // Getters
    const std::string& getName() const { return m_name; }
    uint16_t getAddress() const { return m_address; }
    AddressType getAddressType() const { return m_addressType; }
    uint8_t getSpeed() const { return m_speed; }
    Direction getDirection() const { return m_direction; }
    SpeedStepMode getSpeedStepMode() const { return m_speedStepMode; }
    
    /**
     * @brief Get function state (F0-F28)
     * @param functionNum Function number (0-28)
     * @return true if function is active, false otherwise
     */
    bool getFunctionState(uint8_t functionNum) const;
    
    /**
     * @brief Get function label (F0-F28)
     * @param functionNum Function number (0-28)
     * @return Function label string, empty if not set
     */
    const std::string& getFunctionLabel(uint8_t functionNum) const;

    /**
     * @brief Get formatted address string for WiThrottle protocol
     * @return "S123" for short address or "L1234" for long address
     */
    std::string getAddressString() const;

    // Setters
    void setSpeed(uint8_t speed);
    void setDirection(Direction direction);
    void setSpeedStepMode(SpeedStepMode mode);
    
    /**
     * @brief Set function state (F0-F28)
     * @param functionNum Function number (0-28)
     * @param state true to activate, false to deactivate
     */
    void setFunctionState(uint8_t functionNum, bool state);
    
    /**
     * @brief Set function label (F0-F28)
     * @param functionNum Function number (0-28)
     * @param label Label text (e.g., "Headlight", "Bell")
     */
    void setFunctionLabel(uint8_t functionNum, const std::string& label);

private:
    static constexpr uint8_t MAX_FUNCTIONS = 29; // F0-F28
    static const std::string EMPTY_STRING;

    std::string m_name;
    uint16_t m_address;
    AddressType m_addressType;
    uint8_t m_speed;                        // 0-126 for 128 speed steps
    Direction m_direction;
    SpeedStepMode m_speedStepMode;
    
    bool m_functionStates[MAX_FUNCTIONS];   // F0-F28 states
    std::string m_functionLabels[MAX_FUNCTIONS]; // F0-F28 labels
};
