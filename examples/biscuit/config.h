#pragma once

/* Application/user-facing configuration. Adjust here without touching core logic. */

#define APP_WINDOW_TITLE "Biscuit Fuel"
#define APP_DEFAULT_WIDTH 1280
#define APP_DEFAULT_HEIGHT 720

/* Ortho camera looking down -Z onto XY. Height in world units. */
#define APP_CAMERA_HEIGHT 5.4f

/* Sim sub-step. Not a 1000 Hz thread — main iterate still owns the loop. */
#define APP_FIXED_DT 0.004f
#define APP_MAX_SUBSTEPS 12

#define APP_START_CAR_X 0.0f
#define APP_START_CAR_Y 1.15f

#define GAME_SPAWN_ACTIVATE_RADIUS 2.8f
#define CAR_HOP_IMPULSE 9.5f

/* Unity-like Debug.DrawLine overlay (track segs, wheel circles). */
#define APP_DEBUG_DRAW 1

#define APP_SELFTEST_BMP "/home/user/ame-next/biscuit.bmp"
