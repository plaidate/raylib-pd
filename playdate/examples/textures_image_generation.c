/*******************************************************************************************
*
*   raylib-pd [textures] example - procedural image generation
*
*   Playdate analog of examples/textures/textures_image_generation.c: generates images
*   procedurally (no file I/O needed) and draws them as textures through rlsw.
*
*   Example licensed under an unmodified zlib/libpng license, which is an OSI-certified,
*   BSD-like license that allows static linking with closed source software
*
********************************************************************************************/

#include "raylib.h"

#define NUM_TEXTURES  6

//------------------------------------------------------------------------------------
// Program main entry point
//------------------------------------------------------------------------------------
int main(void)
{
    // Initialization
    //--------------------------------------------------------------------------------------
    const int screenWidth = 400;
    const int screenHeight = 240;

    InitWindow(screenWidth, screenHeight, "raylib-pd [textures] example - image generation");

    Image verGradient = GenImageGradientLinear(110, 90, 0, WHITE, BLACK);
    Image horGradient = GenImageGradientLinear(110, 90, 90, WHITE, BLACK);
    Image radGradient = GenImageGradientRadial(110, 90, 0.0f, WHITE, BLACK);
    Image checked = GenImageChecked(110, 90, 16, 16, WHITE, BLACK);
    Image checkedSmall = GenImageChecked(110, 90, 4, 4, WHITE, BLACK);
    Image cellular = GenImageCellular(110, 90, 24);

    Texture2D textures[NUM_TEXTURES] = { 0 };

    textures[0] = LoadTextureFromImage(verGradient);
    textures[1] = LoadTextureFromImage(horGradient);
    textures[2] = LoadTextureFromImage(radGradient);
    textures[3] = LoadTextureFromImage(checked);
    textures[4] = LoadTextureFromImage(checkedSmall);
    textures[5] = LoadTextureFromImage(cellular);

    // Unload image data (CPU RAM)
    UnloadImage(verGradient);
    UnloadImage(horGradient);
    UnloadImage(radGradient);
    UnloadImage(checked);
    UnloadImage(checkedSmall);
    UnloadImage(cellular);

    const char *names[NUM_TEXTURES] = {
        "V GRADIENT", "H GRADIENT", "RADIAL", "CHECKED 16", "CHECKED 4", "CELLULAR",
    };

    SetTargetFPS(50);
    //--------------------------------------------------------------------------------------

    // Main game loop
    while (!WindowShouldClose())
    {
        // Draw
        //----------------------------------------------------------------------------------
        BeginDrawing();

            ClearBackground(RAYWHITE);

            for (int i = 0; i < NUM_TEXTURES; i++)
            {
                int x = 15 + (i%3)*130;
                int y = 15 + (i/3)*110;

                DrawTexture(textures[i], x, y, WHITE);
                DrawRectangleLines(x, y, 110, 90, BLACK);
                DrawText(names[i], x + 5, y + 94, 10, DARKGRAY);
            }

        EndDrawing();
        //----------------------------------------------------------------------------------
    }

    // De-Initialization
    //--------------------------------------------------------------------------------------
    for (int i = 0; i < NUM_TEXTURES; i++) UnloadTexture(textures[i]);

    CloseWindow();
    //--------------------------------------------------------------------------------------

    return 0;
}
