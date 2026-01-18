#include "ThrottleMeter.h"

ThrottleMeter::ThrottleMeter(lv_obj_t* parent, float scale)
    : m_container(nullptr)
    , m_meter(nullptr)
    , m_scaleId(nullptr)
    , m_needle(nullptr)
    , m_valueLabel(nullptr)
    , m_unitLabel(nullptr)
    , m_locoLabel(nullptr)
    , m_functionsButton(nullptr)
    , m_releaseButton(nullptr)
    , m_min(0)
    , m_max(126)
    , m_value(0)
    , m_scale(scale > 0 ? scale : 1.0f)
    , m_animRunning(false)
    , m_assignedKnob(-1)
    , m_userData(nullptr)
{
    m_knobIndicators[0] = nullptr;
    m_knobIndicators[1] = nullptr;
    m_knobAvailable[0] = true;
    m_knobAvailable[1] = true;
    // Create container
    m_container = lv_obj_create(parent);
    lv_obj_remove_style_all(m_container);
    lv_obj_set_size(m_container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(m_container, LV_FLEX_FLOW_COLUMN);
    
    // Create the meter
    m_meter = lv_meter_create(m_container);
    lv_obj_remove_style(m_meter, nullptr, LV_PART_MAIN);
    lv_obj_remove_style(m_meter, nullptr, LV_PART_INDICATOR);
    lv_obj_set_width(m_meter, LV_PCT(100));
    
    // Indicator pivot style similar to demo's meter3
    lv_obj_set_style_pad_hor(m_meter, 10, 0);
    lv_obj_set_style_size(m_meter, 10, LV_PART_INDICATOR);
    lv_obj_set_style_radius(m_meter, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(m_meter, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(m_meter, lv_palette_darken(LV_PALETTE_GREY, 4), LV_PART_INDICATOR);
    lv_obj_set_style_outline_color(m_meter, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_outline_width(m_meter, 3, LV_PART_INDICATOR);
    lv_obj_set_style_text_color(m_meter, lv_palette_darken(LV_PALETTE_GREY, 1), LV_PART_TICKS);
    
    // Create scale
    m_scaleId = lv_meter_add_scale(m_meter);
    lv_meter_set_scale_range(m_meter, m_scaleId, m_min, m_max, 220, 360 - 220);
    lv_meter_set_scale_ticks(m_meter, m_scaleId, 21, 3, 17, lv_color_white());
    lv_meter_set_scale_major_ticks(m_meter, m_scaleId, 4, 4, 22, lv_color_white(), 15);
    
    // Color arcs and lines (three zones)
    lv_meter_indicator_t* indic;
    
    // Red zone (0-20)
    indic = lv_meter_add_arc(m_meter, m_scaleId, 10, lv_palette_main(LV_PALETTE_RED), 0);
    lv_meter_set_indicator_start_value(m_meter, indic, 0);
    lv_meter_set_indicator_end_value(m_meter, indic, 20);
    
    indic = lv_meter_add_scale_lines(m_meter, m_scaleId, 
        lv_palette_darken(LV_PALETTE_RED, 3), 
        lv_palette_darken(LV_PALETTE_RED, 3), true, 0);
    lv_meter_set_indicator_start_value(m_meter, indic, 0);
    lv_meter_set_indicator_end_value(m_meter, indic, 20);
    
    // Blue zone (20-40)
    indic = lv_meter_add_arc(m_meter, m_scaleId, 12, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_meter_set_indicator_start_value(m_meter, indic, 20);
    lv_meter_set_indicator_end_value(m_meter, indic, 40);
    
    indic = lv_meter_add_scale_lines(m_meter, m_scaleId, 
        lv_palette_darken(LV_PALETTE_BLUE, 3), 
        lv_palette_darken(LV_PALETTE_BLUE, 3), true, 0);
    lv_meter_set_indicator_start_value(m_meter, indic, 20);
    lv_meter_set_indicator_end_value(m_meter, indic, 40);
    
    // Green zone (40-60)
    indic = lv_meter_add_arc(m_meter, m_scaleId, 10, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_meter_set_indicator_start_value(m_meter, indic, 40);
    lv_meter_set_indicator_end_value(m_meter, indic, 60);
    
    indic = lv_meter_add_scale_lines(m_meter, m_scaleId, 
        lv_palette_darken(LV_PALETTE_GREEN, 3), 
        lv_palette_darken(LV_PALETTE_GREEN, 3), true, 0);
    lv_meter_set_indicator_start_value(m_meter, indic, 40);
    lv_meter_set_indicator_end_value(m_meter, indic, 60);
    
    // Needle
    m_needle = lv_meter_add_needle_line(m_meter, m_scaleId, 4, 
        lv_palette_darken(LV_PALETTE_GREY, 4), -25);
    
    // Value label
    m_valueLabel = lv_label_create(m_meter);
    lv_label_set_text(m_valueLabel, "0");
    
    // Unit label  
    m_unitLabel = lv_label_create(m_meter);
    lv_label_set_text(m_unitLabel, "");
    
    // Create additional UI elements
    createKnobIndicators();
    createButtons();
    
    // Loco label (below meter)
    m_locoLabel = lv_label_create(m_container);
    lv_label_set_text(m_locoLabel, "");
    lv_obj_set_style_text_align(m_locoLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(m_locoLabel, LV_PCT(100));
    
    // Apply initial settings
    setScale(m_scale);
    setValue(m_value);
}

ThrottleMeter::~ThrottleMeter()
{
    if (m_animRunning) {
        stopAnimation();
    }
    
    // Don't delete LVGL objects - they're managed by parent screen
    // LVGL will clean up the hierarchy when the screen is unloaded
}

void ThrottleMeter::setValue(int32_t value)
{
    // Clamp to range
    if (value < m_min) value = m_min;
    if (value > m_max) value = m_max;
    
    m_value = value;
    
    if (m_needle) {
        lv_meter_set_indicator_value(m_meter, m_needle, value);
    }
    
    if (m_valueLabel) {
        lv_label_set_text_fmt(m_valueLabel, "%" LV_PRId32, value);
    }
}

void ThrottleMeter::setScale(float scale)
{
    m_scale = scale > 0 ? scale : 1.0f;
    
    lv_coord_t size = static_cast<lv_coord_t>(BASE_SIZE * m_scale);
    lv_obj_set_size(m_container, size, size);
    
    // Make the meter roughly square inside the container
    lv_obj_set_size(m_meter, lv_pct(100), lv_pct(100));
    
    // Align labels relative to meter
    lv_obj_align(m_valueLabel, LV_ALIGN_TOP_MID, 10, lv_pct(55));
    lv_obj_align_to(m_unitLabel, m_valueLabel, LV_ALIGN_OUT_RIGHT_BOTTOM, 10, 0);
}

void ThrottleMeter::setRange(int32_t min, int32_t max)
{
    if (min >= max) {
        return;
    }
    
    m_min = min;
    m_max = max;
    
    if (m_scaleId) {
        lv_meter_set_scale_range(m_meter, m_scaleId, m_min, m_max, 220, 360 - 220);
    }
    
    // Ensure current value is inside new range
    setValue(m_value);
}

void ThrottleMeter::setUnit(const char* unit)
{
    if (!unit) {
        return;
    }
    
    if (m_unitLabel) {
        lv_label_set_text(m_unitLabel, unit);
    }
}

void ThrottleMeter::startAnimation(uint32_t timeMs, uint32_t playbackMs)
{
    if (m_animRunning) {
        return;
    }
    
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, this);
    lv_anim_set_exec_cb(&a, animationCallback);
    lv_anim_set_values(&a, m_min, m_max);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_time(&a, timeMs);
    lv_anim_set_playback_time(&a, playbackMs);
    lv_anim_start(&a);
    
    m_animRunning = true;
}

void ThrottleMeter::stopAnimation()
{
    if (!m_animRunning) {
        return;
    }
    
    lv_anim_del(this, animationCallback);
    m_animRunning = false;
}

void ThrottleMeter::animationCallback(void* var, int32_t value)
{
    ThrottleMeter* tm = static_cast<ThrottleMeter*>(var);
    if (!tm) {
        return;
    }
    
    if (tm->m_needle) {
        lv_meter_set_indicator_value(tm->m_meter, tm->m_needle, value);
    }
    
    if (tm->m_valueLabel) {
        lv_label_set_text_fmt(tm->m_valueLabel, "%" LV_PRId32, value);
    }
}

void ThrottleMeter::createKnobIndicators()
{
    // Create L and R indicators at the bottom corners of the meter
    const char* labels[2] = {"L", "R"};
    
    for (int i = 0; i < 2; i++) {
        m_knobIndicators[i] = lv_btn_create(m_meter);
        lv_obj_set_size(m_knobIndicators[i], 40, 30);
        lv_obj_align(m_knobIndicators[i], i == 0 ? LV_ALIGN_BOTTOM_LEFT : LV_ALIGN_BOTTOM_RIGHT, i == 0 ? 5 : -5, -5);
        
        lv_obj_t* label = lv_label_create(m_knobIndicators[i]);
        lv_label_set_text(label, labels[i]);
        lv_obj_center(label);
        
        // Store knob ID in user data
        lv_obj_set_user_data(m_knobIndicators[i], (void*)(intptr_t)i);
    }
    
    updateKnobIndicators();
}

void ThrottleMeter::createButtons()
{
    // Create functions and release buttons (initially hidden)
    m_functionsButton = lv_btn_create(m_container);
    lv_obj_set_size(m_functionsButton, LV_PCT(45), 35);
    lv_obj_add_flag(m_functionsButton, LV_OBJ_FLAG_HIDDEN);
    
    lv_obj_t* funcLabel = lv_label_create(m_functionsButton);
    lv_label_set_text(funcLabel, "Functions");
    lv_obj_center(funcLabel);
    
    m_releaseButton = lv_btn_create(m_container);
    lv_obj_set_size(m_releaseButton, LV_PCT(45), 35);
    lv_obj_add_flag(m_releaseButton, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(m_releaseButton, lv_palette_main(LV_PALETTE_RED), 0);
    
    lv_obj_t* releaseLabel = lv_label_create(m_releaseButton);
    lv_label_set_text(releaseLabel, "Release");
    lv_obj_center(releaseLabel);
}

void ThrottleMeter::updateKnobIndicators()
{
    for (int i = 0; i < 2; i++) {
        if (!m_knobIndicators[i]) continue;
        
        if (m_assignedKnob == i) {
            // Active knob - highlight
            lv_obj_set_style_bg_color(m_knobIndicators[i], lv_palette_main(LV_PALETTE_GREEN), 0);
            lv_obj_clear_state(m_knobIndicators[i], LV_STATE_DISABLED);
        } else if (!m_knobAvailable[i]) {
            // Unavailable (other knob is active) - gray out
            lv_obj_set_style_bg_color(m_knobIndicators[i], lv_palette_main(LV_PALETTE_GREY), 0);
            lv_obj_add_state(m_knobIndicators[i], LV_STATE_DISABLED);
        } else {
            // Available - normal button color
            lv_obj_set_style_bg_color(m_knobIndicators[i], lv_palette_main(LV_PALETTE_BLUE), 0);
            lv_obj_clear_state(m_knobIndicators[i], LV_STATE_DISABLED);
        }
    }
}

void ThrottleMeter::setLocomotive(const char* name, int address)
{
    char buf[64];
    lv_snprintf(buf, sizeof(buf), "%s (#%d)", name, address);
    lv_label_set_text(m_locoLabel, buf);
    
    // Show buttons when loco is assigned
    lv_obj_clear_flag(m_functionsButton, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(m_releaseButton, LV_OBJ_FLAG_HIDDEN);
}

void ThrottleMeter::clearLocomotive()
{
    lv_label_set_text(m_locoLabel, "");
    
    // Hide buttons when no loco
    lv_obj_add_flag(m_functionsButton, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(m_releaseButton, LV_OBJ_FLAG_HIDDEN);
}

void ThrottleMeter::setAssignedKnob(int knobId)
{
    m_assignedKnob = knobId;
    updateKnobIndicators();
}

void ThrottleMeter::setKnobAvailable(int knobId, bool available)
{
    if (knobId >= 0 && knobId < 2) {
        m_knobAvailable[knobId] = available;
        updateKnobIndicators();
    }
}

void ThrottleMeter::setKnobTouchCallback(lv_event_cb_t callback, void* userData)
{
    m_userData = userData;
    for (int i = 0; i < 2; i++) {
        if (m_knobIndicators[i]) {
            lv_obj_add_event_cb(m_knobIndicators[i], callback, LV_EVENT_CLICKED, userData);
        }
    }
}

void ThrottleMeter::setFunctionsCallback(lv_event_cb_t callback, void* userData)
{
    if (m_functionsButton) {
        lv_obj_add_event_cb(m_functionsButton, callback, LV_EVENT_CLICKED, userData);
    }
}

void ThrottleMeter::setReleaseCallback(lv_event_cb_t callback, void* userData)
{
    if (m_releaseButton) {
        lv_obj_add_event_cb(m_releaseButton, callback, LV_EVENT_CLICKED, userData);
    }
}
