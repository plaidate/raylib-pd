/*******************************************************************************************
*
*   raylib-pd [core] example - Playdate button input
*
*   Playdate analog of examples/core/core_input_keys.c: move a ball with the d-pad
*   (mapped to the arrow keys), recenter it with the A button (mapped to KEY_SPACE).
*
*   Example licensed under an unmodified zlib/libpng license, which is an OSI-certified,
*   BSD-like license that allows static linking with closed source software
*
********************************************************************************************/

#include "raylib.h"

//------------------------------------------------------------------------------------
// Program main entry point
//------------------------------------------------------------------------------------
int main(void)
{
    // Initialization
    //--------------------------------------------------------------------------------------
    const int screenWidth = 400;
    const int screenHeight = 240;

    InitWindow(screenWidth, screenHeight, "raylib-pd [core] example - button input");

    Vector2 ballPosition = { (float)screenWidth/2, (float)screenHeight/2 };

    SetTargetFPS(50);
    //--------------------------------------------------------------------------------------

    // Main game loop
    while (!WindowShouldClose())
    {
        // Update
        //----------------------------------------------------------------------------------
        if (IsKeyDown(KEY_RIGHT)) ballPosition.x += 3.0f;   // d-pad right
        if (IsKeyDown(KEY_LEFT)) ballPosition.x -= 3.0f;    // d-pad left
        if (IsKeyDown(KEY_UP)) ballPosition.y -= 3.0f;      // d-pad up
        if (IsKeyDown(KEY_DOWN)) ballPosition.y += 3.0f;    // d-pad down

        if (IsKeyPressed(KEY_SPACE))                        // A button
        {
            ballPosition.x = (float)screenWidth/2;
            ballPosition.y = (float)screenHeight/2;
        }
        //----------------------------------------------------------------------------------

        // Draw
        //----------------------------------------------------------------------------------
        BeginDrawing();

            ClearBackground(RAYWHITE);

            DrawText("move the ball with the d-pad, A to recenter", 20, 10, 10, DARKGRAY);

            DrawCircleV(ballPosition, 25, MAROON);

            if (IsKeyDown(KEY_SPACE)) DrawText("A", 190, 220, 20, BLACK);
            if (IsKeyDown(KEY_ENTER)) DrawText("B", 210, 220, 20, BLACK);

        EndDrawing();
        //----------------------------------------------------------------------------------
    }

    // De-Initialization
    //--------------------------------------------------------------------------------------
    CloseWindow();
    //--------------------------------------------------------------------------------------

    return 0;
}
