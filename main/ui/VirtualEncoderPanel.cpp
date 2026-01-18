#include "VirtualEncoderPanel.h"
#include "esp_log.h"

static const char* TAG = "VirtualEncoderPanel";

VirtualEncoderPanel::VirtualEncoderPanel()
    : m_panel(nullptr)
    , m_rotateCWButton(nullptr)
    , m_rotateCCWButton(nullptr)
    , m_pressButton(nullptr)
    , m_statusLabel(nullptr)
    , m_activeKnob(0)
    , m_rotationCallback(nullptr)
    , m_pressCallback(nullptr)
    , m_userData(nullptr)
{
    m_knobSelectButtons[0] = nullptr;
    m_knobSelectButtons[1] = nullptr;
}

lv_obj_t* VirtualEncoderPanel::create(lv_obj_t* parent,
                                      void (*rotationCallback)(void*, int, int),
                                      void (*pressCallback)(void*, int),
                                      void* userData)
{
    m_rotationCallback = rotationCallback;
    m_pressCallback = pressCallback;
    m_userData = userData;
    
    // Create compact main panel container - single row at bottom
    m_panel = lv_obj_create(parent);
    lv_obj_set_size(m_panel, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(m_panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(m_panel, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(m_panel, 5, 0);
    lv_obj_set_style_pad_column(m_panel, 5, 0);
    lv_obj_align(m_panel, LV_ALIGN_BOTTOM_MID, 0, 0);
    
    // Knob selection buttons (compact)
    for (int i = 0; i < 2; i++) {
        m_knobSelectButtons[i] = lv_btn_create(m_panel);
        lv_obj_set_size(m_knobSelectButtons[i], 50, 40);
        lv_obj_add_event_cb(m_knobSelectButtons[i], onKnobSelectClicked, LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(m_knobSelectButtons[i], (void*)(intptr_t)i);
        
        lv_obj_t* label = lv_label_create(m_knobSelectButtons[i]);
        lv_label_set_text(label, i == 0 ? "L" : "R");
        lv_obj_center(label);
    }
    
    updateKnobButtons();
    
    // CCW button
    m_rotateCCWButton = lv_btn_create(m_panel);
    lv_obj_set_size(m_rotateCCWButton, 60, 40);
    lv_obj_add_event_cb(m_rotateCCWButton, onRotateCCWClicked, LV_EVENT_CLICKED, this);
    lv_obj_set_style_bg_color(m_rotateCCWButton, lv_palette_main(LV_PALETTE_BLUE), 0);
    
    lv_obj_t* ccwLabel = lv_label_create(m_rotateCCWButton);
    lv_label_set_text(ccwLabel, LV_SYMBOL_LEFT);
    lv_obj_center(ccwLabel);
    
    // Press button
    m_pressButton = lv_btn_create(m_panel);
    lv_obj_set_size(m_pressButton, 60, 40);
    lv_obj_add_event_cb(m_pressButton, onPressClicked, LV_EVENT_CLICKED, this);
    lv_obj_set_style_bg_color(m_pressButton, lv_palette_main(LV_PALETTE_GREEN), 0);
    
    lv_obj_t* pressLabel = lv_label_create(m_pressButton);
    lv_label_set_text(pressLabel, LV_SYMBOL_OK);
    lv_obj_center(pressLabel);
    
    // CW button
    m_rotateCWButton = lv_btn_create(m_panel);
    lv_obj_set_size(m_rotateCWButton, 60, 40);
    lv_obj_add_event_cb(m_rotateCWButton, onRotateCWClicked, LV_EVENT_CLICKED, this);
    lv_obj_set_style_bg_color(m_rotateCWButton, lv_palette_main(LV_PALETTE_BLUE), 0);
    
    lv_obj_t* cwLabel = lv_label_create(m_rotateCWButton);
    lv_label_set_text(cwLabel, LV_SYMBOL_RIGHT);
    lv_obj_center(cwLabel);
    
    // Status label removed - too much clutter
    m_statusLabel = nullptr;
    
    ESP_LOGI(TAG, "Virtual encoder panel created (compact)");
    return m_panel;
}

void VirtualEncoderPanel::setActiveKnob(int knobId)
{
    if (knobId < 0 || knobId > 1) return;
    
    m_activeKnob = knobId;
    updateKnobButtons();
    
    ESP_LOGI(TAG, "Active knob: %d", knobId);
}

void VirtualEncoderPanel::updateKnobButtons()
{
    for (int i = 0; i < 2; i++) {
        if (m_knobSelectButtons[i]) {
            if (i == m_activeKnob) {
                lv_obj_set_style_bg_color(m_knobSelectButtons[i], lv_palette_main(LV_PALETTE_GREEN), 0);
            } else {
                lv_obj_set_style_bg_color(m_knobSelectButtons[i], lv_palette_main(LV_PALETTE_GREY), 0);
            }
        }
    }
}

void VirtualEncoderPanel::onKnobSelectClicked(lv_event_t* e)
{
    VirtualEncoderPanel* panel = static_cast<VirtualEncoderPanel*>(lv_event_get_user_data(e));
    lv_obj_t* btn = lv_event_get_target(e);
    int knobId = (int)(intptr_t)lv_obj_get_user_data(btn);
    
    panel->setActiveKnob(knobId);
}

void VirtualEncoderPanel::onRotateCWClicked(lv_event_t* e)
{
    VirtualEncoderPanel* panel = static_cast<VirtualEncoderPanel*>(lv_event_get_user_data(e));
    
    if (panel->m_rotationCallback) {
        panel->m_rotationCallback(panel->m_userData, panel->m_activeKnob, 1);  // +1 for CW
    }
    
    ESP_LOGD(TAG, "Knob %d rotated CW (+1)", panel->m_activeKnob);
}

void VirtualEncoderPanel::onRotateCCWClicked(lv_event_t* e)
{
    VirtualEncoderPanel* panel = static_cast<VirtualEncoderPanel*>(lv_event_get_user_data(e));
    
    if (panel->m_rotationCallback) {
        panel->m_rotationCallback(panel->m_userData, panel->m_activeKnob, -1);  // -1 for CCW
    }
    
    ESP_LOGD(TAG, "Knob %d rotated CCW (-1)", panel->m_activeKnob);
}

void VirtualEncoderPanel::onPressClicked(lv_event_t* e)
{
    VirtualEncoderPanel* panel = static_cast<VirtualEncoderPanel*>(lv_event_get_user_data(e));
    
    if (panel->m_pressCallback) {
        panel->m_pressCallback(panel->m_userData, panel->m_activeKnob);
    }
    
    ESP_LOGI(TAG, "Knob %d pressed", panel->m_activeKnob);
}
