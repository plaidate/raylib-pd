/*******************************************************************************************
*
*   raylib-pd [core] example - basic window
*
*   Playdate analog of examples/core/core_basic_window.c: open the (fixed 400x240,
*   1-bit) "window" and draw a message.
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

    InitWindow(screenWidth, screenHeight, "raylib-pd [core] example - basic window");

    SetTargetFPS(50);               // Playdate refresh rate is 50 fps max
    //--------------------------------------------------------------------------------------

    // Main game loop
    while (!WindowShouldClose())
    {
        // Draw
        //----------------------------------------------------------------------------------
        BeginDrawing();

            ClearBackground(RAYWHITE);

            DrawText("Congrats! You created", 70, 90, 20, BLACK);
            DrawText("your first Playdate window!", 70, 115, 20, BLACK);

            DrawRectangleLines(10, 10, screenWidth - 20, screenHeight - 20, GRAY);

        EndDrawing();
        //----------------------------------------------------------------------------------
    }

    // De-Initialization
    //--------------------------------------------------------------------------------------
    CloseWindow();
    //--------------------------------------------------------------------------------------

    return 0;
}
