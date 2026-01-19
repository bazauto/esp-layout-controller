#pragma once

#include <memory>

class WiFiManager;

/**
 * @brief Owns WiFiManager lifecycle and auto-connect logic.
 */
class WiFiController {
public:
    WiFiController();
    ~WiFiController();

    WiFiController(const WiFiController&) = delete;
    WiFiController& operator=(const WiFiController&) = delete;

    void initialize();
    void autoConnect();

    bool isConnected() const;
    WiFiManager* getManager() const;

private:
    std::unique_ptr<WiFiManager> m_wifiManager;
};
