#pragma once

#include "lvgl.h"
#include "../model/Throttle.h"
#include <vector>
#include <string>

/**
 * @brief Function panel overlay for locomotive functions (F0-F28).
 */
class FunctionPanel {
public:
    FunctionPanel();
    ~FunctionPanel() = default;

    FunctionPanel(const FunctionPanel&) = delete;
    FunctionPanel& operator=(const FunctionPanel&) = delete;

    /**
     * @brief Create the panel under the given parent.
     */
    lv_obj_t* create(lv_obj_t* parent, lv_event_cb_t closeCallback, void* userData);

    /**
     * @brief Show the panel with the provided function data.
     */
    void show(int throttleId, const std::string& locoName, const std::vector<Function>& functions);

    /**
     * @brief Hide the panel.
     */
    void hide();

    /**
     * @brief Update button states/labels without recreating the panel.
     */
    void updateFunctions(const std::vector<Function>& functions);

    /**
     * @brief Set callback for function button toggles.
     */
    void setFunctionCallback(lv_event_cb_t callback, void* userData);

    bool isVisible() const;
    bool isScrolling() const;
    int getThrottleId() const { return m_throttleId; }

private:
    void rebuildButtons(const std::vector<Function>& functions);
    void updateButtonStates(const std::vector<Function>& functions);
    bool labelsChanged(const std::vector<Function>& functions) const;

    lv_obj_t* m_panel;
    lv_obj_t* m_titleLabel;
    lv_obj_t* m_closeButton;
    lv_obj_t* m_buttonsContainer;

    lv_event_cb_t m_functionCallback;
    void* m_functionCallbackUserData;

    int m_throttleId;
    std::vector<lv_obj_t*> m_functionButtons;
    std::vector<std::string> m_lastLabels;
};
