#pragma once

/*
 * Compile-time fallbacks. Runtime overrides live in settings.yaml
 * (ame_settings) — prefer editing that file. These macros are the
 * defaults when YAML is missing or a key is absent.
 */

#define APP_WINDOW_TITLE "Biscuit Fuel"
#define APP_DEFAULT_WIDTH 1280
#define APP_DEFAULT_HEIGHT 720

/* Ortho camera looking down -Z onto XY. Height in world units. */
#define APP_CAMERA_HEIGHT 5.4f

/* Fallback fixed step when settings.yaml is absent (1000 Hz). */
#define APP_LOGIC_HZ_DEFAULT 1000.0f
#define APP_FIXED_DT_DEFAULT (1.0f / APP_LOGIC_HZ_DEFAULT)
/* Main-thread pump catch-up cap: must cover one display frame at logic.hz
 * (e.g. 60 Hz display × 1000 Hz logic ≈ 17 steps). Hitch clamp is 50 ms. */
#define APP_MAX_SUBSTEPS 64

#define APP_START_CAR_X 0.0f
#define APP_START_CAR_Y 1.15f

#define GAME_SPAWN_ACTIVATE_RADIUS 2.8f
#define CAR_HOP_IMPULSE 9.5f

/* Unity-like Debug.DrawLine overlay (track segs, wheel circles). */
#define APP_DEBUG_DRAW 1

#define APP_SELFTEST_BMP "biscuit.bmp"

/* Default settings path (cwd-relative). Override with --settings <file>. */
#define APP_SETTINGS_FILE "examples/biscuit/settings.yaml"
