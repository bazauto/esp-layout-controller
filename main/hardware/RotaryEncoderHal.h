#pragma once

#include <cstdint>
#include <functional>
#include "driver/i2c.h"

/**
 * @brief HAL for I2C rotary encoders (fixed addresses).
 */
class RotaryEncoderHal {
public:
    struct EncoderStatus {
        uint8_t address = 0;
        bool present = false;
    };

    // Physical mounting: top knob = L (knob 0), bottom knob = R (knob 1)
    static constexpr uint8_t ENCODER_1_ADDRESS = 0x77;
    static constexpr uint8_t ENCODER_2_ADDRESS = 0x76;

    RotaryEncoderHal(i2c_port_t port = I2C_NUM_0);
    ~RotaryEncoderHal() = default;

    RotaryEncoderHal(const RotaryEncoderHal&) = delete;
    RotaryEncoderHal& operator=(const RotaryEncoderHal&) = delete;

    void initialise();
    void startPollingTask();

    EncoderStatus getStatus(int index) const;

    void setRotationCallback(std::function<void(int, int)> callback);
    void setPressCallback(std::function<void(int, bool)> callback);

private:
    static void pollingTask(void* arg);
    void pollOnce();
    bool probeAddress(uint8_t address);
    bool readBytes(uint8_t address, uint8_t base, uint8_t reg, uint8_t* data, size_t length);
    bool writeBytes(uint8_t address, uint8_t base, uint8_t reg, const uint8_t* data, size_t length);
    bool readEncoderDelta(uint8_t address, int32_t& outDelta);
    bool readButtonPressed(uint8_t address, bool& outPressed);
    void configureButton(uint8_t address);

    i2c_port_t m_port;
    EncoderStatus m_status[2];
    std::function<void(int, int)> m_rotationCallback;
    std::function<void(int, bool)> m_pressCallback;
    void* m_pollingTaskHandle;
    bool m_lastPressed[2];
};
