/**********************************************************************************************
*
*   rcore_playdate - Functions to manage window, graphics device and inputs
*
*   PLATFORM: PLAYDATE (Panic Playdate handheld)
*       - Playdate Simulator (macOS/Linux/Windows host process, pdex dynamic library):
*         the game context is a pthread
*       - Device (ARM Cortex-M7, TARGET_PLAYDATE): the game context is a coroutine on
*         a static stack with a small assembly context switch
*
*   ARCHITECTURE:
*       The Playdate OS owns the main loop: it calls an update callback at the display
*       refresh rate and expects it to return. raylib programs own their loop instead
*       (while (!WindowShouldClose())), so the raylib program runs on its own "green"
*       thread and control strictly alternates with the Playdate update callback:
*
*         - eventHandler(kEventInit) registers the update callback (host side)
*         - the first update callback starts the game thread, which runs the example's
*           main() (renamed to rl_playdate_main via -Dmain=rl_playdate_main)
*         - each update callback snapshots input, hands the game thread one time slice,
*           and waits (bounded) for it to yield
*         - the game thread yields inside SwapScreenBuffer(), i.e. once per EndDrawing(),
*           after staging a dithered 1-bit frame the host blits to the LCD
*
*       Rendering uses the rlsw software renderer (GRAPHICS_API_OPENGL_SOFTWARE) with a
*       16-bit RGB565 framebuffer (as in the ESP32 raylib port); SwapScreenBuffer()
*       reads rlsw's internal buffer directly (swGetColorBuffer, no conversion copy)
*       and converts it to the Playdate's 1-bit display with a 4x4 ordered (Bayer)
*       dither on luminance.
*
*   LIMITATIONS:
*       - Software renderer (rlsw) only
*       - Screen is fixed at 400x240 (InitWindow sizes are clamped)
*       - No audio through raylib (use pd->sound via GetPlaydateAPI())
*       - Simulator only for now (game thread uses pthreads; device build needs a
*         bare-metal context switch and pd->file-backed stdio)
*
*   EXTENSIONS (declared in playdate/pd_raylib.h):
*       - GetCrankAngle()/GetCrankChange()/IsCrankDocked() for the crank
*       - GetPlaydateAPI() exposes the PlaydateAPI pointer (host-thread caveats apply)
*
*   INPUT MAPPING:
*       d-pad          -> KEY_UP/KEY_DOWN/KEY_LEFT/KEY_RIGHT and gamepad 0 left-face buttons
*       A button       -> KEY_SPACE and GAMEPAD_BUTTON_RIGHT_FACE_DOWN
*       B button       -> KEY_ENTER and GAMEPAD_BUTTON_RIGHT_FACE_RIGHT
*       crank change   -> mouse wheel Y (in full-turn units) and the crank extension API
*
*   CONFIGURATION:
*       #define PD_FRAMEDUMP
*           Periodically write the staged 1-bit frame (frame.bin) and a status heartbeat
*           (pd_status.json) to the game's Data directory for headless verification
*
*   DEPENDENCIES:
*       - Playdate SDK C API (pd_api.h)
*       - rlsw: Software renderer
*
*   LICENSE: zlib/libpng
*
*   Copyright (c) 2026 Ramon Santamaria (@raysan5) and contributors
*
*   This software is provided "as-is", without any express or implied warranty. In no event
*   will the authors be held liable for any damages arising from the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose, including commercial
*   applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not claim that you
*     wrote the original software. If you use this software in a product, an acknowledgment
*     in the product documentation would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not be misrepresented
*     as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
**********************************************************************************************/

#include "pd_api.h"

#if defined(TARGET_SIMULATOR)
    #include <pthread.h>
    #include <dlfcn.h>      // dladdr(): locate pdex.dylib -> chdir into the pdx
    #include <unistd.h>     // chdir()
#elif defined(TARGET_PLAYDATE)
    // Bare metal (Cortex-M7): the game runs as a coroutine on its own static
    // stack, switched with a small assembly context switch (see CoroSwitch)
#else
    #error "PLATFORM_PLAYDATE: build with TARGET_SIMULATOR or TARGET_PLAYDATE"
#endif

//----------------------------------------------------------------------------------
// Types and Structures Definition
//----------------------------------------------------------------------------------
// Input snapshot written by the host (update callback) and consumed by the game
// thread in PollInputEvents(); pushed/released/crankDelta accumulate between polls
// so nothing is lost if the game frame spans several host frames
typedef struct {
    PDButtons current;      // Buttons held right now
    PDButtons pushed;       // Accumulated press edges since last poll
    PDButtons released;     // Accumulated release edges since last poll
    float crankDelta;       // Accumulated crank movement since last poll (degrees)
    float crankAngle;       // Current crank angle (degrees)
    bool crankDocked;       // Current crank docked state
} PlaydateInput;

typedef struct {
    PlaydateAPI *pd;                                // Playdate API (valid after kEventInit)
    unsigned char bitFrame[LCD_ROWS*LCD_ROWSIZE];   // Dithered 1-bit frame staged for the host blit

    volatile PlaydateInput input;                   // Host-written input snapshot
    float frameCrankChange;                         // Crank change consumed for the current game frame (degrees)
    float frameCrankAngle;                          // Crank angle at the last poll (degrees)
    bool frameCrankDocked;                          // Crank docked state at the last poll

    volatile float requestedRefreshRate;            // SetTargetFPS() forwarded to pd->display (game writes, host applies)
    int cursorVisibleFrames;                        // Crosshair overlay countdown after virtual-mouse movement
} PlatformData;

//----------------------------------------------------------------------------------
// Global Variables Definition
//----------------------------------------------------------------------------------
extern CoreData CORE;                   // Global CORE state context

static PlatformData platform = { 0 };   // Platform specific data

static volatile unsigned int statHostFrames = 0;    // Update callbacks run
static volatile unsigned int statGameSlices = 0;    // Slices handed to the game thread
static volatile unsigned int statFramesShown = 0;   // Frames blitted to the LCD

#if defined(PD_RAYLIB_LUA)
// Lua mode: the Playdate Lua runtime owns the update loop and calls the bound
// raylib functions synchronously from playdate.update(), so there is no game
// thread and SwapScreenBuffer() blits directly. Bindings live in raylib_lua.c
void PdRaylibLuaRegister(PlaydateAPI *pd);
#else
// Green-thread handshake: control strictly alternates between the Playdate update
// callback (host) and the game context; only the current turn's side runs.
// Simulator: the game context is a pthread synchronized with a condvar.
// Device: the game context is a coroutine on a static stack (single hardware
// thread, so pd API calls are safe from both sides).
static bool gameThreadStarted = false;
static volatile bool gameExited = false;
static volatile bool frameDirty = false;

#if defined(TARGET_SIMULATOR)
enum { TURN_HOST, TURN_GAME };

static pthread_mutex_t gameLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gameCond = PTHREAD_COND_INITIALIZER;
static int gameTurn = TURN_HOST;
#elif defined(TARGET_PLAYDATE)
#define GAME_STACK_SIZE (128*1024)
static unsigned char gameStack[GAME_STACK_SIZE] __attribute__((aligned(8)));
static void *gameSP = NULL;     // Saved game-context stack pointer
static void *hostSP = NULL;     // Saved host-context stack pointer
#endif

int rl_playdate_main(void);             // Example's main(), renamed via -Dmain=rl_playdate_main
#endif // PD_RAYLIB_LUA

//----------------------------------------------------------------------------------
// Module Internal Functions Declaration
//----------------------------------------------------------------------------------
int InitPlatform(void);                 // Initialize platform (graphics, inputs and more)
static void StageFrame(void);           // Dither the rlsw framebuffer into bitFrame
#if !defined(PD_RAYLIB_LUA)
static void GameYield(void);            // Hand control back to the host until the next slice
#endif
#if defined(PD_FRAMEDUMP)
static void WriteFrameDump(void);       // Dump the staged frame + heartbeat for headless verification
#endif

//----------------------------------------------------------------------------------
// Module Functions Declaration
//----------------------------------------------------------------------------------
// NOTE: Functions declaration is provided by raylib.h

//----------------------------------------------------------------------------------
// Module Functions Definition: Window and Graphics Device
//----------------------------------------------------------------------------------

// Check if application should close
bool WindowShouldClose(void)
{
    if (CORE.Window.ready) return CORE.Window.shouldClose;
    else return true;
}

// Toggle fullscreen mode
void ToggleFullscreen(void)
{
    TRACELOG(LOG_WARNING, "ToggleFullscreen() not available on target platform");
}

// Toggle borderless windowed mode
void ToggleBorderlessWindowed(void)
{
    TRACELOG(LOG_WARNING, "ToggleBorderlessWindowed() not available on target platform");
}

// Set window state: maximized, if resizable
void MaximizeWindow(void)
{
    TRACELOG(LOG_WARNING, "MaximizeWindow() not available on target platform");
}

// Set window state: minimized
void MinimizeWindow(void)
{
    TRACELOG(LOG_WARNING, "MinimizeWindow() not available on target platform");
}

// Restore window from being minimized/maximized
void RestoreWindow(void)
{
    TRACELOG(LOG_WARNING, "RestoreWindow() not available on target platform");
}

// Set window configuration state using flags
void SetWindowState(unsigned int flags)
{
    TRACELOG(LOG_WARNING, "SetWindowState() not available on target platform");
}

// Clear window configuration state flags
void ClearWindowState(unsigned int flags)
{
    TRACELOG(LOG_WARNING, "ClearWindowState() not available on target platform");
}

// Set icon for window
void SetWindowIcon(Image image)
{
    TRACELOG(LOG_WARNING, "SetWindowIcon() not available on target platform");
}

// Set icon for window
void SetWindowIcons(Image *images, int count)
{
    TRACELOG(LOG_WARNING, "SetWindowIcons() not available on target platform");
}

// Set title for window
void SetWindowTitle(const char *title)
{
    CORE.Window.title = title;
}

// Set window position on screen (windowed mode)
void SetWindowPosition(int x, int y)
{
    TRACELOG(LOG_WARNING, "SetWindowPosition() not available on target platform");
}

// Set monitor for the current window
void SetWindowMonitor(int monitor)
{
    TRACELOG(LOG_WARNING, "SetWindowMonitor() not available on target platform");
}

// Set window minimum dimensions (FLAG_WINDOW_RESIZABLE)
void SetWindowMinSize(int width, int height)
{
    CORE.Window.screenMin.width = width;
    CORE.Window.screenMin.height = height;
}

// Set window maximum dimensions (FLAG_WINDOW_RESIZABLE)
void SetWindowMaxSize(int width, int height)
{
    CORE.Window.screenMax.width = width;
    CORE.Window.screenMax.height = height;
}

// Set window dimensions
void SetWindowSize(int width, int height)
{
    TRACELOG(LOG_WARNING, "SetWindowSize() not available on target platform");
}

// Set window opacity, value opacity is between 0.0 and 1.0
void SetWindowOpacity(float opacity)
{
    TRACELOG(LOG_WARNING, "SetWindowOpacity() not available on target platform");
}

// Set window focused
void SetWindowFocused(void)
{
    TRACELOG(LOG_WARNING, "SetWindowFocused() not available on target platform");
}

// Get native window handle
void *GetWindowHandle(void)
{
    // The closest thing to a native handle: the PlaydateAPI pointer
    return (void *)platform.pd;
}

// Get number of monitors
int GetMonitorCount(void)
{
    return 1;
}

// Get current monitor where window is placed
int GetCurrentMonitor(void)
{
    return 0;
}

// Get selected monitor position
Vector2 GetMonitorPosition(int monitor)
{
    return (Vector2){ 0, 0 };
}

// Get selected monitor width (currently used by monitor)
int GetMonitorWidth(int monitor)
{
    return LCD_COLUMNS;
}

// Get selected monitor height (currently used by monitor)
int GetMonitorHeight(int monitor)
{
    return LCD_ROWS;
}

// Get selected monitor physical width in millimetres
int GetMonitorPhysicalWidth(int monitor)
{
    return 61;      // Playdate LCD active area, approx.
}

// Get selected monitor physical height in millimetres
int GetMonitorPhysicalHeight(int monitor)
{
    return 37;      // Playdate LCD active area, approx.
}

// Get selected monitor refresh rate
int GetMonitorRefreshRate(int monitor)
{
    return 50;      // Playdate maximum refresh rate
}

// Get the human-readable, UTF-8 encoded name of the selected monitor
const char *GetMonitorName(int monitor)
{
    return "Playdate LCD";
}

// Get window position XY on monitor
Vector2 GetWindowPosition(void)
{
    return (Vector2){ 0, 0 };
}

// Get window scale DPI factor for current monitor
Vector2 GetWindowScaleDPI(void)
{
    return (Vector2){ 1.0f, 1.0f };
}

// Set clipboard text content
void SetClipboardText(const char *text)
{
    TRACELOG(LOG_WARNING, "SetClipboardText() not implemented on target platform");
}

// Get clipboard text content
const char *GetClipboardText(void)
{
    TRACELOG(LOG_WARNING, "GetClipboardText() not implemented on target platform");
    return NULL;
}

// Get clipboard image
Image GetClipboardImage(void)
{
    Image image = { 0 };

    TRACELOG(LOG_WARNING, "GetClipboardImage() not implemented on target platform");

    return image;
}

// Show mouse cursor
void ShowCursor(void)
{
    CORE.Input.Mouse.cursorHidden = false;
}

// Hide mouse cursor
void HideCursor(void)
{
    CORE.Input.Mouse.cursorHidden = true;
}

// Enable cursor (unlock cursor)
void EnableCursor(void)
{
    SetMousePosition(CORE.Window.screen.width/2, CORE.Window.screen.height/2);

    CORE.Input.Mouse.cursorHidden = false;
}

// Disable cursor (lock cursor)
void DisableCursor(void)
{
    SetMousePosition(CORE.Window.screen.width/2, CORE.Window.screen.height/2);

    CORE.Input.Mouse.cursorHidden = true;
}

// Swap back buffer with front buffer (screen drawing)
// NOTE: Runs on the game thread; stages a dithered frame for the host blit,
// then yields until the host hands over the next time slice
void SwapScreenBuffer(void)
{
    StageFrame();

#if defined(PD_RAYLIB_LUA)
    // Lua mode runs inside playdate.update(): blit directly, no thread handoff
    PlaydateAPI *pd = platform.pd;
    unsigned char *frame = pd->graphics->getFrame();
    memcpy(frame, platform.bitFrame, sizeof(platform.bitFrame));
    pd->graphics->markUpdatedRows(0, LCD_ROWS - 1);

    statHostFrames++;
    statFramesShown++;
#if defined(PD_FRAMEDUMP)
    if ((statHostFrames % 100) == 0) WriteFrameDump();
#endif
#else
    frameDirty = true;

    GameYield();
#endif
}

//----------------------------------------------------------------------------------
// Module Functions Definition: Misc
//----------------------------------------------------------------------------------

// Get elapsed time measure in seconds since InitTimer()
double GetTime(void)
{
#if defined(TARGET_SIMULATOR)
    // Simulator: the pdex library runs in the host process, so the host
    // monotonic clock is available and thread-safe
    struct timespec ts = { 0 };
    clock_gettime(CLOCK_MONOTONIC, &ts);
    unsigned long long nanoSeconds = (unsigned long long)ts.tv_sec*1000000000LLU + (unsigned long long)ts.tv_nsec;

    return (double)(nanoSeconds - CORE.Time.base)*1e-9;  // Elapsed time since InitTimer()
#else
    // Device: single hardware thread (coroutine), pd is safe to call here.
    // getElapsedTime() is the hi-res timer (microsecond-ish resolution, float
    // seconds since launch; float granularity degrades to ~1 ms only after
    // multi-hour sessions) — needed by physics/timing code like Physac
    return (double)platform.pd->system->getElapsedTime();
#endif
}

// Open URL with default system browser (if available)
void OpenURL(const char *url)
{
    TRACELOG(LOG_WARNING, "OpenURL() not available on target platform");
}

//----------------------------------------------------------------------------------
// Module Functions Definition: Inputs
//----------------------------------------------------------------------------------

// Set internal gamepad mappings
int SetGamepadMappings(const char *mappings)
{
    TRACELOG(LOG_WARNING, "SetGamepadMappings() not implemented on target platform");
    return 0;
}

// Set gamepad vibration
void SetGamepadVibration(int gamepad, float leftMotor, float rightMotor, float duration)
{
    TRACELOG(LOG_WARNING, "SetGamepadVibration() not implemented on target platform");
}

// Set mouse position XY
void SetMousePosition(int x, int y)
{
    CORE.Input.Mouse.currentPosition = (Vector2){ (float)x, (float)y };
    CORE.Input.Mouse.previousPosition = CORE.Input.Mouse.currentPosition;
}

// Set mouse cursor
void SetMouseCursor(int cursor)
{
    TRACELOG(LOG_WARNING, "SetMouseCursor() not implemented on target platform");
}

// Get physical key name
const char *GetKeyName(int key)
{
    TRACELOG(LOG_WARNING, "GetKeyName() not implemented on target platform");
    return "";
}

// Register all input events
// NOTE: Runs on the game thread, right after SwapScreenBuffer() yielded and the
// host handed over a fresh input snapshot
void PollInputEvents(void)
{
#if SUPPORT_GESTURES_SYSTEM
    UpdateGestures();
#endif

    // Single-source frame pacing: the Playdate runtime already paces the update
    // callback, so a SetTargetFPS() request becomes the display refresh rate and
    // raylib's own frame limiter is disabled. Left enabled, the two 20 ms pacers
    // run in series and beat against each other (measured ~44 fps with stutter
    // spikes instead of a locked 50)
    if (CORE.Time.target > 0.0)
    {
        float fps = (float)(1.0/CORE.Time.target);
        if (fps > 50.0f) fps = 50.0f;       // Playdate display maximum
        platform.requestedRefreshRate = fps;
        CORE.Time.target = 0.0;
    }

    // Reset keys/chars pressed registered
    CORE.Input.Keyboard.keyPressedQueueCount = 0;
    CORE.Input.Keyboard.charPressedQueueCount = 0;

    // Reset last gamepad button/axis registered state
    CORE.Input.Gamepad.lastButtonPressed = 0;   // GAMEPAD_BUTTON_UNKNOWN

    // Register previous keys states
    for (int i = 0; i < MAX_KEYBOARD_KEYS; i++)
    {
        CORE.Input.Keyboard.previousKeyState[i] = CORE.Input.Keyboard.currentKeyState[i];
        CORE.Input.Keyboard.keyRepeatInFrame[i] = 0;
    }

    // Register previous gamepad button states
    for (int i = 0; i < MAX_GAMEPAD_BUTTONS; i++) CORE.Input.Gamepad.previousButtonState[0][i] = CORE.Input.Gamepad.currentButtonState[0][i];

    // Register previous touch states
    for (int i = 0; i < MAX_TOUCH_POINTS; i++) CORE.Input.Touch.previousTouchState[i] = CORE.Input.Touch.currentTouchState[i];

#if defined(PD_RAYLIB_LUA)
    // Lua mode: single context, read the Playdate input state directly
    PDButtons current = 0, pushed = 0, released = 0;
    platform.pd->system->getButtonState(&current, &pushed, &released);
    platform.frameCrankChange = platform.pd->system->getCrankChange();
    platform.frameCrankAngle = platform.pd->system->getCrankAngle();
    platform.frameCrankDocked = platform.pd->system->isCrankDocked()? true : false;

    // Apply a SetTargetFPS() request (no host update callback to do it)
    if (platform.requestedRefreshRate > 0.0f)
    {
        platform.pd->display->setRefreshRate(platform.requestedRefreshRate);
        platform.requestedRefreshRate = 0.0f;
    }
#else
    // Consume the host input snapshot (safe: host is blocked while the game runs)
    PDButtons current = platform.input.current;
    PDButtons pushed = platform.input.pushed;
    platform.input.pushed = 0;
    platform.input.released = 0;
    platform.frameCrankChange = platform.input.crankDelta;
    platform.input.crankDelta = 0.0f;
    platform.frameCrankAngle = platform.input.crankAngle;
    platform.frameCrankDocked = platform.input.crankDocked;
#endif

    // Map buttons to keyboard keys and gamepad 0 buttons
    static const struct { PDButtons button; int key; int gamepadButton; } map[6] = {
        { kButtonUp,    KEY_UP,    GAMEPAD_BUTTON_LEFT_FACE_UP },
        { kButtonDown,  KEY_DOWN,  GAMEPAD_BUTTON_LEFT_FACE_DOWN },
        { kButtonLeft,  KEY_LEFT,  GAMEPAD_BUTTON_LEFT_FACE_LEFT },
        { kButtonRight, KEY_RIGHT, GAMEPAD_BUTTON_LEFT_FACE_RIGHT },
        { kButtonA,     KEY_SPACE, GAMEPAD_BUTTON_RIGHT_FACE_DOWN },
        { kButtonB,     KEY_ENTER, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT },
    };

    for (int i = 0; i < 6; i++)
    {
        // Held state: report held if currently down or if a press edge occurred
        // inside the slice (press+release between polls still counts one frame)
        char down = ((current & map[i].button) || (pushed & map[i].button))? 1 : 0;

        CORE.Input.Keyboard.currentKeyState[map[i].key] = down;
        CORE.Input.Gamepad.currentButtonState[0][map[i].gamepadButton] = down;

        if (pushed & map[i].button)
        {
            if (CORE.Input.Keyboard.keyPressedQueueCount < MAX_KEY_PRESSED_QUEUE)
            {
                CORE.Input.Keyboard.keyPressedQueue[CORE.Input.Keyboard.keyPressedQueueCount] = map[i].key;
                CORE.Input.Keyboard.keyPressedQueueCount++;
            }

            CORE.Input.Gamepad.lastButtonPressed = map[i].gamepadButton;
        }
    }

    CORE.Input.Gamepad.ready[0] = true;

    // Crank rotation maps to mouse wheel (one full turn = one wheel unit)
    CORE.Input.Mouse.previousWheelMove = CORE.Input.Mouse.currentWheelMove;
    CORE.Input.Mouse.currentWheelMove = (Vector2){ 0.0f, platform.frameCrankChange/360.0f };

    // Virtual mouse: the d-pad moves a cursor in screen (virtual-resolution)
    // coordinates, the crank drives its X axis (one full turn = screen width),
    // A = left button and B = middle button. Lets mouse-driven raylib code
    // (e.g. raylib-games' missile_commander) run unmodified
    CORE.Input.Mouse.previousPosition = CORE.Input.Mouse.currentPosition;
    for (int i = 0; i < MAX_MOUSE_BUTTONS; i++) CORE.Input.Mouse.previousButtonState[i] = CORE.Input.Mouse.currentButtonState[i];

    static bool cursorInitialized = false;
    if (!cursorInitialized)
    {
        cursorInitialized = true;
        CORE.Input.Mouse.currentPosition = (Vector2){ (float)CORE.Window.screen.width/2, (float)CORE.Window.screen.height/2 };
        CORE.Input.Mouse.previousPosition = CORE.Input.Mouse.currentPosition;
    }

    float cursorStep = (float)CORE.Window.screen.width/100.0f;      // Full crossing in ~2s
    if (current & kButtonRight) CORE.Input.Mouse.currentPosition.x += cursorStep;
    if (current & kButtonLeft) CORE.Input.Mouse.currentPosition.x -= cursorStep;
    if (current & kButtonDown) CORE.Input.Mouse.currentPosition.y += cursorStep;
    if (current & kButtonUp) CORE.Input.Mouse.currentPosition.y -= cursorStep;
    CORE.Input.Mouse.currentPosition.x += platform.frameCrankChange*(float)CORE.Window.screen.width/360.0f;

    if (CORE.Input.Mouse.currentPosition.x < 0) CORE.Input.Mouse.currentPosition.x = 0;
    if (CORE.Input.Mouse.currentPosition.x > (float)CORE.Window.screen.width) CORE.Input.Mouse.currentPosition.x = (float)CORE.Window.screen.width;
    if (CORE.Input.Mouse.currentPosition.y < 0) CORE.Input.Mouse.currentPosition.y = 0;
    if (CORE.Input.Mouse.currentPosition.y > (float)CORE.Window.screen.height) CORE.Input.Mouse.currentPosition.y = (float)CORE.Window.screen.height;

    CORE.Input.Mouse.currentButtonState[MOUSE_BUTTON_LEFT] = ((current & kButtonA) || (pushed & kButtonA))? 1 : 0;
    CORE.Input.Mouse.currentButtonState[MOUSE_BUTTON_MIDDLE] = ((current & kButtonB) || (pushed & kButtonB))? 1 : 0;

    // Show the crosshair overlay for ~3s whenever the virtual mouse moves
    if (((CORE.Input.Mouse.currentPosition.x != CORE.Input.Mouse.previousPosition.x) ||
         (CORE.Input.Mouse.currentPosition.y != CORE.Input.Mouse.previousPosition.y)) &&
        !CORE.Input.Mouse.cursorHidden) platform.cursorVisibleFrames = 150;
    else if (platform.cursorVisibleFrames > 0) platform.cursorVisibleFrames--;
}

//----------------------------------------------------------------------------------
// Module Functions Definition: Playdate extensions (see playdate/pd_raylib.h)
//----------------------------------------------------------------------------------

// Get current crank angle (degrees, [0..360), 0 = crank pointing up)
float GetCrankAngle(void)
{
    return platform.frameCrankAngle;
}

// Get crank angle variation since last frame (degrees)
float GetCrankChange(void)
{
    return platform.frameCrankChange;
}

// Check if the crank is docked
bool IsCrankDocked(void)
{
    return platform.frameCrankDocked;
}

// Get the PlaydateAPI pointer
// WARNING: raylib code runs on the game thread; most pd->system/display/graphics
// calls are only safe from the host (update callback) side
PlaydateAPI *GetPlaydateAPI(void)
{
    return platform.pd;
}

//----------------------------------------------------------------------------------
// Module Internal Functions Definition
//----------------------------------------------------------------------------------

// Load a file through pd->file, so examples can read assets bundled in the .pdx
// (and files in the game's Data dir) on both simulator and device; raylib's
// LoadImage/LoadFont/LoadModel all route through the LoadFileData/Text callbacks
static unsigned char *PlaydateLoadFileData(const char *fileName, int *dataSize)
{
    PlaydateAPI *pd = platform.pd;

    *dataSize = 0;

    FileStat fileStat = { 0 };
    if ((pd->file->stat(fileName, &fileStat) != 0) || fileStat.isdir)
    {
        TRACELOG(LOG_WARNING, "FILEIO: [%s] File not found in pdx/Data", fileName);
        return NULL;
    }

    SDFile *file = pd->file->open(fileName, kFileRead|kFileReadData);
    if (file == NULL) return NULL;

    unsigned char *data = (unsigned char *)RL_MALLOC(fileStat.size);
    int bytesRead = (data != NULL)? pd->file->read(file, data, fileStat.size) : 0;
    pd->file->close(file);

    if (bytesRead != (int)fileStat.size)
    {
        RL_FREE(data);
        TRACELOG(LOG_WARNING, "FILEIO: [%s] Failed to read file", fileName);
        return NULL;
    }

    *dataSize = bytesRead;
    TRACELOG(LOG_INFO, "FILEIO: [%s] File loaded successfully (%i bytes)", fileName, bytesRead);
    return data;
}

// Text variant of PlaydateLoadFileData (adds the NUL terminator)
static char *PlaydateLoadFileText(const char *fileName)
{
    int dataSize = 0;
    unsigned char *data = PlaydateLoadFileData(fileName, &dataSize);
    if (data == NULL) return NULL;

    char *text = (char *)RL_REALLOC(data, dataSize + 1);
    if (text == NULL) { RL_FREE(data); return NULL; }
    text[dataSize] = '\0';

    return text;
}

// Initialize platform: graphics, inputs and more
// NOTE: Runs on the game thread (called from InitWindow()); the PlaydateAPI
// pointer is already stored by eventHandler() before the game thread starts
int InitPlatform(void)
{
#if defined(TARGET_SIMULATOR)
    // Move the process CWD inside the .pdx bundle so plain-stdio asset paths
    // work: some raylib games fopen("resources/...") directly instead of
    // going through the pd->file-backed callbacks (transmission's
    // missions.txt crashed on this). dladdr on one of our own symbols
    // locates pdex.dylib, which lives at the bundle root.
    {
        Dl_info dlinfo;
        if (dladdr((const void *)&InitPlatform, &dlinfo) && (dlinfo.dli_fname != NULL))
        {
            char pdxPath[1024] = { 0 };
            strncpy(pdxPath, dlinfo.dli_fname, sizeof(pdxPath) - 1);
            char *slash = strrchr(pdxPath, '/');
            if (slash != NULL)
            {
                *slash = '\0';
                if (chdir(pdxPath) == 0) TRACELOG(LOG_INFO, "PLATFORM: CWD set to pdx bundle: %s", pdxPath);
            }
        }
    }
#endif

    // Playdate rendering requires the rlsw software renderer
    if (rlGetVersion() != RL_OPENGL_SOFTWARE)
    {
        TRACELOG(LOG_WARNING, "DISPLAY: Playdate platform requires software renderer (GRAPHICS_API_OPENGL_SOFTWARE)");
        TRACELOG(LOG_FATAL, "PLATFORM: Failed to initialize graphics device");
        return -1;
    }

    // The Playdate LCD is fixed 400x240. Other requested sizes become a virtual
    // resolution: rlsw still rasterizes 400x240 (render size) and every frame is
    // drawn through CORE.Window.screenScale (raylib's high-DPI mechanism), so
    // games written for e.g. 800x450 run unmodified with GetScreenWidth() = 800
    CORE.Window.display.width = LCD_COLUMNS;
    CORE.Window.display.height = LCD_ROWS;
    CORE.Window.render.width = LCD_COLUMNS;
    CORE.Window.render.height = LCD_ROWS;
    CORE.Window.currentFbo.width = CORE.Window.render.width;
    CORE.Window.currentFbo.height = CORE.Window.render.height;

    if ((CORE.Window.screen.width != LCD_COLUMNS) || (CORE.Window.screen.height != LCD_ROWS))
    {
        CORE.Window.screenScale = MatrixScale((float)LCD_COLUMNS/(float)CORE.Window.screen.width,
                                              (float)LCD_ROWS/(float)CORE.Window.screen.height, 1.0f);
        TRACELOG(LOG_INFO, "DISPLAY: Virtual resolution %ix%i scaled onto the %ix%i LCD",
                 CORE.Window.screen.width, CORE.Window.screen.height, LCD_COLUMNS, LCD_ROWS);
    }

    TRACELOG(LOG_INFO, "DISPLAY: Device initialized successfully");
    TRACELOG(LOG_INFO, "    > Display size: %i x %i", CORE.Window.display.width, CORE.Window.display.height);
    TRACELOG(LOG_INFO, "    > Screen size:  %i x %i", CORE.Window.screen.width, CORE.Window.screen.height);
    TRACELOG(LOG_INFO, "    > Render size:  %i x %i", CORE.Window.render.width, CORE.Window.render.height);

    CORE.Window.ready = true;

    // Initialize timing system
    InitTimer();

    // Initialize storage system
#if defined(TARGET_SIMULATOR)
    CORE.Storage.basePath = GetWorkingDirectory();
#else
    CORE.Storage.basePath = ".";    // No working directory on device; use pd->file for data
#endif

    // Route file loading through pd->file (reads from inside the .pdx bundle)
    SetLoadFileDataCallback(PlaydateLoadFileData);
    SetLoadFileTextCallback(PlaydateLoadFileText);

    TRACELOG(LOG_INFO, "PLATFORM: PLAYDATE (SIMULATOR): Initialized successfully");

    return 0;
}

// Close platform
void ClosePlatform(void)
{
    // Nothing to release: rlsw owns the framebuffer, bitFrame is static
}

// 4x4 Bayer matrix, thresholds spread across 0..255
static const unsigned char bayer4[4][4] = {
    {   8, 136,  40, 168 },
    { 200,  72, 232, 104 },
    {  56, 184,  24, 152 },
    { 248, 120, 216,  88 },
};

// Dither lookup tables, built once on first use. The old per-pixel pipeline
// (565 expansion to 8-bit, BT.601-ish weighted luminance, contrast stretch
// [24..232] -> [0..255] with clamps) is folded into:
//  - per-channel LUTs indexed by the raw 5/6-bit fields, pre-multiplied by the
//    luminance weights (sum <= 255*256, fits uint16_t; 256 bytes total)
//  - Bayer thresholds mapped through the inverse stretch and pre-shifted into
//    the un-normalized sum domain, so the comparison needs no >>8 either
// Output is bit-exact with the arithmetic version
static uint16_t ditherLumR[32];
static uint16_t ditherLumG[64];
static uint16_t ditherLumB[32];
static uint16_t ditherThresh[4][4];

static void InitDitherTables(void)
{
    for (int i = 0; i < 32; i++)
    {
        int c = (i << 3) | (i >> 2);
        ditherLumR[i] = (uint16_t)(c*77);
        ditherLumB[i] = (uint16_t)(c*29);
    }
    for (int i = 0; i < 64; i++)
    {
        int c = (i << 2) | (i >> 4);
        ditherLumG[i] = (uint16_t)(c*150);
    }

    for (int ty = 0; ty < 4; ty++)
    {
        for (int tx = 0; tx < 4; tx++)
        {
            // Smallest 8-bit luminance whose stretched value clears the
            // threshold (the stretch is monotone, so "stretch(lum) > t"
            // is exactly "lum >= minLum")
            int t = bayer4[ty][tx];
            int minLum = 256;
            for (int L = 0; L < 256; L++)
            {
                int s = (L - 24)*255/208;
                if (s < 0) s = 0; else if (s > 255) s = 255;
                if (s > t) { minLum = L; break; }
            }
            // Compare in sum domain: (sum >> 8) >= minLum  <=>  sum >= minLum << 8
            ditherThresh[ty][tx] = (uint16_t)(minLum << 8);
        }
    }
}

// Dither the rlsw framebuffer into the 1-bit staging frame (game thread)
// Reads rlsw's internal RGB565 color buffer directly (no conversion copy),
// following the ESP32 raylib port. Playdate rows are LCD_ROWSIZE bytes,
// MSB-first, bit set = white
static void StageFrame(void)
{
    int fbWidth = 0, fbHeight = 0;
    const unsigned short *src = (const unsigned short *)swGetColorBuffer(&fbWidth, &fbHeight);

    if ((src == NULL) || (fbWidth != LCD_COLUMNS) || (fbHeight != LCD_ROWS)) return;

    if (ditherThresh[0][0] == 0) InitDitherTables();

    for (int y = 0; y < LCD_ROWS; y++)
    {
        const uint16_t *brow = ditherThresh[y & 3];
        unsigned char *dst = &platform.bitFrame[y*LCD_ROWSIZE];
        // rlsw stores the framebuffer OpenGL-style, row 0 at the bottom
        const unsigned short *s = src + (size_t)(LCD_ROWS - 1 - y)*LCD_COLUMNS;

        for (int xb = 0; xb < LCD_COLUMNS/8; xb++)
        {
            unsigned char byte = 0;

            for (int bit = 0; bit < 8; bit++)
            {
                unsigned short p = *s++;
                unsigned int sum = ditherLumR[p >> 11] + ditherLumG[(p >> 5) & 0x3F] + ditherLumB[p & 0x1F];
                if (sum >= brow[bit & 3]) byte |= (unsigned char)(0x80 >> bit);
            }

            *dst++ = byte;
        }
    }

    // Virtual-mouse crosshair overlay (XOR so it reads on any background)
    if (platform.cursorVisibleFrames > 0)
    {
        int cx = (int)(CORE.Input.Mouse.currentPosition.x*(float)LCD_COLUMNS/(float)CORE.Window.screen.width);
        int cy = (int)(CORE.Input.Mouse.currentPosition.y*(float)LCD_ROWS/(float)CORE.Window.screen.height);

        for (int d = -4; d <= 4; d++)
        {
            int px = cx + d, py = cy + d;
            if ((px >= 0) && (px < LCD_COLUMNS) && (cy >= 0) && (cy < LCD_ROWS) && (d != 0))
                platform.bitFrame[cy*LCD_ROWSIZE + px/8] ^= (unsigned char)(0x80 >> (px%8));
            if ((py >= 0) && (py < LCD_ROWS) && (cx >= 0) && (cx < LCD_COLUMNS))
                platform.bitFrame[py*LCD_ROWSIZE + cx/8] ^= (unsigned char)(0x80 >> (cx%8));
        }
    }
}

#if !defined(PD_RAYLIB_LUA)
#if defined(TARGET_SIMULATOR)

// Yield the game thread until the host update callback hands over the next slice
static void GameYield(void)
{
    pthread_mutex_lock(&gameLock);
    gameTurn = TURN_HOST;
    pthread_cond_broadcast(&gameCond);
    while (gameTurn != TURN_GAME) pthread_cond_wait(&gameCond, &gameLock);
    pthread_mutex_unlock(&gameLock);

    statGameSlices++;
}

// Game thread entry: wait for the first slice, then run the raylib program
static void *GameThreadMain(void *arg)
{
    (void)arg;

    pthread_mutex_lock(&gameLock);
    while (gameTurn != TURN_GAME) pthread_cond_wait(&gameCond, &gameLock);
    pthread_mutex_unlock(&gameLock);

    rl_playdate_main();

    pthread_mutex_lock(&gameLock);
    gameExited = true;
    gameTurn = TURN_HOST;
    pthread_cond_broadcast(&gameCond);
    pthread_mutex_unlock(&gameLock);

    return NULL;
}

// Create the game thread (host side); it blocks until the first slice
static void HostStartGame(void)
{
    pthread_t thread;
    pthread_create(&thread, NULL, GameThreadMain, NULL);
}

// Hand the game thread one time slice and wait (bounded) for it to yield
static void HostGiveSlice(void)
{
    pthread_mutex_lock(&gameLock);
    if (gameTurn == TURN_HOST)
    {
        gameTurn = TURN_GAME;
        pthread_cond_broadcast(&gameCond);
    }

    // Wait up to 100 ms for the game to yield; longer means it is inside a
    // heavy load or a WaitTime() sleep, the next update picks it up
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_nsec += 100*1000000;
    if (deadline.tv_nsec >= 1000000000)
    {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000;
    }

    while ((gameTurn == TURN_GAME) && !gameExited)
    {
        if (pthread_cond_timedwait(&gameCond, &gameLock, &deadline) != 0) break;
    }
    pthread_mutex_unlock(&gameLock);
}

#elif defined(TARGET_PLAYDATE)

// Cortex-M7 coroutine context switch: push the AAPCS callee-saved registers
// (r4-r11, lr and s16-s31; the FPU is single-precision, fpv5-sp-d16) onto the
// current stack, save SP through saveSP, switch to loadSP and pop the other
// context's registers, "returning" into it via the popped pc
__attribute__((naked)) static void CoroSwitch(void **saveSP, void *loadSP)
{
    __asm volatile(
        "push {r4-r11, lr}      \n"
        "vpush {s16-s31}        \n"
        "mov r2, sp             \n"
        "str r2, [r0]           \n"
        "mov sp, r1             \n"
        "vpop {s16-s31}         \n"
        "pop {r4-r11, pc}       \n"
    );
}

// First entry of the game coroutine (reached via the crafted initial frame)
static void GameEntry(void)
{
    rl_playdate_main();

    gameExited = true;
    for (;;) CoroSwitch(&gameSP, hostSP);   // Nothing left to run; always hand back
}

// Yield the game coroutine until the host update callback hands over the next slice
static void GameYield(void)
{
    CoroSwitch(&gameSP, hostSP);

    statGameSlices++;
}

// Prepare the game coroutine (host side): craft an initial stack frame laid
// out exactly as CoroSwitch expects to pop it — from low to high addresses:
// s16-s31 (16 words), r4-r11 (8 words), lr (GameEntry, thumb bit set)
static void HostStartGame(void)
{
    uintptr_t top = ((uintptr_t)gameStack + GAME_STACK_SIZE) & ~(uintptr_t)7;
    uint32_t *frame = (uint32_t *)(top - 25*sizeof(uint32_t));

    for (int i = 0; i < 24; i++) frame[i] = 0;
    frame[24] = (uint32_t)(uintptr_t)&GameEntry | 1;    // Popped into pc (thumb)

    gameSP = frame;
}

// Switch into the game coroutine; returns when it yields (or exits)
static void HostGiveSlice(void)
{
    CoroSwitch(&hostSP, gameSP);
}

#endif
#endif // !PD_RAYLIB_LUA

#if defined(PD_FRAMEDUMP)
// Write the staged 1-bit frame and a status heartbeat to the game's Data
// directory, for headless verification (host thread)
static void WriteFrameDump(void)
{
    PlaydateAPI *pd = platform.pd;

    SDFile *f = pd->file->open("frame.bin", kFileWrite);
    if (f != NULL)
    {
        pd->file->write(f, (void *)platform.bitFrame, sizeof(platform.bitFrame));
        pd->file->close(f);
    }

#if defined(PD_RAYLIB_LUA)
    const char *exited = "false";   // Lua mode has no game thread to exit
#else
    const char *exited = gameExited? "true" : "false";
#endif
    char buf[256];
    int n = snprintf(buf, sizeof(buf),
                     "{\"host_frames\":%u,\"game_slices\":%u,\"frames_shown\":%u,\"game_exited\":%s}\n",
                     statHostFrames, statGameSlices, statFramesShown, exited);

    f = pd->file->open("pd_status.json", kFileWrite);
    if (f != NULL)
    {
        pd->file->write(f, buf, (unsigned int)n);
        pd->file->close(f);
    }
}
#endif

#if defined(TARGET_PLAYDATE)
// Route raylib logs to the Playdate console (default TraceLog vprintf goes
// nowhere on device)
static void PlaydateTraceLog(int logLevel, const char *text, va_list args)
{
    (void)logLevel;

    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), text, args);
    if (platform.pd != NULL) platform.pd->system->logToConsole("%s", buffer);
}

// Minimal newlib syscall stubs: raylib's stdio-based file paths still link
// against these even though the examples never call them
int _write(int fd, const void *buf, size_t count) { (void)fd; (void)buf; return (int)count; }
int _read(int fd, void *buf, size_t count) { (void)fd; (void)buf; (void)count; return 0; }
int _close(int fd) { (void)fd; return -1; }
int _lseek(int fd, int off, int whence) { (void)fd; (void)off; (void)whence; return 0; }
int _fstat(int fd, void *st) { (void)fd; (void)st; return -1; }
int _isatty(int fd) { (void)fd; return 1; }
int _getpid(void) { return 1; }
int _kill(int pid, int sig) { (void)pid; (void)sig; return -1; }
void _exit(int status) { (void)status; for (;;) { } }
int _open(const char *path, int flags, int mode) { (void)path; (void)flags; (void)mode; return -1; }
int _unlink(const char *path) { (void)path; return -1; }
int _gettimeofday(void *tv, void *tz) { (void)tv; (void)tz; return -1; }
int _sbrk(int incr) { (void)incr; return -1; }
int _stat(const char *path, void *st) { (void)path; (void)st; return -1; }
char *getcwd(char *buf, size_t size) { if ((buf != NULL) && (size > 1)) { buf[0] = '.'; buf[1] = '\0'; } return buf; }
int chdir(const char *path) { (void)path; return 0; }   // Pretend success: pd->file paths are pdx-relative anyway
void _init(void) { }
void _fini(void) { }    // Referenced by __libc_fini_array; we link with -nostartfiles
#endif

#if !defined(PD_RAYLIB_LUA)
// Playdate update callback (host side): snapshot input, hand the game thread a
// slice, blit the staged frame when dirty
static int PlaydateUpdate(void *userdata)
{
    PlaydateAPI *pd = (PlaydateAPI *)userdata;

    statHostFrames++;

#if defined(PD_TIMING)
    float timingCallbackStart = pd->system->getElapsedTime();
    float timingSliceDuration = 0.0f;
    unsigned char timingFrameDirty = 0;
#endif

    // Refresh the input snapshot (accumulate edges between game polls)
    PDButtons current = 0, pushed = 0, released = 0;
    pd->system->getButtonState(&current, &pushed, &released);

    platform.input.current = current;
    platform.input.pushed |= pushed;
    platform.input.released |= released;
    platform.input.crankDelta += pd->system->getCrankChange();
    platform.input.crankAngle = pd->system->getCrankAngle();
    platform.input.crankDocked = pd->system->isCrankDocked()? true : false;

#if defined(PD_FRAMEDUMP)
    // Autopilot button-mash: if a Data file named "autopilot" exists
    // (contents "mash <seed>"), synthesize input on top of the real buttons —
    // any game becomes headlessly soak-testable with zero source changes.
    // Direction held for 20-90 frames, A tapped often, B rarely.
    {
        static int apChecked = 0;
        static unsigned int apState = 0;            // 0 = autopilot off
        static PDButtons apHeld = 0, apTap = 0;
        static unsigned int apHoldUntil = 0, apNextTap = 0, apTapLeft = 0;

        if (!apChecked)
        {
            apChecked = 1;
            SDFile *f = pd->file->open("autopilot", kFileReadData);
            if (f != NULL)
            {
                char buf[64] = { 0 };
                int n = pd->file->read(f, buf, sizeof(buf) - 1);
                pd->file->close(f);
                unsigned int seed = 0;
                for (int i = 0; (i < n) && (buf[i] != '\0'); i++)
                    if ((buf[i] >= '0') && (buf[i] <= '9')) seed = seed*10u + (unsigned int)(buf[i] - '0');
                apState = (seed == 0)? 0x9d2c5680u : seed;
                pd->system->logToConsole("AUTOPILOT: active (seed %u)", apState);
            }
        }
        if (apState != 0)
        {
            #define AP_RAND() (apState ^= apState << 13, apState ^= apState >> 17, apState ^= apState << 5, apState)
            if (statHostFrames >= apHoldUntil)
            {
                static const PDButtons dirs[6] = { kButtonUp, kButtonDown, kButtonLeft, kButtonRight,
                                                   (PDButtons)(kButtonUp|kButtonRight), (PDButtons)0 };
                apHeld = dirs[AP_RAND()%6];
                apHoldUntil = statHostFrames + 20 + AP_RAND()%70;
                platform.input.pushed |= apHeld;    // edge for IsKeyPressed users
            }
            if (statHostFrames >= apNextTap)
            {
                unsigned int r = AP_RAND()%100;
                apTap = (r < 55)? kButtonA : ((r < 65)? kButtonB : (PDButtons)0);
                apTapLeft = (apTap != 0)? 3 : 0;
                if (apTap != 0) platform.input.pushed |= apTap;
                apNextTap = statHostFrames + 25 + AP_RAND()%60;
            }
            PDButtons synth = apHeld;
            if (apTapLeft > 0) { synth |= apTap; apTapLeft--; }
            platform.input.current = (PDButtons)(platform.input.current | synth);
            #undef AP_RAND
        }
    }
#endif

    if (!gameThreadStarted)
    {
        gameThreadStarted = true;
        HostStartGame();
    }

    // Apply a SetTargetFPS() request forwarded by the game context
    if (platform.requestedRefreshRate > 0.0f)
    {
        pd->display->setRefreshRate(platform.requestedRefreshRate);
        platform.requestedRefreshRate = 0.0f;
    }

    if (!gameExited)
    {
#if defined(PD_TIMING)
        float t0 = pd->system->getElapsedTime();
        HostGiveSlice();
        timingSliceDuration = pd->system->getElapsedTime() - t0;
#else
        HostGiveSlice();
#endif
    }

#if defined(TARGET_SIMULATOR)
    // Run the game's queued pd->sound mutations on this (host) thread — the
    // simulator sound engine deadlocks if the game pthread mutates players
    // directly (see pd_raudio.c). pd_raudio is always linked in this tree.
    {
        extern void PdRaudioHostPump(void);
        PdRaudioHostPump();
    }
#endif

    if (frameDirty)
    {
        frameDirty = false;
#if defined(PD_TIMING)
        timingFrameDirty = 1;
#endif
        statFramesShown++;

        unsigned char *frame = pd->graphics->getFrame();
        memcpy(frame, platform.bitFrame, sizeof(platform.bitFrame));
        pd->graphics->markUpdatedRows(0, LCD_ROWS - 1);
    }

#if defined(PD_FRAMEDUMP)
    if ((statHostFrames % 100) == 0) WriteFrameDump();
#endif

#if defined(PD_TIMING)
    // Temporary instrumentation: per-update timing of the first 600 host
    // frames, dumped as CSV for startup-performance analysis
    {
        #define PD_TIMING_FRAMES 600
        static float tCallback[PD_TIMING_FRAMES];   // Callback entry time (s)
        static float tSlice[PD_TIMING_FRAMES];      // HostGiveSlice duration (s) -- filled below
        static unsigned char tDirty[PD_TIMING_FRAMES];

        unsigned int i = statHostFrames - 1;
        if (i < PD_TIMING_FRAMES)
        {
            tCallback[i] = timingCallbackStart;
            tSlice[i] = timingSliceDuration;
            tDirty[i] = timingFrameDirty;
        }
        else if (i == PD_TIMING_FRAMES)
        {
            SDFile *f = pd->file->open("timing.csv", kFileWrite);
            if (f != NULL)
            {
                char line[64];
                int n = snprintf(line, sizeof(line), "frame,cb_start,slice_dur,dirty\n");
                pd->file->write(f, line, (unsigned int)n);
                for (unsigned int k = 0; k < PD_TIMING_FRAMES; k++)
                {
                    n = snprintf(line, sizeof(line), "%u,%.6f,%.6f,%u\n", k, (double)tCallback[k], (double)tSlice[k], tDirty[k]);
                    pd->file->write(f, line, (unsigned int)n);
                }
                pd->file->close(f);
            }
        }
    }
#endif

    return 1;
}
#endif // !PD_RAYLIB_LUA

// Playdate entry point (called by the Playdate OS/Simulator)
#ifdef _WINDLL
__declspec(dllexport)
#endif
int eventHandler(PlaydateAPI *pd, PDSystemEvent event, uint32_t arg)
{
    (void)arg;

    switch (event)
    {
        case kEventInit:
        {
            platform.pd = pd;
#if defined(TARGET_PLAYDATE)
            SetTraceLogCallback(PlaydateTraceLog);
#endif
            pd->display->setRefreshRate(50.0f);
#if !defined(PD_RAYLIB_LUA)
            // C mode: the update callback drives the green thread. In Lua mode
            // the Lua runtime owns the update loop (setting a callback here
            // would disable Lua), and playdate.update() calls raylib directly
            pd->system->setUpdateCallback(PlaydateUpdate, pd);
#endif
            pd->system->logToConsole("raylib %s Playdate platform starting", RAYLIB_VERSION);
        } break;
#if defined(PD_RAYLIB_LUA)
        case kEventInitLua:
        {
            PdRaylibLuaRegister(pd);
        } break;
#endif
        case kEventTerminate:
        {
            CORE.Window.shouldClose = true;
        } break;
        default: break;
    }

    return 0;
}

// EOF
