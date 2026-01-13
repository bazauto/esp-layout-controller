#include "throttle_meter.h"
#include <stdlib.h>

#include <stdbool.h>
#include <string.h>

struct ThrottleMeter {
    lv_obj_t * cont;
    lv_obj_t * meter;
    lv_meter_scale_t * scale_id;
    lv_meter_indicator_t * needle;
    lv_obj_t * value_label;
    lv_obj_t * unit_label;
    int32_t min;
    int32_t max;
    int32_t value;
    float scale;
    bool anim_running;
};

// base pixel size used when computing widget sizes (tunable)
static const lv_coord_t base_size = 200;

ThrottleMeter_t * throttle_meter_create(lv_obj_t * parent, float scale)
{
    ThrottleMeter_t * tm = calloc(1, sizeof(*tm));
    if(!tm) return NULL;
    tm->min = 10;
    tm->max = 60;
    tm->value = tm->min;
    tm->scale = scale > 0 ? scale : 1.0f;

    tm->cont = lv_obj_create(parent);
    lv_obj_remove_style_all(tm->cont);
    lv_obj_set_size(tm->cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(tm->cont, LV_FLEX_FLOW_COLUMN);

    // create the meter
    tm->meter = lv_meter_create(tm->cont);
    lv_obj_remove_style(tm->meter, NULL, LV_PART_MAIN);
    lv_obj_remove_style(tm->meter, NULL, LV_PART_INDICATOR);
    lv_obj_set_width(tm->meter, LV_PCT(100));

    // indicator pivot style similar to demo's meter3
    lv_obj_set_style_pad_hor(tm->meter, 10, 0);
    lv_obj_set_style_size(tm->meter, 10, LV_PART_INDICATOR);
    lv_obj_set_style_radius(tm->meter, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(tm->meter, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(tm->meter, lv_palette_darken(LV_PALETTE_GREY, 4), LV_PART_INDICATOR);
    lv_obj_set_style_outline_color(tm->meter, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_outline_width(tm->meter, 3, LV_PART_INDICATOR);
    lv_obj_set_style_text_color(tm->meter, lv_palette_darken(LV_PALETTE_GREY, 1), LV_PART_TICKS);

    // create scale
    tm->scale_id = lv_meter_add_scale(tm->meter);
    lv_meter_set_scale_range(tm->meter, tm->scale_id, tm->min, tm->max, 220, 360 - 220);
    lv_meter_set_scale_ticks(tm->meter, tm->scale_id, 21, 3, 17, lv_color_white());
    lv_meter_set_scale_major_ticks(tm->meter, tm->scale_id, 4, 4, 22, lv_color_white(), 15);

    // color arcs and lines (three zones)
    lv_meter_indicator_t * indic;
    indic = lv_meter_add_arc(tm->meter, tm->scale_id, 10, lv_palette_main(LV_PALETTE_RED), 0);
    lv_meter_set_indicator_start_value(tm->meter, indic, 0);
    lv_meter_set_indicator_end_value(tm->meter, indic, 20);

    indic = lv_meter_add_scale_lines(tm->meter, tm->scale_id, lv_palette_darken(LV_PALETTE_RED, 3), lv_palette_darken(LV_PALETTE_RED, 3), true, 0);
    lv_meter_set_indicator_start_value(tm->meter, indic, 0);
    lv_meter_set_indicator_end_value(tm->meter, indic, 20);

    indic = lv_meter_add_arc(tm->meter, tm->scale_id, 12, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_meter_set_indicator_start_value(tm->meter, indic, 20);
    lv_meter_set_indicator_end_value(tm->meter, indic, 40);

    indic = lv_meter_add_scale_lines(tm->meter, tm->scale_id, lv_palette_darken(LV_PALETTE_BLUE, 3), lv_palette_darken(LV_PALETTE_BLUE, 3), true, 0);
    lv_meter_set_indicator_start_value(tm->meter, indic, 20);
    lv_meter_set_indicator_end_value(tm->meter, indic, 40);

    indic = lv_meter_add_arc(tm->meter, tm->scale_id, 10, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_meter_set_indicator_start_value(tm->meter, indic, 40);
    lv_meter_set_indicator_end_value(tm->meter, indic, 60);

    indic = lv_meter_add_scale_lines(tm->meter, tm->scale_id, lv_palette_darken(LV_PALETTE_GREEN, 3), lv_palette_darken(LV_PALETTE_GREEN, 3), true, 0);
    lv_meter_set_indicator_start_value(tm->meter, indic, 40);
    lv_meter_set_indicator_end_value(tm->meter, indic, 60);

    // needle
    tm->needle = lv_meter_add_needle_line(tm->meter, tm->scale_id, 4, lv_palette_darken(LV_PALETTE_GREY, 4), -25);

    // value label and unit
    tm->value_label = lv_label_create(tm->meter);
    lv_label_set_text(tm->value_label, "-");

    tm->unit_label = lv_label_create(tm->meter);
    lv_label_set_text(tm->unit_label, "Mbps");

    tm->anim_running = false;

    // apply initial size according to scale
    throttle_meter_set_scale(tm, tm->scale);
    throttle_meter_set_value(tm, tm->value);

    return tm;
}

void throttle_meter_set_value(ThrottleMeter_t * tm, int32_t v)
{
    if(!tm) return;
    if(v < tm->min) v = tm->min;
    if(v > tm->max) v = tm->max;
    tm->value = v;
    if(tm->needle) lv_meter_set_indicator_value(tm->meter, tm->needle, v);
    if(tm->value_label) lv_label_set_text_fmt(tm->value_label, "%"LV_PRId32, v);
}

int32_t throttle_meter_get_value(ThrottleMeter_t * tm)
{
    if(!tm) return 0;
    return tm->value;
}

void throttle_meter_set_scale(ThrottleMeter_t * tm, float scale)
{
    if(!tm) return;
    tm->scale = scale > 0 ? scale : 1.0f;
    lv_coord_t size = (lv_coord_t)(base_size * tm->scale);
    lv_obj_set_size(tm->cont, size, size);
    // make the meter roughly square inside the container
    lv_obj_set_size(tm->meter, lv_pct(100), lv_pct(100));
    // align labels relative to meter
    lv_obj_align(tm->value_label, LV_ALIGN_TOP_MID, 10, lv_pct(55));
    lv_obj_align_to(tm->unit_label, tm->value_label, LV_ALIGN_OUT_RIGHT_BOTTOM, 10, 0);
}

void throttle_meter_set_range(ThrottleMeter_t * tm, int32_t min, int32_t max)
{
    if(!tm) return;
    if(min >= max) return;
    tm->min = min;
    tm->max = max;
    if(tm->scale_id) {
        lv_meter_set_scale_range(tm->meter, tm->scale_id, tm->min, tm->max, 220, 360 - 220);
    }
    // ensure current value is inside new range
    throttle_meter_set_value(tm, tm->value);
}

void throttle_meter_set_unit(ThrottleMeter_t * tm, const char * unit)
{
    if(!tm || !unit) return;
    if(tm->unit_label) lv_label_set_text(tm->unit_label, unit);
}

static void throttle_meter_anim_cb(void * var, int32_t v)
{
    ThrottleMeter_t * tm = var;
    if(!tm) return;
    if(tm->needle) lv_meter_set_indicator_value(tm->meter, tm->needle, v);
    if(tm->value_label) lv_label_set_text_fmt(tm->value_label, "%"LV_PRId32, v);
}

void throttle_meter_start_anim(ThrottleMeter_t * tm, uint32_t time_ms, uint32_t playback_ms)
{
    if(!tm) return;
    if(tm->anim_running) return;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, tm);
    lv_anim_set_exec_cb(&a, throttle_meter_anim_cb);
    lv_anim_set_values(&a, tm->min, tm->max);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_time(&a, time_ms);
    lv_anim_set_playback_time(&a, playback_ms);
    lv_anim_start(&a);
    tm->anim_running = true;
}

void throttle_meter_stop_anim(ThrottleMeter_t * tm)
{
    if(!tm) return;
    if(!tm->anim_running) return;
    lv_anim_del(tm, throttle_meter_anim_cb);
    tm->anim_running = false;
}
