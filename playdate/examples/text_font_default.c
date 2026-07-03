/*******************************************************************************************
*
*   raylib-pd [text] example - default font drawing
*
*   Playdate analog of the text examples: exercises the embedded default raylib font
*   (rtext + rtextures via the software renderer) at several sizes, plus FPS counter.
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

    InitWindow(screenWidth, screenHeight, "raylib-pd [text] example - default font");

    SetTargetFPS(50);
    //--------------------------------------------------------------------------------------

    // Main game loop
    while (!WindowShouldClose())
    {
        // Draw
        //----------------------------------------------------------------------------------
        BeginDrawing();

            ClearBackground(RAYWHITE);

            DrawText("raylib default font on Playdate", 20, 20, 20, BLACK);
            DrawText("size 10: the quick brown fox", 20, 60, 10, DARKGRAY);
            DrawText("size 20: jumps over", 20, 80, 20, DARKGRAY);
            DrawText("size 30: the lazy dog", 20, 110, 30, GRAY);
            DrawText("size 40: 0123456789", 20, 150, 40, BLACK);

            DrawFPS(320, 10);

        EndDrawing();
        //----------------------------------------------------------------------------------
    }

    // De-Initialization
    //--------------------------------------------------------------------------------------
    CloseWindow();
    //--------------------------------------------------------------------------------------

    return 0;
}
