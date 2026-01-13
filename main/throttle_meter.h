// ThrottleMeter - simple LVGL based meter widget
#pragma once

#include "lvgl.h"
#include <stdint.h>

typedef struct ThrottleMeter ThrottleMeter_t;

// Create a ThrottleMeter inside `parent`.
// `scale` is a multiplier applied to the widget base size (1.0 = default size).
ThrottleMeter_t * throttle_meter_create(lv_obj_t * parent, float scale);

void throttle_meter_set_value(ThrottleMeter_t * tm, int32_t v);
int32_t throttle_meter_get_value(ThrottleMeter_t * tm);
void throttle_meter_set_scale(ThrottleMeter_t * tm, float scale);

// Configure value range (min <= value <= max)
void throttle_meter_set_range(ThrottleMeter_t * tm, int32_t min, int32_t max);

// Set unit text displayed next to numeric value (caller retains ownership of the string)
void throttle_meter_set_unit(ThrottleMeter_t * tm, const char * unit);

// Start/stop an automatic animation that cycles the needle between min and max.
// `time_ms` is the forward animation duration, `playback_ms` is the return duration.
void throttle_meter_start_anim(ThrottleMeter_t * tm, uint32_t time_ms, uint32_t playback_ms);
void throttle_meter_stop_anim(ThrottleMeter_t * tm);
