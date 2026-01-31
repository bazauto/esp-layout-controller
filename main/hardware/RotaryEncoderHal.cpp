#include "RotaryEncoderHal.h"
#include "esp_log.h"
#include "waveshare_rgb_lcd_port.h"

static const char* TAG = "RotaryEncoderHal";

namespace {
    constexpr int ENCODER_I2C_TIMEOUT_MS = 20;
    constexpr int ENCODER_POLL_MS = 100;
    constexpr int ENCODER_READ_RETRY_DELAY_MS = 5;
    constexpr uint8_t SEESAW_GPIO_BASE = 0x01;
    constexpr uint8_t SEESAW_GPIO_DIRCLR_BULK = 0x03;
    constexpr uint8_t SEESAW_GPIO_BULK = 0x04;
    constexpr uint8_t SEESAW_GPIO_BULK_SET = 0x05;
    constexpr uint8_t SEESAW_GPIO_PULLENSET = 0x0B;
    constexpr uint8_t SEESAW_ENCODER_BASE = 0x11;
    constexpr uint8_t SEESAW_ENCODER_DELTA = 0x40;
    constexpr uint8_t SEESAW_ROTARY_BUTTON_PIN = 24;
}

RotaryEncoderHal::RotaryEncoderHal(i2c_port_t port)
    : m_port(port)
    , m_rotationCallback(nullptr)
    , m_pressCallback(nullptr)
    , m_pollingTaskHandle(nullptr)
{
    m_status[0] = {ENCODER_1_ADDRESS, false};
    m_status[1] = {ENCODER_2_ADDRESS, false};
    m_lastPressed[0] = false;
    m_lastPressed[1] = false;
}

void RotaryEncoderHal::initialise()
{
    ESP_LOGI(TAG, "Scanning I2C bus for devices...");
    int foundCount = 0;
    for (uint8_t address = 0x03; address <= 0x77; ++address) {
        if (probeAddress(address)) {
            ESP_LOGI(TAG, "I2C device found at 0x%02X", address);
            ++foundCount;
        }
    }
    ESP_LOGI(TAG, "I2C scan complete (%d device(s) found)", foundCount);

    m_status[0].present = probeAddress(ENCODER_1_ADDRESS);
    m_status[1].present = probeAddress(ENCODER_2_ADDRESS);

    ESP_LOGI(TAG, "Encoder 1 (0x%02X): %s", ENCODER_1_ADDRESS, m_status[0].present ? "present" : "missing");
    ESP_LOGI(TAG, "Encoder 2 (0x%02X): %s", ENCODER_2_ADDRESS, m_status[1].present ? "present" : "missing");

    if (m_status[0].present) {
        configureButton(ENCODER_1_ADDRESS);
    }
    if (m_status[1].present) {
        configureButton(ENCODER_2_ADDRESS);
    }
}

void RotaryEncoderHal::startPollingTask()
{
    if (m_pollingTaskHandle != nullptr) {
        return;
    }

    xTaskCreate(pollingTask, "rotary_enc", 3072, this, 4, reinterpret_cast<TaskHandle_t*>(&m_pollingTaskHandle));
}

RotaryEncoderHal::EncoderStatus RotaryEncoderHal::getStatus(int index) const
{
    if (index < 0 || index > 1) {
        return {};
    }
    return m_status[index];
}

void RotaryEncoderHal::setRotationCallback(std::function<void(int, int)> callback)
{
    m_rotationCallback = std::move(callback);
}

void RotaryEncoderHal::setPressCallback(std::function<void(int, bool)> callback)
{
    m_pressCallback = std::move(callback);
}

void RotaryEncoderHal::pollingTask(void* arg)
{
    auto* hal = static_cast<RotaryEncoderHal*>(arg);
    if (!hal) {
        vTaskDelete(nullptr);
        return;
    }

    while (true) {
        hal->pollOnce();
        vTaskDelay(pdMS_TO_TICKS(ENCODER_POLL_MS));
    }
}

void RotaryEncoderHal::pollOnce()
{
    for (int i = 0; i < 2; ++i) {
        if (!m_status[i].present) {
            continue;
        }

        int32_t deltaRaw = 0;
        bool deltaOk = readEncoderDelta(m_status[i].address, deltaRaw);

        if (deltaOk) {
            if (deltaRaw != 0 && m_rotationCallback) {
                ESP_LOGD(TAG, "Encoder %d delta=%ld", i, static_cast<long>(deltaRaw));
                m_rotationCallback(i, static_cast<int>(deltaRaw));
            }
        } else {
            ESP_LOGW(TAG, "Encoder %d read failed (delta)", i);
        }

        vTaskDelay(pdMS_TO_TICKS(ENCODER_READ_RETRY_DELAY_MS));

        bool pressed = false;
        if (readButtonPressed(m_status[i].address, pressed)) {
            if (pressed != m_lastPressed[i] && m_pressCallback) {
                ESP_LOGD(TAG, "Encoder %d press: %s", i, pressed ? "down" : "up");
                m_pressCallback(i, pressed);
            }
            m_lastPressed[i] = pressed;
        } else {
            ESP_LOGW(TAG, "Encoder %d read failed (button)", i);
        }
    }
}

bool RotaryEncoderHal::probeAddress(uint8_t address)
{
    uint8_t dummy = 0;
    esp_err_t err = i2c_master_read_from_device(m_port, address, &dummy, 1, pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    return err == ESP_OK;
}

bool RotaryEncoderHal::readBytes(uint8_t address, uint8_t base, uint8_t reg, uint8_t* data, size_t length)
{
    uint8_t cmd[2] = {base, reg};
    esp_err_t err = i2c_master_write_read_device(m_port, address, cmd, sizeof(cmd), data, length,
                                                 pdMS_TO_TICKS(ENCODER_I2C_TIMEOUT_MS));
    return err == ESP_OK;
}

bool RotaryEncoderHal::writeBytes(uint8_t address, uint8_t base, uint8_t reg, const uint8_t* data, size_t length)
{
    uint8_t buffer[6] = {base, reg, 0, 0, 0, 0};
    size_t payload = length > 4 ? 4 : length;
    for (size_t i = 0; i < payload; ++i) {
        buffer[2 + i] = data[i];
    }
    esp_err_t err = i2c_master_write_to_device(m_port, address, buffer, 2 + payload,
                                               pdMS_TO_TICKS(ENCODER_I2C_TIMEOUT_MS));
    return err == ESP_OK;
}

bool RotaryEncoderHal::readEncoderDelta(uint8_t address, int32_t& outDelta)
{
    uint8_t data[4] = {0};
    // Read twice to mirror observed hardware behaviour (second read yields current delta)
    if (!readBytes(address, SEESAW_ENCODER_BASE, SEESAW_ENCODER_DELTA, data, sizeof(data))) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(ENCODER_READ_RETRY_DELAY_MS));
    if (!readBytes(address, SEESAW_ENCODER_BASE, SEESAW_ENCODER_DELTA, data, sizeof(data))) {
        return false;
    }

    outDelta = (static_cast<int32_t>(data[0]) << 24) |
               (static_cast<int32_t>(data[1]) << 16) |
               (static_cast<int32_t>(data[2]) << 8) |
               (static_cast<int32_t>(data[3]));
    return true;
}

bool RotaryEncoderHal::readButtonPressed(uint8_t address, bool& outPressed)
{
    uint8_t data[4] = {0};
    if (!readBytes(address, SEESAW_GPIO_BASE, SEESAW_GPIO_BULK, data, sizeof(data))) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(ENCODER_READ_RETRY_DELAY_MS));
    if (!readBytes(address, SEESAW_GPIO_BASE, SEESAW_GPIO_BULK, data, sizeof(data))) {
        return false;
    }

    uint32_t gpio = (static_cast<uint32_t>(data[0]) << 24) |
                    (static_cast<uint32_t>(data[1]) << 16) |
                    (static_cast<uint32_t>(data[2]) << 8) |
                    (static_cast<uint32_t>(data[3]));

    bool pinHigh = (gpio & (1u << SEESAW_ROTARY_BUTTON_PIN)) != 0;
    outPressed = !pinHigh; // active-low

    return true;
}

void RotaryEncoderHal::configureButton(uint8_t address)
{
    uint32_t mask = (1u << SEESAW_ROTARY_BUTTON_PIN);
    uint8_t data[4] = {
        static_cast<uint8_t>((mask >> 24)),
        static_cast<uint8_t>((mask >> 16)),
        static_cast<uint8_t>((mask >> 8)),
        static_cast<uint8_t>(mask)
    };

    // Enable pin interrupt, set pin as input (clear direction) and enable pull-up
    writeBytes(address, SEESAW_GPIO_BASE, SEESAW_GPIO_DIRCLR_BULK, data, sizeof(data));
    writeBytes(address, SEESAW_GPIO_BASE, SEESAW_GPIO_PULLENSET, data, sizeof(data));
    writeBytes(address, SEESAW_GPIO_BASE, SEESAW_GPIO_BULK_SET, data, sizeof(data));
}
