#include "test_throttle_screen.h"
#include "throttle_meter.h"

void test_throttle_screen(void)
{
    lv_obj_t * scr = lv_scr_act();

    // Main container: horizontal split left/right
    lv_obj_t * main_cont = lv_obj_create(scr);
    lv_obj_remove_style_all(main_cont);
    lv_obj_set_size(main_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(main_cont, LV_FLEX_FLOW_ROW);

    // Left half (will contain 2x2 grid of meters)
    lv_obj_t * left = lv_obj_create(main_cont);
    lv_obj_set_size(left, LV_PCT(50), LV_PCT(100));
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);

    // Right half left empty for now
    lv_obj_t * right = lv_obj_create(main_cont);
    lv_obj_set_size(right, LV_PCT(50), LV_PCT(100));

    // Grid on left: 2 columns x 2 rows
    static lv_coord_t grid_col[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t grid_row[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(left, grid_col, grid_row);

    // Create 4 containers and place ThrottleMeters inside
    for(int r = 0; r < 2; ++r) {
        for(int c = 0; c < 2; ++c) {
            lv_obj_t * cell = lv_obj_create(left);
            lv_obj_remove_style_all(cell);
            lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_STRETCH, c, 1, LV_GRID_ALIGN_STRETCH, r, 1);
            // Create meter sized to fill cell. Use scale 0.9 to leave padding.
            ThrottleMeter_t * tm = throttle_meter_create(cell, 0.9f);
            (void)tm; // keep pointer if application needs later
        }
    }
}
