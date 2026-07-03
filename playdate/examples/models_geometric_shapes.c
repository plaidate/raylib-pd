/*******************************************************************************************
*
*   raylib-pd [models] example - geometric shapes in 3D
*
*   Playdate analog of examples/models/models_geometric_shapes.c: a camera orbits a
*   set of 3D shapes, exercising the rlsw depth buffer and perspective projection.
*   The crank rotates the camera; it auto-orbits while the crank is docked.
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

    InitWindow(screenWidth, screenHeight, "raylib-pd [models] example - geometric shapes");

    Camera camera = { 0 };
    camera.position = (Vector3){ 8.0f, 6.0f, 8.0f };
    camera.target = (Vector3){ 0.0f, 0.5f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    float orbitAngle = 45.0f;   // Degrees around the Y axis

    SetTargetFPS(50);
    //--------------------------------------------------------------------------------------

    // Main game loop
    while (!WindowShouldClose())
    {
        // Update
        //----------------------------------------------------------------------------------
        if (IsCrankDocked()) orbitAngle += 0.5f;            // Auto-orbit
        else orbitAngle += GetCrankChange();                // Crank steers the camera

        float rad = orbitAngle*DEG2RAD;
        camera.position = (Vector3){ sinf(rad)*11.0f, 6.0f, cosf(rad)*11.0f };
        //----------------------------------------------------------------------------------

        // Draw
        //----------------------------------------------------------------------------------
        BeginDrawing();

            ClearBackground(RAYWHITE);

            BeginMode3D(camera);

                DrawCube((Vector3){ -3.0f, 0.5f, 0.0f }, 1.0f, 1.0f, 1.0f, RED);
                DrawCubeWires((Vector3){ -3.0f, 0.5f, 0.0f }, 1.0f, 1.0f, 1.0f, BLACK);

                DrawSphere((Vector3){ 0.0f, 1.0f, 0.0f }, 1.0f, DARKBLUE);
                DrawSphereWires((Vector3){ 0.0f, 1.0f, 0.0f }, 1.0f, 8, 8, BLACK);

                DrawCylinder((Vector3){ 3.0f, 0.0f, 0.0f }, 0.7f, 0.7f, 2.0f, 12, MAROON);
                DrawCylinderWires((Vector3){ 3.0f, 0.0f, 0.0f }, 0.7f, 0.7f, 2.0f, 12, BLACK);

                DrawGrid(10, 1.0f);

            EndMode3D();

            DrawText("crank orbits the camera (auto when docked)", 20, 10, 10, DARKGRAY);
            DrawFPS(330, 220);

        EndDrawing();
        //----------------------------------------------------------------------------------
    }

    // De-Initialization
    //--------------------------------------------------------------------------------------
    CloseWindow();
    //--------------------------------------------------------------------------------------

    return 0;
}
