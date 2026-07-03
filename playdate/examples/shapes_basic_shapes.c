/*******************************************************************************************
*
*   raylib-pd [shapes] example - basic shapes drawing
*
*   Playdate analog of examples/shapes/shapes_basic_shapes.c, laid out for 400x240.
*   Colors land on the 1-bit LCD as Bayer-dithered gray levels.
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

    InitWindow(screenWidth, screenHeight, "raylib-pd [shapes] example - basic shapes drawing");

    float rotation = 0.0f;

    SetTargetFPS(50);
    //--------------------------------------------------------------------------------------

    // Main game loop
    while (!WindowShouldClose())
    {
        // Update
        //----------------------------------------------------------------------------------
        rotation += 0.4f;
        //----------------------------------------------------------------------------------

        // Draw
        //----------------------------------------------------------------------------------
        BeginDrawing();

            ClearBackground(RAYWHITE);

            DrawText("some basic shapes available on raylib", 40, 10, 10, DARKGRAY);

            // Circle shapes and lines
            DrawCircle(screenWidth/5, 60, 25, DARKBLUE);
            DrawCircleGradient((Vector2){ screenWidth/5.0f, 130.0f }, 30, GREEN, SKYBLUE);
            DrawCircleLines(screenWidth/5, 200, 30, DARKBLUE);

            // Rectangle shapes and lines
            DrawRectangle(screenWidth/5*2 - 30, 40, 60, 50, RED);
            DrawRectangleGradientH(screenWidth/5*2 - 45, 110, 90, 30, MAROON, GOLD);
            DrawRectangleLines(screenWidth/5*2 - 20, 160, 40, 60, ORANGE);

            // Triangle shapes and lines
            DrawTriangle((Vector2){ screenWidth/5.0f*3.0f, 40.0f },
                         (Vector2){ screenWidth/5.0f*3.0f - 30.0f, 90.0f },
                         (Vector2){ screenWidth/5.0f*3.0f + 30.0f, 90.0f }, VIOLET);

            DrawTriangleLines((Vector2){ screenWidth/5.0f*3.0f, 110.0f },
                              (Vector2){ screenWidth/5.0f*3.0f - 30.0f, 170.0f },
                              (Vector2){ screenWidth/5.0f*3.0f + 30.0f, 170.0f }, DARKBLUE);

            // Polygon shapes and lines
            DrawPoly((Vector2){ (float)screenWidth/5.0f*4.0f, 70.0f }, 6, 40, rotation, BROWN);
            DrawPolyLinesEx((Vector2){ (float)screenWidth/5.0f*4.0f, 160.0f }, 6, 40, rotation, 3, BEIGE);

            DrawLine(18, 22, screenWidth - 18, 22, BLACK);

        EndDrawing();
        //----------------------------------------------------------------------------------
    }

    // De-Initialization
    //--------------------------------------------------------------------------------------
    CloseWindow();
    //--------------------------------------------------------------------------------------

    return 0;
}
