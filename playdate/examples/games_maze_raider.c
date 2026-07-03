/*******************************************************************************************
*
*   Maze Raider — raylib-pd showcase game
*
*   First-person 3D maze crawler for the Playdate: procedurally generated mazes
*   (recursive backtracker), textured walls via GenMeshCubicmap + the rlsw
*   software renderer, billboard coin pickups, and an exit portal that unlocks
*   once every coin is collected. Levels grow as you clear them.
*
*   Controls:  crank / d-pad left-right ... turn
*              d-pad up-down .............. walk
*              A .......................... start / next level
*              B .......................... reshuffle current maze
*
*   Built on the raylib [models] first-person maze example,
*   Copyright (c) 2019-2025 Ramon Santamaria (@raysan5), zlib/libpng license.
*
********************************************************************************************/

#include "raylib.h"
#include "pd_raylib.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#if defined(MR_DEBUG)
#include "pd_api.h"
#endif

#define MAZE_MAX_W  23
#define MAZE_MAX_H  17
#define MAX_COINS   8

typedef enum { STATE_TITLE, STATE_PLAY, STATE_CLEAR } GameState;

typedef struct { int x, y; bool taken; } Coin;

// Level state (rebuilt by LoadLevel)
static unsigned char grid[MAZE_MAX_H][MAZE_MAX_W];      // 1 = wall
static int mazeW = 0, mazeH = 0;
static Model model;
static Texture2D minimap;
static Color *mapPixels = NULL;
static bool levelLoaded = false;
static Coin coins[MAX_COINS];
static int numCoins = 0, coinsTaken = 0;
static int exitX = 1, exitY = 1;

// Maze walls sit on integer world coordinates: cell (x,y) spans
// [x-0.5..x+0.5] x [y-0.5..y+0.5] in world XZ (mapPosition = origin)
static const Vector3 mapPosition = { 0.0f, 0.0f, 0.0f };

// Carve a perfect maze with an iterative recursive backtracker; the exit is
// the deepest dead end reached from the start cell (1,1)
static void GenMaze(int w, int h)
{
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) grid[y][x] = 1;

    static struct { unsigned char x, y; } stack[(MAZE_MAX_W/2 + 1)*(MAZE_MAX_H/2 + 1)];
    const int dirs[4][2] = { { 2, 0 }, { -2, 0 }, { 0, 2 }, { 0, -2 } };
    int top = 0, maxDepth = 0;

    grid[1][1] = 0;
    stack[top].x = 1; stack[top].y = 1; top++;
    exitX = 1; exitY = 1;

    while (top > 0)
    {
        int cx = stack[top - 1].x, cy = stack[top - 1].y;
        int cand[4], n = 0;
        for (int i = 0; i < 4; i++)
        {
            int nx = cx + dirs[i][0], ny = cy + dirs[i][1];
            if ((nx > 0) && (nx < w - 1) && (ny > 0) && (ny < h - 1) && grid[ny][nx]) cand[n++] = i;
        }
        if (n == 0) { top--; continue; }

        int d = cand[GetRandomValue(0, n - 1)];
        int nx = cx + dirs[d][0], ny = cy + dirs[d][1];
        grid[cy + dirs[d][1]/2][cx + dirs[d][0]/2] = 0;
        grid[ny][nx] = 0;
        stack[top].x = (unsigned char)nx; stack[top].y = (unsigned char)ny; top++;
        if (top > maxDepth) { maxDepth = top; exitX = nx; exitY = ny; }
    }
}

static void UnloadLevel(void)
{
    if (!levelLoaded) return;
    UnloadModel(model);             // atlas texture is shared, not owned by the model
    UnloadTexture(minimap);
    UnloadImageColors(mapPixels);
    levelLoaded = false;
}

static void LoadLevel(int level, Texture2D atlas, Camera *camera)
{
    UnloadLevel();

    mazeW = 13 + 2*(level - 1); if (mazeW > MAZE_MAX_W) mazeW = MAZE_MAX_W;
    mazeH = 9 + 2*(level - 1);  if (mazeH > MAZE_MAX_H) mazeH = MAZE_MAX_H;

    SetRandomSeed((unsigned int)(GetTime()*1000.0) + (unsigned int)level*7919u);
    GenMaze(mazeW, mazeH);

    Image imMap = GenImageColor(mazeW, mazeH, BLACK);
    for (int y = 0; y < mazeH; y++)
        for (int x = 0; x < mazeW; x++)
            if (grid[y][x]) ImageDrawPixel(&imMap, x, y, WHITE);

    Mesh mesh = GenMeshCubicmap(imMap, (Vector3){ 1.0f, 1.0f, 1.0f });
    model = LoadModelFromMesh(mesh);
    model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = atlas;

    minimap = LoadTextureFromImage(imMap);
    mapPixels = LoadImageColors(imMap);
    UnloadImage(imMap);

    // Coins on floor cells away from the start and off the exit
    numCoins = 3 + level; if (numCoins > MAX_COINS) numCoins = MAX_COINS;
    coinsTaken = 0;
    for (int c = 0; c < numCoins; c++)
    {
        int tries = 200;
        while (tries--)
        {
            int x = GetRandomValue(1, mazeW - 2), y = GetRandomValue(1, mazeH - 2);
            if (grid[y][x]) continue;
            if ((x + y) < 8) continue;                       // not right at the start
            if ((x == exitX) && (y == exitY)) continue;
            bool dup = false;
            for (int i = 0; i < c; i++) if ((coins[i].x == x) && (coins[i].y == y)) dup = true;
            if (dup) continue;
            coins[c].x = x; coins[c].y = y; coins[c].taken = false;
            break;
        }
        if (tries < 0) { numCoins = c; break; }              // tiny maze: fewer coins
    }

    // Player starts in cell (1,1) facing down the open corridor
    camera->position = (Vector3){ 1.0f, 0.4f, 1.0f };
    float dx = (!grid[1][2])? 1.0f : 0.0f;
    float dz = (dx == 0.0f)? 1.0f : 0.0f;
    camera->target = (Vector3){ 1.0f + dx, 0.4f, 1.0f + dz };
    camera->up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera->fovy = 60.0f;
    camera->projection = CAMERA_PERSPECTIVE;

    levelLoaded = true;
}

// Coin pickup sprite: bullseye reads crisply after the 1-bit dither
static Texture2D GenCoinTexture(void)
{
    Image img = GenImageColor(32, 32, BLANK);
    ImageDrawCircle(&img, 16, 16, 14, BLACK);
    ImageDrawCircle(&img, 16, 16, 11, WHITE);
    ImageDrawCircle(&img, 16, 16, 5, BLACK);
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
}

int main(void)
{
    const int screenWidth = 400;
    const int screenHeight = 240;

    InitWindow(screenWidth, screenHeight, "Maze Raider");
    InitAudioDevice();

    Texture2D atlas = LoadTexture("resources/cubicmap_atlas.png");
    Texture2D coinTex = GenCoinTexture();
    Sound sndCoin = LoadSound("resources/coin.wav");
    Sound sndClear = LoadSound("resources/spring.wav");

    Camera camera = { 0 };
    int level = 1;
    GameState state = STATE_TITLE;
    float playTime = 0.0f, bobPhase = 0.0f, prevBob = 0.0f, animT = 0.0f;

    LoadLevel(level, atlas, &camera);       // title screen shows a live maze

    SetTargetFPS(50);

    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();
        animT += dt;

#if defined(MR_DEBUG)
        // Temporary timing probe: headless-readable heartbeat in the Data dir
        {
            static int dbgFrames = 0;
            dbgFrames++;
            if ((dbgFrames % 100) == 0)
            {
                PlaydateAPI *pd = GetPlaydateAPI();
                SDFile *f = pd->file->open("mr_debug.txt", kFileWrite);
                if (f)
                {
                    char buf[320];
                    int n = snprintf(buf, sizeof(buf),
                        "frames=%d time=%.3f dt=%.6f playTime=%.3f animT=%.3f state=%d "
                        "pos=(%.3f,%.3f,%.3f) tgt=(%.3f,%.3f,%.3f) crank=%.3f coins=%d level=%d\n",
                        dbgFrames, GetTime(), (double)dt, (double)playTime, (double)animT, (int)state,
                        (double)camera.position.x, (double)camera.position.y, (double)camera.position.z,
                        (double)camera.target.x, (double)camera.target.y, (double)camera.target.z,
                        (double)GetCrankChange(), coinsTaken, level);
                    pd->file->write(f, buf, (unsigned int)n);
                    pd->file->close(f);
                }
            }
        }
#endif

        // Update
        //----------------------------------------------------------------------------------
        if (state == STATE_TITLE)
        {
            UpdateCameraPro(&camera, (Vector3){ 0 }, (Vector3){ 20.0f*dt, 0.0f, 0.0f }, 0.0f);
            // animT gate: the simulator can replay the launcher's A press into
            // the first frames — ignore A until the title has actually shown
#if defined(MR_DEBUG)
            if ((IsKeyPressed(KEY_SPACE) && (animT > 0.75f)) || (animT > 3.0f))  // autopilot: self-start
#else
            if (IsKeyPressed(KEY_SPACE) && (animT > 0.75f))     // A button
#endif
            {
                LoadLevel(level, atlas, &camera);
                playTime = 0.0f;
                state = STATE_PLAY;
            }
        }
        else if (state == STATE_PLAY)
        {
            playTime += dt;

            Vector3 oldCamPos = camera.position;

            float yaw = GetCrankChange()*0.5f
                      + (IsKeyDown(KEY_RIGHT)? 100.0f*dt : 0.0f)
                      - (IsKeyDown(KEY_LEFT)? 100.0f*dt : 0.0f);
            float fwd = (IsKeyDown(KEY_UP)? 2.5f*dt : 0.0f) - (IsKeyDown(KEY_DOWN)? 2.5f*dt : 0.0f);
#if defined(MR_DEBUG)
            // Autopilot: wander the maze — walk forward, sweep the heading;
            // every few seconds warp to the next objective so the full
            // coin -> exit -> next-level loop gets exercised headlessly
            fwd = 2.0f*dt;
            yaw = 35.0f*dt*sinf(0.35f*animT);
            {
                static float warpT = 0.0f;
                warpT += dt;
                if (warpT > 6.0f)
                {
                    warpT = 0.0f;
                    int wx = exitX, wy = exitY;
                    for (int c = 0; c < numCoins; c++)
                        if (!coins[c].taken) { wx = coins[c].x; wy = coins[c].y; break; }
                    float ddx = (float)wx - camera.position.x;
                    float ddz = (float)wy - camera.position.z;
                    camera.position.x += ddx; camera.position.z += ddz;
                    camera.target.x += ddx; camera.target.z += ddz;
                }
            }
#endif
            UpdateCameraPro(&camera, (Vector3){ fwd, 0.0f, 0.0f }, (Vector3){ yaw, 0.0f, 0.0f }, 0.0f);

            // Head bob while walking (applied as a delta so the camera vector is preserved)
            if (fwd != 0.0f) bobPhase += 10.0f*dt;
            float bob = 0.02f*sinf(bobPhase);
            camera.position.y += bob - prevBob;
            camera.target.y += bob - prevBob;
            prevBob = bob;

            // 2D collision against wall cells around the player (from the maze example)
            Vector2 playerPos = { camera.position.x, camera.position.z };
            float playerRadius = 0.2f;
            int playerCellX = (int)(playerPos.x - mapPosition.x + 0.5f);
            int playerCellY = (int)(playerPos.y - mapPosition.z + 0.5f);
            if (playerCellX < 0) playerCellX = 0; else if (playerCellX >= mazeW) playerCellX = mazeW - 1;
            if (playerCellY < 0) playerCellY = 0; else if (playerCellY >= mazeH) playerCellY = mazeH - 1;

            bool hitWall = false;
            for (int y = playerCellY - 1; y <= playerCellY + 1; y++)
            {
                if ((y < 0) || (y >= mazeH)) continue;
                for (int x = playerCellX - 1; x <= playerCellX + 1; x++)
                {
                    if ((x >= 0) && (x < mazeW) && (mapPixels[y*mazeW + x].r == 255) &&
                        CheckCollisionCircleRec(playerPos, playerRadius,
                            (Rectangle){ mapPosition.x - 0.5f + x*1.0f, mapPosition.z - 0.5f + y*1.0f, 1.0f, 1.0f }))
                    {
                        hitWall = true;
                    }
                }
            }
            if (hitWall)
            {
                // Undo the move on the target too, or the forward vector
                // stretches a little every colliding frame and eventually
                // degenerates the view matrix
                camera.target.x -= (camera.position.x - oldCamPos.x);
                camera.target.z -= (camera.position.z - oldCamPos.z);
                camera.position.x = oldCamPos.x;
                camera.position.z = oldCamPos.z;
            }

            // Coin pickups
            for (int c = 0; c < numCoins; c++)
            {
                if (coins[c].taken) continue;
                float dx = camera.position.x - (float)coins[c].x;
                float dz = camera.position.z - (float)coins[c].y;
                if ((dx*dx + dz*dz) < 0.16f)
                {
                    coins[c].taken = true;
                    coinsTaken++;
                    PlaySound(sndCoin);
                }
            }

            // Exit portal (only once every coin is collected)
            if ((coinsTaken == numCoins) && (playerCellX == exitX) && (playerCellY == exitY))
            {
                PlaySound(sndClear);
                state = STATE_CLEAR;
            }

            if (IsKeyPressed(KEY_ENTER))    // B button: reshuffle this level
            {
                LoadLevel(level, atlas, &camera);
                playTime = 0.0f;
            }
        }
        else // STATE_CLEAR
        {
            static float clearT = 0.0f;
            clearT += dt;
#if defined(MR_DEBUG)
            if (IsKeyPressed(KEY_SPACE) || (clearT > 2.0f))  // autopilot: self-advance
#else
            if (IsKeyPressed(KEY_SPACE))    // A button: next level
#endif
            {
                clearT = 0.0f;
                level++;
                LoadLevel(level, atlas, &camera);
                playTime = 0.0f;
                state = STATE_PLAY;
            }
        }
        //----------------------------------------------------------------------------------

        // Draw
        //----------------------------------------------------------------------------------
        BeginDrawing();

            ClearBackground(RAYWHITE);

            BeginMode3D(camera);
                DrawModel(model, mapPosition, 1.0f, WHITE);

                for (int c = 0; c < numCoins; c++)
                {
                    if (coins[c].taken) continue;
                    float y = 0.4f + 0.08f*sinf(4.0f*animT + (float)c*1.3f);
                    DrawBillboard(camera, coinTex, (Vector3){ (float)coins[c].x, y, (float)coins[c].y }, 0.4f, WHITE);
                }

                // Exit portal: wireframe while locked, pulsing solid pillar when open
                Vector3 exitPos = { (float)exitX, 0.5f, (float)exitY };
                if (coinsTaken == numCoins)
                {
                    float pulse = 0.55f + 0.1f*sinf(6.0f*animT);
                    DrawCube(exitPos, pulse, 1.0f, pulse, BLACK);
                }
                else DrawCubeWires(exitPos, 0.55f, 1.0f, 0.55f, DARKGRAY);
            EndMode3D();

            if (state == STATE_TITLE)
            {
                DrawRectangle(50, 50, 300, 140, RAYWHITE);
                DrawRectangleLines(50, 50, 300, 140, BLACK);
                DrawText("MAZE RAIDER", 200 - MeasureText("MAZE RAIDER", 30)/2, 66, 30, BLACK);
                DrawText("collect every coin, find the exit", 200 - MeasureText("collect every coin, find the exit", 10)/2, 106, 10, DARKGRAY);
                DrawText("crank = turn   d-pad = walk", 200 - MeasureText("crank = turn   d-pad = walk", 10)/2, 124, 10, DARKGRAY);
                if (((int)(animT*2.0f))%2 == 0)
                    DrawText("press A to start", 200 - MeasureText("press A to start", 20)/2, 152, 20, BLACK);
            }
            else
            {
                // Minimap (top right): walls white, floor black
                int scale = (mazeW > 16)? 3 : 4;
                int mapX = screenWidth - mazeW*scale - 8, mapY = 8;
                DrawTextureEx(minimap, (Vector2){ (float)mapX, (float)mapY }, 0.0f, (float)scale, WHITE);
                DrawRectangleLines(mapX - 1, mapY - 1, mazeW*scale + 2, mazeH*scale + 2, BLACK);
                for (int c = 0; c < numCoins; c++)
                    if (!coins[c].taken)
                        DrawRectangle(mapX + coins[c].x*scale + scale/2 - 1, mapY + coins[c].y*scale + scale/2 - 1, 2, 2, WHITE);
                if ((coinsTaken < numCoins) || (((int)(animT*4.0f))%2 == 0))
                    DrawRectangle(mapX + exitX*scale, mapY + exitY*scale, scale, scale, LIGHTGRAY);
                int pcx = (int)(camera.position.x + 0.5f), pcy = (int)(camera.position.z + 0.5f);
                DrawRectangle(mapX + pcx*scale, mapY + pcy*scale, scale, scale, GRAY);

                // Status chip (top left)
                const char *hud = TextFormat("LV %d   COINS %d/%d   %02d:%02d", level,
                    coinsTaken, numCoins, (int)playTime/60, (int)playTime%60);
                DrawRectangle(4, 4, MeasureText(hud, 10) + 8, 16, RAYWHITE);
                DrawRectangleLines(4, 4, MeasureText(hud, 10) + 8, 16, BLACK);
                DrawText(hud, 8, 7, 10, BLACK);

                if ((coinsTaken == numCoins) && (state == STATE_PLAY))
                    DrawText("exit is open!", 8, 24, 10, BLACK);
            }

            if (state == STATE_CLEAR)
            {
                DrawRectangle(70, 70, 260, 100, RAYWHITE);
                DrawRectangleLines(70, 70, 260, 100, BLACK);
                const char *msg = TextFormat("LEVEL %d CLEAR!", level);
                DrawText(msg, 200 - MeasureText(msg, 20)/2, 84, 20, BLACK);
                const char *tm = TextFormat("time %02d:%02d", (int)playTime/60, (int)playTime%60);
                DrawText(tm, 200 - MeasureText(tm, 10)/2, 112, 10, DARKGRAY);
                if (((int)(animT*2.0f))%2 == 0)
                    DrawText("press A for next level", 200 - MeasureText("press A for next level", 10)/2, 140, 10, BLACK);
            }

            DrawFPS(4, screenHeight - 20);

        EndDrawing();
        //----------------------------------------------------------------------------------
    }

    UnloadLevel();
    UnloadTexture(atlas);
    UnloadTexture(coinTex);
    UnloadSound(sndCoin);
    UnloadSound(sndClear);
    CloseAudioDevice();
    CloseWindow();

    return 0;
}
