#include "RosterCarousel.h"
#include "esp_log.h"

static const char* TAG = "RosterCarousel";

RosterCarousel::RosterCarousel()
    : m_panel(nullptr)
    , m_currentLabel(nullptr)
    , m_idLabel(nullptr)
    , m_positionLabel(nullptr)
    , m_leftArrow(nullptr)
    , m_rightArrow(nullptr)
    , m_lastRosterIndex(-1)
{
}

lv_obj_t* RosterCarousel::create(lv_obj_t* parent)
{
    m_panel = lv_obj_create(parent);
    lv_obj_set_size(m_panel, LV_PCT(90), 120);
    lv_obj_set_style_pad_all(m_panel, 6, 0);

    m_leftArrow = lv_label_create(m_panel);
    lv_label_set_text(m_leftArrow, "<");
    lv_obj_set_style_text_font(m_leftArrow, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(m_leftArrow, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_align(m_leftArrow, LV_ALIGN_LEFT_MID, 0, 0);

    m_rightArrow = lv_label_create(m_panel);
    lv_label_set_text(m_rightArrow, ">");
    lv_obj_set_style_text_font(m_rightArrow, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(m_rightArrow, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_align(m_rightArrow, LV_ALIGN_RIGHT_MID, 0, 0);

    m_positionLabel = lv_label_create(m_panel);
    lv_label_set_text(m_positionLabel, "");
    lv_obj_set_style_text_font(m_positionLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(m_positionLabel, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_align(m_positionLabel, LV_ALIGN_TOP_MID, 0, 2);

    m_currentLabel = lv_label_create(m_panel);
    lv_label_set_text(m_currentLabel, "No roster");
    lv_obj_set_width(m_currentLabel, 220);
    lv_label_set_long_mode(m_currentLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_font(m_currentLabel, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(m_currentLabel, lv_color_white(), 0);
    lv_obj_set_style_text_align(m_currentLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(m_currentLabel, LV_ALIGN_CENTER, 0, 6);

    m_idLabel = lv_label_create(m_panel);
    lv_label_set_text(m_idLabel, "");
    lv_obj_set_style_text_font(m_idLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(m_idLabel, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_text_align(m_idLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(m_idLabel, LV_ALIGN_BOTTOM_MID, 0, -4);

    lv_obj_add_flag(m_panel, LV_OBJ_FLAG_HIDDEN);

    ESP_LOGI(TAG, "Roster carousel created");
    return m_panel;
}

void RosterCarousel::update(ThrottleController* controller)
{
    if (!m_panel || !m_currentLabel || !controller) {
        return;
    }

    ThrottleController::RosterSelectionSnapshot selection;
    if (!controller->getRosterSelectionSnapshot(selection)) {
        return;
    }

    if (!selection.active) {
        lv_obj_add_flag(m_panel, LV_OBJ_FLAG_HIDDEN);
        m_lastRosterIndex = -1;
        return;
    }

    lv_obj_clear_flag(m_panel, LV_OBJ_FLAG_HIDDEN);

    size_t rosterSize = controller->getRosterSize();
    if (rosterSize == 0) {
        lv_label_set_text(m_currentLabel, "No roster");
        if (m_idLabel) {
            lv_label_set_text(m_idLabel, "");
        }
        if (m_positionLabel) {
            lv_label_set_text(m_positionLabel, "");
        }
        return;
    }

    WiThrottleClient::Locomotive entry;
    std::string currentText = "Unknown";
    int currentAddress = 0;
    if (controller->getLocoAtRosterIndex(selection.rosterIndex, entry)) {
        currentText = entry.name;
        currentAddress = entry.address;
    }

    lv_label_set_text(m_currentLabel, currentText.c_str());

    if (m_idLabel) {
        char line[32];
        lv_snprintf(line, sizeof(line), "#%d", currentAddress);
        lv_label_set_text(m_idLabel, line);
    }

    if (m_positionLabel) {
        char positionText[32];
        lv_snprintf(positionText, sizeof(positionText), "%d/%d", selection.rosterIndex + 1, static_cast<int>(rosterSize));
        lv_label_set_text(m_positionLabel, positionText);
    }

    if (m_leftArrow) {
        lv_obj_set_style_text_opa(m_leftArrow, rosterSize > 1 ? LV_OPA_COVER : LV_OPA_30, 0);
    }
    if (m_rightArrow) {
        lv_obj_set_style_text_opa(m_rightArrow, rosterSize > 1 ? LV_OPA_COVER : LV_OPA_30, 0);
    }

    if (m_lastRosterIndex >= 0 && m_lastRosterIndex != selection.rosterIndex) {
        int delta = selection.rosterIndex - m_lastRosterIndex;
        if (delta > 1) delta = -1;
        if (delta < -1) delta = 1;

        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, m_currentLabel);
        lv_anim_set_exec_cb(&anim, [](void* var, int32_t value) {
            lv_obj_set_x(static_cast<lv_obj_t*>(var), value);
        });
        lv_anim_set_time(&anim, 140);
        lv_anim_set_values(&anim, delta > 0 ? 16 : -16, 0);
        lv_anim_start(&anim);
    } else {
        lv_obj_set_x(m_currentLabel, 0);
    }

    m_lastRosterIndex = selection.rosterIndex;
}
