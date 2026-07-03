/*******************************************************************************************
*
*   raylib-pd [audio] example - sounds and music over pd->sound
*
*   Playdate analog of examples/audio/audio_sound_loading.c + audio_music_stream.c,
*   running on the pd_raudio module (raylib audio API mapped onto the Playdate sound
*   engine): Sound = AudioSample + SamplePlayer, Music = FilePlayer (streamed mp3).
*
*   A plays a coin sound, B pauses/resumes the music, the crank bends the music pitch.
*
*   Example licensed under an unmodified zlib/libpng license, which is an OSI-certified,
*   BSD-like license that allows static linking with closed source software
*
********************************************************************************************/

#include "raylib.h"
#include "pd_raylib.h"

//------------------------------------------------------------------------------------
// Program main entry point
//------------------------------------------------------------------------------------
int main(void)
{
    // Initialization
    //--------------------------------------------------------------------------------------
    const int screenWidth = 400;
    const int screenHeight = 240;

    InitWindow(screenWidth, screenHeight, "raylib-pd [audio] example - sound and music");

    InitAudioDevice();

    Sound coin = LoadSound("resources/coin.wav");
    Sound spring = LoadSound("resources/spring.wav");
    Music music = LoadMusicStream("resources/country.mp3");

    PlayMusicStream(music);

    bool musicPaused = false;
    float musicPitch = 1.0f;
    int framesCounter = 0;

    SetTargetFPS(50);
    //--------------------------------------------------------------------------------------

    // Main game loop
    while (!WindowShouldClose())
    {
        // Update
        //----------------------------------------------------------------------------------
        UpdateMusicStream(music);       // No-op on the Playdate (streams on its own)

        framesCounter++;
        if ((framesCounter % 250) == 0) PlaySound(spring);      // Periodic, for headless runs

        if (IsKeyPressed(KEY_SPACE)) PlaySound(coin);           // A button

        if (IsKeyPressed(KEY_ENTER))                            // B button
        {
            musicPaused = !musicPaused;
            if (musicPaused) PauseMusicStream(music);
            else ResumeMusicStream(music);
        }

        float crank = GetCrankChange();
        if (crank != 0.0f)
        {
            musicPitch += crank/720.0f;
            if (musicPitch < 0.5f) musicPitch = 0.5f;
            if (musicPitch > 2.0f) musicPitch = 2.0f;
            SetMusicPitch(music, musicPitch);
        }
        //----------------------------------------------------------------------------------

        // Draw
        //----------------------------------------------------------------------------------
        BeginDrawing();

            ClearBackground(RAYWHITE);

            DrawText("raudio over pd->sound", 100, 20, 20, BLACK);

            DrawText(TextFormat("music: %s   %5.1f / %5.1f s",
                     IsMusicStreamPlaying(music)? "PLAYING" : "PAUSED",
                     GetMusicTimePlayed(music), GetMusicTimeLength(music)), 60, 70, 10, BLACK);

            // Progress bar
            DrawRectangleLines(50, 90, 300, 14, BLACK);
            float length = GetMusicTimeLength(music);
            if (length > 0.0f) DrawRectangle(50, 90, (int)(300.0f*GetMusicTimePlayed(music)/length), 14, DARKGRAY);

            DrawText(TextFormat("pitch: %.2f (crank)", musicPitch), 60, 115, 10, DARKGRAY);
            DrawText(TextFormat("coin: %s   spring: %s",
                     IsSoundPlaying(coin)? "ON" : "--",
                     IsSoundPlaying(spring)? "ON" : "--"), 60, 135, 10, DARKGRAY);

            DrawText("A: coin   B: pause/resume   crank: pitch", 70, 200, 10, GRAY);
            DrawText("(spring plays automatically every 5s)", 90, 215, 10, LIGHTGRAY);

        EndDrawing();
        //----------------------------------------------------------------------------------
    }

    // De-Initialization
    //--------------------------------------------------------------------------------------
    UnloadSound(coin);
    UnloadSound(spring);
    UnloadMusicStream(music);

    CloseAudioDevice();
    CloseWindow();
    //--------------------------------------------------------------------------------------

    return 0;
}
