/**********************************************************************************************
*
*   pd_raylib.h - Playdate-specific raylib extensions (PLATFORM_PLAYDATE)
*
*   Implemented in src/platforms/rcore_playdate.c. Regular raylib API covers the rest:
*     d-pad  -> KEY_UP/KEY_DOWN/KEY_LEFT/KEY_RIGHT (also gamepad 0 left-face buttons)
*     A      -> KEY_SPACE (also GAMEPAD_BUTTON_RIGHT_FACE_DOWN)
*     B      -> KEY_ENTER (also GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)
*     crank  -> GetMouseWheelMove() (one full turn = 1.0), or the functions below
*
**********************************************************************************************/

#ifndef PD_RAYLIB_H
#define PD_RAYLIB_H

#include "raylib.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct PlaydateAPI PlaydateAPI;

float GetCrankAngle(void);          // Current crank angle (degrees, [0..360), 0 = up)
float GetCrankChange(void);         // Crank angle variation since last frame (degrees)
bool IsCrankDocked(void);           // Check if the crank is docked

// The raw Playdate C API. WARNING: raylib code runs on a game thread; most
// pd->system/display/graphics calls are only safe from the update-callback side
PlaydateAPI *GetPlaydateAPI(void);

#if defined(__cplusplus)
}
#endif

#endif // PD_RAYLIB_H
