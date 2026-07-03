/*******************************************************************************************
*
*   raylib-pd [core] example - crank input
*
*   Playdate-specific input test: the crank drives a needle on a gauge, using the
*   pd_raylib.h extension API (GetCrankAngle/GetCrankChange/IsCrankDocked). The
*   crank also feeds GetMouseWheelMove() (one full turn = 1.0), shown as a counter.
*
*   Example licensed under an unmodified zlib/libpng license, which is an OSI-certified,
*   BSD-like license that allows static linking with closed source software
*
********************************************************************************************/

#include "raylib.h"
#include "pd_raylib.h"

#include <math.h>

//------------------------------------------------------------------------------------
// Program main entry point
//------------------------------------------------------------------------------------
int main(void)
{
    // Initialization
    //--------------------------------------------------------------------------------------
    const int screenWidth = 400;
    const int screenHeight = 240;

    InitWindow(screenWidth, screenHeight, "raylib-pd [core] example - crank input");

    const Vector2 center = { (float)screenWidth/2, (float)screenHeight/2 + 20 };
    const float radius = 70.0f;
    float turns = 0.0f;     // Accumulated full crank revolutions (from the wheel mapping)

    SetTargetFPS(50);
    //--------------------------------------------------------------------------------------

    // Main game loop
    while (!WindowShouldClose())
    {
        // Update
        //----------------------------------------------------------------------------------
        float angle = GetCrankAngle();          // Degrees, 0 = up
        turns += GetMouseWheelMove();           // Crank maps to the mouse wheel
        //----------------------------------------------------------------------------------

        // Draw
        //----------------------------------------------------------------------------------
        BeginDrawing();

            ClearBackground(RAYWHITE);

            DrawText("turn the crank", 150, 10, 10, DARKGRAY);

            // Gauge with a tick every 45 degrees
            DrawCircleLinesV(center, radius, BLACK);
            for (int i = 0; i < 8; i++)
            {
                float a = (float)i*45.0f*DEG2RAD;
                Vector2 t0 = { center.x + sinf(a)*(radius - 6), center.y - cosf(a)*(radius - 6) };
                Vector2 t1 = { center.x + sinf(a)*radius, center.y - cosf(a)*radius };
                DrawLineV(t0, t1, BLACK);
            }

            // Needle
            float rad = angle*DEG2RAD;
            Vector2 tip = { center.x + sinf(rad)*(radius - 12), center.y - cosf(rad)*(radius - 12) };
            DrawLineEx(center, tip, 3.0f, MAROON);
            DrawCircleV(center, 5, BLACK);

            DrawText(TextFormat("angle: %6.1f deg", angle), 10, 210, 10, BLACK);
            DrawText(TextFormat("turns: %+6.2f", turns), 150, 210, 10, BLACK);
            if (IsCrankDocked()) DrawText("crank is docked!", 280, 210, 10, MAROON);

        EndDrawing();
        //----------------------------------------------------------------------------------
    }

    // De-Initialization
    //--------------------------------------------------------------------------------------
    CloseWindow();
    //--------------------------------------------------------------------------------------

    return 0;
}
