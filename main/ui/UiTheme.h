#pragma once

#include "lvgl.h"

/**
 * @brief The one place UI colours are defined.
 *
 * Before this the app mixed two styles: raw hex like 0x00AA00 on the config
 * screens, and LVGL's own `lv_palette_main(LV_PALETTE_GREEN)` on the widgets.
 * Both are saturated, and the two never quite matched.
 *
 * The palette here is the muted set introduced with the orchestrator screens,
 * adopted app-wide. Buttons are desaturated so a screenful of them reads as one
 * surface rather than a set of warnings; status *text* stays brighter, because
 * a small label has to carry its meaning at a glance.
 *
 * Colours are named for what they mean, not what they look like. A red button
 * is `BUTTON_DESTRUCTIVE`, so a later change of shade needs one edit here and
 * none anywhere else.
 */
namespace UiTheme {

// --- Buttons ---------------------------------------------------------------

/** Navigation and the ordinary primary action. */
constexpr uint32_t BUTTON_PRIMARY = 0x1d4e89;

/** Connect, save, confirm. */
constexpr uint32_t BUTTON_POSITIVE = 0x2d6a4f;

/** Disconnect, release, forget. Destructive or disconnecting, not "error". */
constexpr uint32_t BUTTON_DESTRUCTIVE = 0x8c3232;

/** A step back from destructive: disconnect-but-keep-credentials. */
constexpr uint32_t BUTTON_CAUTION = 0x8a5a24;

/** Back, cancel, and anything inactive. */
constexpr uint32_t BUTTON_NEUTRAL = 0x555555;

// --- State indicators ------------------------------------------------------
//
// Used on small widgets that signal state rather than invite a press: knob
// assignment pips, the direction badge, the track-power button.

constexpr uint32_t STATE_ACTIVE = 0x2d6a4f;
constexpr uint32_t STATE_ALTERNATE = 0x1d4e89;
constexpr uint32_t STATE_INACTIVE = 0x4a4a4a;
constexpr uint32_t STATE_FAULT = 0x8c3232;

// --- Text ------------------------------------------------------------------
//
// Brighter than the button fills on purpose: a one-line status label has far
// less area to carry its meaning with.

constexpr uint32_t TEXT_OK = 0x40c057;
constexpr uint32_t TEXT_WARNING = 0xfab005;
constexpr uint32_t TEXT_ERROR = 0xe03131;
constexpr uint32_t TEXT_MUTED = 0x999999;
constexpr uint32_t TEXT_LABEL = 0xcccccc;

// --- Surfaces --------------------------------------------------------------

constexpr uint32_t SURFACE_SCREEN = 0x000000;
constexpr uint32_t SURFACE_PANEL = 0x1a1a1a;
constexpr uint32_t SURFACE_OVERLAY = 0x333333;

/** Convenience so call sites read `UiTheme::colour(UiTheme::BUTTON_PRIMARY)`. */
inline lv_color_t colour(uint32_t rgb)
{
    return lv_color_hex(rgb);
}

}  // namespace UiTheme
