#include "FunctionPanel.h"

FunctionPanel::FunctionPanel()
    : m_panel(nullptr)
    , m_titleLabel(nullptr)
    , m_closeButton(nullptr)
    , m_buttonsContainer(nullptr)
    , m_functionCallback(nullptr)
    , m_functionCallbackUserData(nullptr)
    , m_throttleId(-1)
{
}

lv_obj_t* FunctionPanel::create(lv_obj_t* parent, lv_event_cb_t closeCallback, void* userData)
{
    m_panel = lv_obj_create(parent);
    lv_obj_set_size(m_panel, LV_PCT(95), LV_PCT(100));
    lv_obj_set_flex_flow(m_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(m_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(m_panel, 8, 0);
    lv_obj_set_style_pad_row(m_panel, 6, 0);

    lv_obj_t* header = lv_obj_create(m_panel);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    m_titleLabel = lv_label_create(header);
    lv_label_set_text(m_titleLabel, "Functions");

    m_closeButton = lv_btn_create(header);
    lv_obj_set_size(m_closeButton, 40, 30);
    lv_obj_add_event_cb(m_closeButton, closeCallback, LV_EVENT_CLICKED, userData);

    lv_obj_t* closeLabel = lv_label_create(m_closeButton);
    lv_label_set_text(closeLabel, LV_SYMBOL_CLOSE);
    lv_obj_center(closeLabel);

    m_buttonsContainer = lv_obj_create(m_panel);
    lv_obj_set_size(m_buttonsContainer, LV_PCT(100), LV_PCT(90));
    lv_obj_set_flex_flow(m_buttonsContainer, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(m_buttonsContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(m_buttonsContainer, 6, 0);
    lv_obj_set_style_pad_row(m_buttonsContainer, 6, 0);
    lv_obj_set_style_pad_column(m_buttonsContainer, 6, 0);
    lv_obj_set_scroll_dir(m_buttonsContainer, LV_DIR_VER);

    lv_obj_add_flag(m_panel, LV_OBJ_FLAG_HIDDEN);
    return m_panel;
}

void FunctionPanel::setFunctionCallback(lv_event_cb_t callback, void* userData)
{
    m_functionCallback = callback;
    m_functionCallbackUserData = userData;
}

void FunctionPanel::show(int throttleId, const std::string& locoName, const std::vector<Function>& functions)
{
    m_throttleId = throttleId;
    if (m_titleLabel) {
        if (!locoName.empty()) {
            std::string title = "Functions - " + locoName;
            lv_label_set_text(m_titleLabel, title.c_str());
        } else {
            lv_label_set_text(m_titleLabel, "Functions");
        }
    }

    rebuildButtons(functions);
    lv_obj_clear_flag(m_panel, LV_OBJ_FLAG_HIDDEN);
}

void FunctionPanel::hide()
{
    if (m_panel) {
        lv_obj_add_flag(m_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

bool FunctionPanel::isVisible() const
{
    return m_panel && !lv_obj_has_flag(m_panel, LV_OBJ_FLAG_HIDDEN);
}

void FunctionPanel::updateFunctions(const std::vector<Function>& functions)
{
    if (labelsChanged(functions)) {
        rebuildButtons(functions);
    } else {
        updateButtonStates(functions);
    }
}

void FunctionPanel::rebuildButtons(const std::vector<Function>& functions)
{
    if (!m_buttonsContainer) {
        return;
    }

    lv_obj_clean(m_buttonsContainer);
    m_functionButtons.clear();
    m_lastLabels.clear();

    for (const auto& func : functions) {
        lv_obj_t* btn = lv_btn_create(m_buttonsContainer);
        lv_obj_set_size(btn, 100, 50);
        lv_obj_set_user_data(btn, reinterpret_cast<void*>(static_cast<intptr_t>(func.number)));

        if (m_functionCallback) {
            lv_obj_add_event_cb(btn, m_functionCallback, LV_EVENT_PRESSED, m_functionCallbackUserData);
            lv_obj_add_event_cb(btn, m_functionCallback, LV_EVENT_RELEASED, m_functionCallbackUserData);
        }

        lv_obj_t* label = lv_label_create(btn);
        std::string text = "F" + std::to_string(func.number);
        if (!func.label.empty()) {
            text += "\n" + func.label;
        }
        lv_label_set_text(label, text.c_str());
        lv_obj_center(label);

        m_functionButtons.push_back(btn);
        m_lastLabels.push_back(func.label);
    }

    updateButtonStates(functions);
}

void FunctionPanel::updateButtonStates(const std::vector<Function>& functions)
{
    if (m_functionButtons.size() != functions.size()) {
        return;
    }

    for (size_t i = 0; i < functions.size(); ++i) {
        lv_obj_t* btn = m_functionButtons[i];
        const auto& func = functions[i];
        lv_obj_set_style_bg_color(btn,
                                  func.state ? lv_palette_main(LV_PALETTE_GREEN)
                                             : lv_palette_main(LV_PALETTE_GREY),
                                  0);
    }
}

bool FunctionPanel::labelsChanged(const std::vector<Function>& functions) const
{
    if (functions.size() != m_functionButtons.size()) {
        return true;
    }

    if (functions.size() != m_lastLabels.size()) {
        return true;
    }

    for (size_t i = 0; i < functions.size(); ++i) {
        if (functions[i].label != m_lastLabels[i]) {
            return true;
        }
    }

    return false;
}
