/**********************************************************************************************
*
*   raylib_lua.c - raylib bindings for the Playdate Lua runtime
*
*   Registered into Lua as the `rl` namespace by PdRaylibLuaRegister(), which the
*   platform backend (rcore_playdate.c, built with PD_RAYLIB_LUA) calls on
*   kEventInitLua. Lua's playdate.update() then drives raylib synchronously:
*
*       rl.initWindow(400, 240)
*       function playdate.update()
*           rl.beginDrawing()
*           rl.clearBackground(245, 245, 245)
*           rl.drawCircle(200, 120, 40, 190, 33, 55)
*           rl.endDrawing()
*       end
*
*   Conventions:
*       - colors are trailing r, g, b arguments (0-255, alpha = 255)
*       - textures are integer handles from rl.loadTexture() (0 = failure)
*       - input comes from the regular playdate.* Lua API, not raylib
*
*   LICENSE: zlib/libpng
*
**********************************************************************************************/

#include "pd_api.h"
#include "raylib.h"
#include "rlgl.h"

static PlaydateAPI *pd = NULL;

#define MAX_LUA_TEXTURES 64
static Texture2D luaTextures[MAX_LUA_TEXTURES];     // Slot 0 unused: handle 0 means failure

#define MAX_LUA_MODELS 8
static Model luaModels[MAX_LUA_MODELS];             // Slot 0 unused: handle 0 means failure
static bool luaModelUsed[MAX_LUA_MODELS];

#define MAX_LUA_FONTS 8
static Font luaFonts[MAX_LUA_FONTS];

#define MAX_LUA_IMAGES 8
static Image luaImages[MAX_LUA_IMAGES];
static int luaImageFrames[MAX_LUA_IMAGES];          // Frame count (LoadImageAnim), 1 for stills

#define MAX_LUA_RENDERTEXTURES 4
static RenderTexture2D luaRenderTextures[MAX_LUA_RENDERTEXTURES];

static Camera3D luaCamera;                          // Last camera passed to rl.beginMode3D (for billboards)

// Trailing r,g,b arguments starting at position pos
static Color ArgColor(int pos)
{
    Color color = { 0, 0, 0, 255 };
    color.r = (unsigned char)pd->lua->getArgInt(pos);
    color.g = (unsigned char)pd->lua->getArgInt(pos + 1);
    color.b = (unsigned char)pd->lua->getArgInt(pos + 2);
    return color;
}

//----------------------------------------------------------------------------------
// Core
//----------------------------------------------------------------------------------
static int l_initWindow(lua_State *L)
{
    (void)L;
    InitWindow(pd->lua->getArgInt(1), pd->lua->getArgInt(2), "raylib-lua");
    InitAudioDevice();      // pd_raudio: free to initialize, so always available in Lua
    return 0;
}

static int l_setTargetFPS(lua_State *L)
{
    (void)L;
    SetTargetFPS(pd->lua->getArgInt(1));
    return 0;
}

static int l_beginDrawing(lua_State *L) { (void)L; BeginDrawing(); return 0; }
static int l_endDrawing(lua_State *L) { (void)L; EndDrawing(); return 0; }

static int l_clearBackground(lua_State *L)
{
    (void)L;
    ClearBackground(ArgColor(1));
    return 0;
}

static int l_getFrameTime(lua_State *L) { (void)L; pd->lua->pushFloat(GetFrameTime()); return 1; }
static int l_getTime(lua_State *L) { (void)L; pd->lua->pushFloat((float)GetTime()); return 1; }
static int l_getFPS(lua_State *L) { (void)L; pd->lua->pushInt(GetFPS()); return 1; }

//----------------------------------------------------------------------------------
// Text
//----------------------------------------------------------------------------------
static int l_drawText(lua_State *L)
{
    (void)L;
    DrawText(pd->lua->getArgString(1), pd->lua->getArgInt(2), pd->lua->getArgInt(3), pd->lua->getArgInt(4), ArgColor(5));
    return 0;
}

static int l_measureText(lua_State *L)
{
    (void)L;
    pd->lua->pushInt(MeasureText(pd->lua->getArgString(1), pd->lua->getArgInt(2)));
    return 1;
}

static int l_drawFPS(lua_State *L)
{
    (void)L;
    DrawFPS(pd->lua->getArgInt(1), pd->lua->getArgInt(2));
    return 0;
}

//----------------------------------------------------------------------------------
// Shapes
//----------------------------------------------------------------------------------
static int l_drawPixel(lua_State *L)
{
    (void)L;
    DrawPixel(pd->lua->getArgInt(1), pd->lua->getArgInt(2), ArgColor(3));
    return 0;
}

static int l_drawLine(lua_State *L)
{
    (void)L;
    Vector2 start = { pd->lua->getArgFloat(1), pd->lua->getArgFloat(2) };
    Vector2 end = { pd->lua->getArgFloat(3), pd->lua->getArgFloat(4) };
    DrawLineEx(start, end, pd->lua->getArgFloat(5), ArgColor(6));
    return 0;
}

static int l_drawCircle(lua_State *L)
{
    (void)L;
    DrawCircle(pd->lua->getArgInt(1), pd->lua->getArgInt(2), pd->lua->getArgFloat(3), ArgColor(4));
    return 0;
}

static int l_drawCircleLines(lua_State *L)
{
    (void)L;
    DrawCircleLines(pd->lua->getArgInt(1), pd->lua->getArgInt(2), pd->lua->getArgFloat(3), ArgColor(4));
    return 0;
}

static int l_drawRectangle(lua_State *L)
{
    (void)L;
    DrawRectangle(pd->lua->getArgInt(1), pd->lua->getArgInt(2), pd->lua->getArgInt(3), pd->lua->getArgInt(4), ArgColor(5));
    return 0;
}

static int l_drawRectangleLines(lua_State *L)
{
    (void)L;
    DrawRectangleLines(pd->lua->getArgInt(1), pd->lua->getArgInt(2), pd->lua->getArgInt(3), pd->lua->getArgInt(4), ArgColor(5));
    return 0;
}

static int l_drawTriangle(lua_State *L)
{
    (void)L;
    Vector2 v1 = { pd->lua->getArgFloat(1), pd->lua->getArgFloat(2) };
    Vector2 v2 = { pd->lua->getArgFloat(3), pd->lua->getArgFloat(4) };
    Vector2 v3 = { pd->lua->getArgFloat(5), pd->lua->getArgFloat(6) };
    DrawTriangle(v1, v2, v3, ArgColor(7));
    return 0;
}

static int l_drawPoly(lua_State *L)
{
    (void)L;
    Vector2 center = { pd->lua->getArgFloat(1), pd->lua->getArgFloat(2) };
    DrawPoly(center, pd->lua->getArgInt(3), pd->lua->getArgFloat(4), pd->lua->getArgFloat(5), ArgColor(6));
    return 0;
}

static int l_drawPolyLines(lua_State *L)
{
    (void)L;
    Vector2 center = { pd->lua->getArgFloat(1), pd->lua->getArgFloat(2) };
    DrawPolyLinesEx(center, pd->lua->getArgInt(3), pd->lua->getArgFloat(4), pd->lua->getArgFloat(5), pd->lua->getArgFloat(6), ArgColor(7));
    return 0;
}

//----------------------------------------------------------------------------------
// Textures
//----------------------------------------------------------------------------------
static int l_loadTexture(lua_State *L)
{
    (void)L;
    int handle = 0;

    for (int i = 1; i < MAX_LUA_TEXTURES; i++)
    {
        if (luaTextures[i].id == 0)
        {
            luaTextures[i] = LoadTexture(pd->lua->getArgString(1));
            if (luaTextures[i].id != 0) handle = i;
            break;
        }
    }

    pd->lua->pushInt(handle);
    return 1;
}

static int l_unloadTexture(lua_State *L)
{
    (void)L;
    int handle = pd->lua->getArgInt(1);
    if ((handle > 0) && (handle < MAX_LUA_TEXTURES) && (luaTextures[handle].id != 0))
    {
        UnloadTexture(luaTextures[handle]);
        luaTextures[handle].id = 0;
    }
    return 0;
}

static int l_drawTexture(lua_State *L)
{
    (void)L;
    int handle = pd->lua->getArgInt(1);
    if ((handle <= 0) || (handle >= MAX_LUA_TEXTURES)) return 0;

    Color tint = (pd->lua->getArgCount() >= 6)? ArgColor(4) : WHITE;
    DrawTexture(luaTextures[handle], pd->lua->getArgInt(2), pd->lua->getArgInt(3), tint);
    return 0;
}

static int l_drawTextureEx(lua_State *L)
{
    (void)L;
    int handle = pd->lua->getArgInt(1);
    if ((handle <= 0) || (handle >= MAX_LUA_TEXTURES)) return 0;

    Vector2 position = { pd->lua->getArgFloat(2), pd->lua->getArgFloat(3) };
    Color tint = (pd->lua->getArgCount() >= 8)? ArgColor(6) : WHITE;
    DrawTextureEx(luaTextures[handle], position, pd->lua->getArgFloat(4), pd->lua->getArgFloat(5), tint);
    return 0;
}

static int l_textureSize(lua_State *L)
{
    (void)L;
    int handle = pd->lua->getArgInt(1);
    int w = 0, h = 0;
    if ((handle > 0) && (handle < MAX_LUA_TEXTURES))
    {
        w = luaTextures[handle].width;
        h = luaTextures[handle].height;
    }
    pd->lua->pushInt(w);
    pd->lua->pushInt(h);
    return 2;
}

//----------------------------------------------------------------------------------
// 2D camera, blend modes, rlgl matrix stack
//----------------------------------------------------------------------------------
static int l_beginMode2D(lua_State *L)
{
    (void)L;
    Camera2D camera = { 0 };
    camera.target = (Vector2){ pd->lua->getArgFloat(1), pd->lua->getArgFloat(2) };
    camera.offset = (Vector2){ pd->lua->getArgFloat(3), pd->lua->getArgFloat(4) };
    camera.rotation = pd->lua->getArgFloat(5);
    camera.zoom = pd->lua->getArgFloat(6);
    BeginMode2D(camera);
    return 0;
}

static int l_endMode2D(lua_State *L) { (void)L; EndMode2D(); return 0; }

static int l_beginBlendMode(lua_State *L) { (void)L; BeginBlendMode(pd->lua->getArgInt(1)); return 0; }
static int l_endBlendMode(lua_State *L) { (void)L; EndBlendMode(); return 0; }

static int l_pushMatrix(lua_State *L) { (void)L; rlPushMatrix(); return 0; }
static int l_popMatrix(lua_State *L) { (void)L; rlPopMatrix(); return 0; }

static int l_translate(lua_State *L)
{
    (void)L;
    rlTranslatef(pd->lua->getArgFloat(1), pd->lua->getArgFloat(2), pd->lua->getArgFloat(3));
    return 0;
}

static int l_rotate(lua_State *L)
{
    (void)L;
    rlRotatef(pd->lua->getArgFloat(1), pd->lua->getArgFloat(2), pd->lua->getArgFloat(3), pd->lua->getArgFloat(4));
    return 0;
}

static int l_getRandomValue(lua_State *L)
{
    (void)L;
    pd->lua->pushInt(GetRandomValue(pd->lua->getArgInt(1), pd->lua->getArgInt(2)));
    return 1;
}

//----------------------------------------------------------------------------------
// Gradients and generated textures
//----------------------------------------------------------------------------------
static int l_drawRectangleGradientH(lua_State *L)
{
    (void)L;
    DrawRectangleGradientH(pd->lua->getArgInt(1), pd->lua->getArgInt(2), pd->lua->getArgInt(3), pd->lua->getArgInt(4), ArgColor(5), ArgColor(8));
    return 0;
}

static int l_drawRectangleGradientV(lua_State *L)
{
    (void)L;
    DrawRectangleGradientV(pd->lua->getArgInt(1), pd->lua->getArgInt(2), pd->lua->getArgInt(3), pd->lua->getArgInt(4), ArgColor(5), ArgColor(8));
    return 0;
}

static int l_drawCircleGradient(lua_State *L)
{
    (void)L;
    Vector2 center = { pd->lua->getArgFloat(1), pd->lua->getArgFloat(2) };
    DrawCircleGradient(center, pd->lua->getArgFloat(3), ArgColor(4), ArgColor(7));
    return 0;
}

// Store an Image as a texture in the registry, returning a handle (0 = failure)
static int StoreTextureFromImage(Image image)
{
    int handle = 0;
    for (int i = 1; i < MAX_LUA_TEXTURES; i++)
    {
        if (luaTextures[i].id == 0)
        {
            luaTextures[i] = LoadTextureFromImage(image);
            if (luaTextures[i].id != 0) handle = i;
            break;
        }
    }
    UnloadImage(image);
    return handle;
}

static int l_genTextureChecked(lua_State *L)
{
    (void)L;
    Image image = GenImageChecked(pd->lua->getArgInt(1), pd->lua->getArgInt(2), pd->lua->getArgInt(3), pd->lua->getArgInt(4), ArgColor(5), ArgColor(8));
    pd->lua->pushInt(StoreTextureFromImage(image));
    return 1;
}

static int l_genTextureGradientLinear(lua_State *L)
{
    (void)L;
    Image image = GenImageGradientLinear(pd->lua->getArgInt(1), pd->lua->getArgInt(2), pd->lua->getArgInt(3), ArgColor(4), ArgColor(7));
    pd->lua->pushInt(StoreTextureFromImage(image));
    return 1;
}

static int l_genTextureGradientRadial(lua_State *L)
{
    (void)L;
    Image image = GenImageGradientRadial(pd->lua->getArgInt(1), pd->lua->getArgInt(2), pd->lua->getArgFloat(3), ArgColor(4), ArgColor(7));
    pd->lua->pushInt(StoreTextureFromImage(image));
    return 1;
}

static int l_genTextureCellular(lua_State *L)
{
    (void)L;
    Image image = GenImageCellular(pd->lua->getArgInt(1), pd->lua->getArgInt(2), pd->lua->getArgInt(3));
    pd->lua->pushInt(StoreTextureFromImage(image));
    return 1;
}

//----------------------------------------------------------------------------------
// Texture drawing variants
//----------------------------------------------------------------------------------
static int l_drawTextureRec(lua_State *L)
{
    (void)L;
    int handle = pd->lua->getArgInt(1);
    if ((handle <= 0) || (handle >= MAX_LUA_TEXTURES)) return 0;

    Rectangle source = { pd->lua->getArgFloat(2), pd->lua->getArgFloat(3), pd->lua->getArgFloat(4), pd->lua->getArgFloat(5) };
    Vector2 position = { pd->lua->getArgFloat(6), pd->lua->getArgFloat(7) };
    Color tint = (pd->lua->getArgCount() >= 10)? ArgColor(8) : WHITE;
    DrawTextureRec(luaTextures[handle], source, position, tint);
    return 0;
}

static int l_drawTexturePro(lua_State *L)
{
    (void)L;
    int handle = pd->lua->getArgInt(1);
    if ((handle <= 0) || (handle >= MAX_LUA_TEXTURES)) return 0;

    Rectangle source = { pd->lua->getArgFloat(2), pd->lua->getArgFloat(3), pd->lua->getArgFloat(4), pd->lua->getArgFloat(5) };
    Rectangle dest = { pd->lua->getArgFloat(6), pd->lua->getArgFloat(7), pd->lua->getArgFloat(8), pd->lua->getArgFloat(9) };
    Vector2 origin = { pd->lua->getArgFloat(10), pd->lua->getArgFloat(11) };
    float rotation = pd->lua->getArgFloat(12);
    Color tint = (pd->lua->getArgCount() >= 15)? ArgColor(13) : WHITE;
    DrawTexturePro(luaTextures[handle], source, dest, origin, rotation, tint);
    return 0;
}

//----------------------------------------------------------------------------------
// Models and billboards
//----------------------------------------------------------------------------------
static int l_loadModel(lua_State *L)
{
    (void)L;
    int handle = 0;

    for (int i = 1; i < MAX_LUA_MODELS; i++)
    {
        if (!luaModelUsed[i])
        {
            luaModels[i] = LoadModel(pd->lua->getArgString(1));
            if (luaModels[i].meshCount > 0)
            {
                luaModelUsed[i] = true;
                handle = i;
            }
            break;
        }
    }

    pd->lua->pushInt(handle);
    return 1;
}

// rl.setModelTexture(model, texture): assign a loaded texture to material 0
static int l_setModelTexture(lua_State *L)
{
    (void)L;
    int model = pd->lua->getArgInt(1);
    int texture = pd->lua->getArgInt(2);
    if ((model <= 0) || (model >= MAX_LUA_MODELS) || !luaModelUsed[model]) return 0;
    if ((texture <= 0) || (texture >= MAX_LUA_TEXTURES)) return 0;

    luaModels[model].materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = luaTextures[texture];
    return 0;
}

// rl.drawModel(model, x, y, z, axisX, axisY, axisZ, angle, scale [, r, g, b])
static int l_drawModel(lua_State *L)
{
    (void)L;
    int handle = pd->lua->getArgInt(1);
    if ((handle <= 0) || (handle >= MAX_LUA_MODELS) || !luaModelUsed[handle]) return 0;

    Vector3 position = { pd->lua->getArgFloat(2), pd->lua->getArgFloat(3), pd->lua->getArgFloat(4) };
    Vector3 axis = { pd->lua->getArgFloat(5), pd->lua->getArgFloat(6), pd->lua->getArgFloat(7) };
    float angle = pd->lua->getArgFloat(8);
    float scale = pd->lua->getArgFloat(9);
    Color tint = (pd->lua->getArgCount() >= 12)? ArgColor(10) : WHITE;
    DrawModelEx(luaModels[handle], position, axis, angle, (Vector3){ scale, scale, scale }, tint);
    return 0;
}

static int l_drawBillboard(lua_State *L)
{
    (void)L;
    int handle = pd->lua->getArgInt(1);
    if ((handle <= 0) || (handle >= MAX_LUA_TEXTURES)) return 0;

    Vector3 position = { pd->lua->getArgFloat(2), pd->lua->getArgFloat(3), pd->lua->getArgFloat(4) };
    Color tint = (pd->lua->getArgCount() >= 8)? ArgColor(6) : WHITE;
    DrawBillboard(luaCamera, luaTextures[handle], position, pd->lua->getArgFloat(5), tint);
    return 0;
}

//----------------------------------------------------------------------------------
// 3D
//----------------------------------------------------------------------------------
static int l_beginMode3D(lua_State *L)
{
    (void)L;
    Camera3D camera = { 0 };
    camera.position = (Vector3){ pd->lua->getArgFloat(1), pd->lua->getArgFloat(2), pd->lua->getArgFloat(3) };
    camera.target = (Vector3){ pd->lua->getArgFloat(4), pd->lua->getArgFloat(5), pd->lua->getArgFloat(6) };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = pd->lua->getArgFloat(7);
    camera.projection = (pd->lua->getArgCount() >= 8)? pd->lua->getArgInt(8) : CAMERA_PERSPECTIVE;
    luaCamera = camera;
    BeginMode3D(camera);
    return 0;
}

static int l_endMode3D(lua_State *L) { (void)L; EndMode3D(); return 0; }

static int l_drawCube(lua_State *L)
{
    (void)L;
    Vector3 position = { pd->lua->getArgFloat(1), pd->lua->getArgFloat(2), pd->lua->getArgFloat(3) };
    DrawCube(position, pd->lua->getArgFloat(4), pd->lua->getArgFloat(5), pd->lua->getArgFloat(6), ArgColor(7));
    return 0;
}

static int l_drawCubeWires(lua_State *L)
{
    (void)L;
    Vector3 position = { pd->lua->getArgFloat(1), pd->lua->getArgFloat(2), pd->lua->getArgFloat(3) };
    DrawCubeWires(position, pd->lua->getArgFloat(4), pd->lua->getArgFloat(5), pd->lua->getArgFloat(6), ArgColor(7));
    return 0;
}

static int l_drawSphere(lua_State *L)
{
    (void)L;
    Vector3 position = { pd->lua->getArgFloat(1), pd->lua->getArgFloat(2), pd->lua->getArgFloat(3) };
    DrawSphere(position, pd->lua->getArgFloat(4), ArgColor(5));
    return 0;
}

static int l_drawSphereWires(lua_State *L)
{
    (void)L;
    Vector3 position = { pd->lua->getArgFloat(1), pd->lua->getArgFloat(2), pd->lua->getArgFloat(3) };
    DrawSphereWires(position, pd->lua->getArgFloat(4), 8, 8, ArgColor(5));
    return 0;
}

static int l_drawCylinder(lua_State *L)
{
    (void)L;
    Vector3 position = { pd->lua->getArgFloat(1), pd->lua->getArgFloat(2), pd->lua->getArgFloat(3) };
    DrawCylinder(position, pd->lua->getArgFloat(4), pd->lua->getArgFloat(5), pd->lua->getArgFloat(6), 12, ArgColor(7));
    return 0;
}

static int l_drawPlane(lua_State *L)
{
    (void)L;
    Vector3 position = { pd->lua->getArgFloat(1), pd->lua->getArgFloat(2), pd->lua->getArgFloat(3) };
    Vector2 size = { pd->lua->getArgFloat(4), pd->lua->getArgFloat(5) };
    DrawPlane(position, size, ArgColor(6));
    return 0;
}

static int l_drawLine3D(lua_State *L)
{
    (void)L;
    Vector3 start = { pd->lua->getArgFloat(1), pd->lua->getArgFloat(2), pd->lua->getArgFloat(3) };
    Vector3 end = { pd->lua->getArgFloat(4), pd->lua->getArgFloat(5), pd->lua->getArgFloat(6) };
    DrawLine3D(start, end, ArgColor(7));
    return 0;
}

static int l_drawGrid(lua_State *L)
{
    (void)L;
    DrawGrid(pd->lua->getArgInt(1), pd->lua->getArgFloat(2));
    return 0;
}

//----------------------------------------------------------------------------------
// Fonts
//----------------------------------------------------------------------------------
// rl.loadFont(path, size) -> handle; supports .ttf/.otf (rasterized at size) and .png/.fnt
static int l_loadFont(lua_State *L)
{
    (void)L;
    int handle = 0;

    for (int i = 1; i < MAX_LUA_FONTS; i++)
    {
        if (luaFonts[i].texture.id == 0)
        {
            const char *path = pd->lua->getArgString(1);
            if (IsFileExtension(path, ".ttf") || IsFileExtension(path, ".otf"))
                luaFonts[i] = LoadFontEx(path, pd->lua->getArgInt(2), NULL, 0);
            else
                luaFonts[i] = LoadFont(path);
            if (luaFonts[i].texture.id != 0) handle = i;
            break;
        }
    }

    pd->lua->pushInt(handle);
    return 1;
}

// rl.drawTextEx(font, text, x, y, size, spacing, r, g, b)
static int l_drawTextEx(lua_State *L)
{
    (void)L;
    int handle = pd->lua->getArgInt(1);
    if ((handle <= 0) || (handle >= MAX_LUA_FONTS) || (luaFonts[handle].texture.id == 0)) return 0;

    Vector2 position = { pd->lua->getArgFloat(3), pd->lua->getArgFloat(4) };
    DrawTextEx(luaFonts[handle], pd->lua->getArgString(2), position, pd->lua->getArgFloat(5), pd->lua->getArgFloat(6), ArgColor(7));
    return 0;
}

// rl.measureTextEx(font, text, size, spacing) -> w, h
static int l_measureTextEx(lua_State *L)
{
    (void)L;
    int handle = pd->lua->getArgInt(1);
    Vector2 size = { 0, 0 };
    if ((handle > 0) && (handle < MAX_LUA_FONTS) && (luaFonts[handle].texture.id != 0))
        size = MeasureTextEx(luaFonts[handle], pd->lua->getArgString(2), pd->lua->getArgFloat(3), pd->lua->getArgFloat(4));
    pd->lua->pushFloat(size.x);
    pd->lua->pushFloat(size.y);
    return 2;
}

//----------------------------------------------------------------------------------
// Images (CPU-side): pixel access, animation frames, mesh generation sources
//----------------------------------------------------------------------------------
static int StoreImage(Image image, int frames)
{
    if (image.data == NULL) return 0;
    for (int i = 1; i < MAX_LUA_IMAGES; i++)
    {
        if (luaImages[i].data == NULL)
        {
            luaImages[i] = image;
            luaImageFrames[i] = frames;
            return i;
        }
    }
    UnloadImage(image);
    return 0;
}

static int l_loadImage(lua_State *L)
{
    (void)L;
    pd->lua->pushInt(StoreImage(LoadImage(pd->lua->getArgString(1)), 1));
    return 1;
}

// rl.loadImageAnim(path) -> handle, frameCount (e.g. animated .gif)
static int l_loadImageAnim(lua_State *L)
{
    (void)L;
    int frames = 0;
    Image image = LoadImageAnim(pd->lua->getArgString(1), &frames);
    int handle = StoreImage(image, (frames > 0)? frames : 1);
    pd->lua->pushInt(handle);
    pd->lua->pushInt((handle != 0)? luaImageFrames[handle] : 0);
    return 2;
}

static int l_imageSize(lua_State *L)
{
    (void)L;
    int handle = pd->lua->getArgInt(1);
    int w = 0, h = 0;
    if ((handle > 0) && (handle < MAX_LUA_IMAGES) && (luaImages[handle].data != NULL))
    {
        w = luaImages[handle].width;
        h = luaImages[handle].height;
    }
    pd->lua->pushInt(w);
    pd->lua->pushInt(h);
    return 2;
}

// rl.getImageColor(image, x, y) -> r, g, b (e.g. cubicmap wall tests)
static int l_getImageColor(lua_State *L)
{
    (void)L;
    int handle = pd->lua->getArgInt(1);
    Color color = { 0, 0, 0, 255 };
    if ((handle > 0) && (handle < MAX_LUA_IMAGES) && (luaImages[handle].data != NULL))
        color = GetImageColor(luaImages[handle], pd->lua->getArgInt(2), pd->lua->getArgInt(3));
    pd->lua->pushInt(color.r);
    pd->lua->pushInt(color.g);
    pd->lua->pushInt(color.b);
    return 3;
}

// rl.textureFromImage(image) -> texture handle (uploads the first frame)
static int l_textureFromImage(lua_State *L)
{
    (void)L;
    int handle = pd->lua->getArgInt(1);
    int texture = 0;
    if ((handle > 0) && (handle < MAX_LUA_IMAGES) && (luaImages[handle].data != NULL))
    {
        for (int i = 1; i < MAX_LUA_TEXTURES; i++)
        {
            if (luaTextures[i].id == 0)
            {
                Image frame = luaImages[handle];    // Shallow view of frame 0 (not unloaded)
                luaTextures[i] = LoadTextureFromImage(frame);
                if (luaTextures[i].id != 0) texture = i;
                break;
            }
        }
    }
    pd->lua->pushInt(texture);
    return 1;
}

// rl.updateTextureAnim(texture, image, frame): upload frame N of an animated image
static int l_updateTextureAnim(lua_State *L)
{
    (void)L;
    int texture = pd->lua->getArgInt(1);
    int image = pd->lua->getArgInt(2);
    int frame = pd->lua->getArgInt(3);
    if ((texture <= 0) || (texture >= MAX_LUA_TEXTURES) || (luaTextures[texture].id == 0)) return 0;
    if ((image <= 0) || (image >= MAX_LUA_IMAGES) || (luaImages[image].data == NULL)) return 0;
    if ((frame < 0) || (frame >= luaImageFrames[image])) return 0;

    Image *img = &luaImages[image];
    int frameSize = GetPixelDataSize(img->width, img->height, img->format);
    UpdateTexture(luaTextures[texture], ((unsigned char *)img->data) + (size_t)frame*frameSize);
    return 0;
}

// rl.genMeshCubicmap(image, cubeSize) -> model handle (white pixels become cubes)
static int l_genMeshCubicmap(lua_State *L)
{
    (void)L;
    int image = pd->lua->getArgInt(1);
    int handle = 0;
    if ((image > 0) && (image < MAX_LUA_IMAGES) && (luaImages[image].data != NULL))
    {
        for (int i = 1; i < MAX_LUA_MODELS; i++)
        {
            if (!luaModelUsed[i])
            {
                float s = pd->lua->getArgFloat(2);
                luaModels[i] = LoadModelFromMesh(GenMeshCubicmap(luaImages[image], (Vector3){ s, s, s }));
                if (luaModels[i].meshCount > 0) { luaModelUsed[i] = true; handle = i; }
                break;
            }
        }
    }
    pd->lua->pushInt(handle);
    return 1;
}

// rl.genMeshHeightmap(image, sizeX, sizeY, sizeZ) -> model handle
static int l_genMeshHeightmap(lua_State *L)
{
    (void)L;
    int image = pd->lua->getArgInt(1);
    int handle = 0;
    if ((image > 0) && (image < MAX_LUA_IMAGES) && (luaImages[image].data != NULL))
    {
        for (int i = 1; i < MAX_LUA_MODELS; i++)
        {
            if (!luaModelUsed[i])
            {
                Vector3 size = { pd->lua->getArgFloat(2), pd->lua->getArgFloat(3), pd->lua->getArgFloat(4) };
                luaModels[i] = LoadModelFromMesh(GenMeshHeightmap(luaImages[image], size));
                if (luaModels[i].meshCount > 0) { luaModelUsed[i] = true; handle = i; }
                break;
            }
        }
    }
    pd->lua->pushInt(handle);
    return 1;
}

//----------------------------------------------------------------------------------
// Render textures
//----------------------------------------------------------------------------------
static int l_loadRenderTexture(lua_State *L)
{
    (void)L;
    int handle = 0;
    for (int i = 1; i < MAX_LUA_RENDERTEXTURES; i++)
    {
        if (luaRenderTextures[i].id == 0)
        {
            luaRenderTextures[i] = LoadRenderTexture(pd->lua->getArgInt(1), pd->lua->getArgInt(2));
            if (luaRenderTextures[i].id != 0) handle = i;
            break;
        }
    }
    pd->lua->pushInt(handle);
    return 1;
}

static int l_beginTextureMode(lua_State *L)
{
    (void)L;
    int handle = pd->lua->getArgInt(1);
    if ((handle > 0) && (handle < MAX_LUA_RENDERTEXTURES) && (luaRenderTextures[handle].id != 0))
        BeginTextureMode(luaRenderTextures[handle]);
    return 0;
}

static int l_endTextureMode(lua_State *L) { (void)L; EndTextureMode(); return 0; }

// rl.drawRenderTexture(rt, dx, dy, dw, dh): draw scaled (handles the GL vertical flip)
static int l_drawRenderTexture(lua_State *L)
{
    (void)L;
    int handle = pd->lua->getArgInt(1);
    if ((handle <= 0) || (handle >= MAX_LUA_RENDERTEXTURES) || (luaRenderTextures[handle].id == 0)) return 0;

    Texture2D tex = luaRenderTextures[handle].texture;
    Rectangle source = { 0.0f, 0.0f, (float)tex.width, -(float)tex.height };
    Rectangle dest = { pd->lua->getArgFloat(2), pd->lua->getArgFloat(3), pd->lua->getArgFloat(4), pd->lua->getArgFloat(5) };
    DrawTexturePro(tex, source, dest, (Vector2){ 0, 0 }, 0.0f, WHITE);
    return 0;
}

//----------------------------------------------------------------------------------
// Audio (pd_raudio: raylib audio API over pd->sound)
//----------------------------------------------------------------------------------
#define MAX_LUA_SOUNDS 16
static Sound luaSounds[MAX_LUA_SOUNDS];

#define MAX_LUA_MUSIC 4
static Music luaMusic[MAX_LUA_MUSIC];

static int l_loadSound(lua_State *L)
{
    (void)L;
    int handle = 0;
    for (int i = 1; i < MAX_LUA_SOUNDS; i++)
    {
        if (!IsSoundValid(luaSounds[i]))
        {
            luaSounds[i] = LoadSound(pd->lua->getArgString(1));
            if (IsSoundValid(luaSounds[i])) handle = i;
            break;
        }
    }
    pd->lua->pushInt(handle);
    return 1;
}

static Sound *ArgSound(void)
{
    int handle = pd->lua->getArgInt(1);
    if ((handle <= 0) || (handle >= MAX_LUA_SOUNDS) || !IsSoundValid(luaSounds[handle])) return NULL;
    return &luaSounds[handle];
}

static int l_playSound(lua_State *L) { (void)L; Sound *s = ArgSound(); if (s) PlaySound(*s); return 0; }
static int l_stopSound(lua_State *L) { (void)L; Sound *s = ArgSound(); if (s) StopSound(*s); return 0; }
static int l_pauseSound(lua_State *L) { (void)L; Sound *s = ArgSound(); if (s) PauseSound(*s); return 0; }
static int l_resumeSound(lua_State *L) { (void)L; Sound *s = ArgSound(); if (s) ResumeSound(*s); return 0; }

static int l_isSoundPlaying(lua_State *L)
{
    (void)L;
    Sound *s = ArgSound();
    pd->lua->pushInt((s && IsSoundPlaying(*s))? 1 : 0);
    return 1;
}

static int l_setSoundVolume(lua_State *L) { (void)L; Sound *s = ArgSound(); if (s) SetSoundVolume(*s, pd->lua->getArgFloat(2)); return 0; }
static int l_setSoundPitch(lua_State *L) { (void)L; Sound *s = ArgSound(); if (s) SetSoundPitch(*s, pd->lua->getArgFloat(2)); return 0; }
static int l_setSoundPan(lua_State *L) { (void)L; Sound *s = ArgSound(); if (s) SetSoundPan(*s, pd->lua->getArgFloat(2)); return 0; }

static int l_loadMusic(lua_State *L)
{
    (void)L;
    int handle = 0;
    for (int i = 1; i < MAX_LUA_MUSIC; i++)
    {
        if (!IsMusicValid(luaMusic[i]))
        {
            luaMusic[i] = LoadMusicStream(pd->lua->getArgString(1));
            if (IsMusicValid(luaMusic[i])) handle = i;
            break;
        }
    }
    pd->lua->pushInt(handle);
    return 1;
}

static Music *ArgMusic(void)
{
    int handle = pd->lua->getArgInt(1);
    if ((handle <= 0) || (handle >= MAX_LUA_MUSIC) || !IsMusicValid(luaMusic[handle])) return NULL;
    return &luaMusic[handle];
}

static int l_playMusic(lua_State *L) { (void)L; Music *m = ArgMusic(); if (m) PlayMusicStream(*m); return 0; }
static int l_stopMusic(lua_State *L) { (void)L; Music *m = ArgMusic(); if (m) StopMusicStream(*m); return 0; }
static int l_pauseMusic(lua_State *L) { (void)L; Music *m = ArgMusic(); if (m) PauseMusicStream(*m); return 0; }
static int l_resumeMusic(lua_State *L) { (void)L; Music *m = ArgMusic(); if (m) ResumeMusicStream(*m); return 0; }

static int l_isMusicPlaying(lua_State *L)
{
    (void)L;
    Music *m = ArgMusic();
    pd->lua->pushInt((m && IsMusicStreamPlaying(*m))? 1 : 0);
    return 1;
}

static int l_setMusicVolume(lua_State *L) { (void)L; Music *m = ArgMusic(); if (m) SetMusicVolume(*m, pd->lua->getArgFloat(2)); return 0; }
static int l_setMusicPitch(lua_State *L) { (void)L; Music *m = ArgMusic(); if (m) SetMusicPitch(*m, pd->lua->getArgFloat(2)); return 0; }
static int l_seekMusic(lua_State *L) { (void)L; Music *m = ArgMusic(); if (m) SeekMusicStream(*m, pd->lua->getArgFloat(2)); return 0; }

static int l_musicLength(lua_State *L)
{
    (void)L;
    Music *m = ArgMusic();
    pd->lua->pushFloat(m? GetMusicTimeLength(*m) : 0.0f);
    return 1;
}

static int l_musicPlayed(lua_State *L)
{
    (void)L;
    Music *m = ArgMusic();
    pd->lua->pushFloat(m? GetMusicTimePlayed(*m) : 0.0f);
    return 1;
}

static int l_setMasterVolume(lua_State *L) { (void)L; SetMasterVolume(pd->lua->getArgFloat(1)); return 0; }

//----------------------------------------------------------------------------------
// Registration
//----------------------------------------------------------------------------------
static const struct { lua_CFunction fn; const char *name; } bindings[] = {
    { l_initWindow,         "rl.initWindow" },
    { l_setTargetFPS,       "rl.setTargetFPS" },
    { l_beginDrawing,       "rl.beginDrawing" },
    { l_endDrawing,         "rl.endDrawing" },
    { l_clearBackground,    "rl.clearBackground" },
    { l_getFrameTime,       "rl.getFrameTime" },
    { l_getTime,            "rl.getTime" },
    { l_getFPS,             "rl.getFPS" },
    { l_drawText,           "rl.drawText" },
    { l_measureText,        "rl.measureText" },
    { l_drawFPS,            "rl.drawFPS" },
    { l_drawPixel,          "rl.drawPixel" },
    { l_drawLine,           "rl.drawLine" },
    { l_drawCircle,         "rl.drawCircle" },
    { l_drawCircleLines,    "rl.drawCircleLines" },
    { l_drawRectangle,      "rl.drawRectangle" },
    { l_drawRectangleLines, "rl.drawRectangleLines" },
    { l_drawTriangle,       "rl.drawTriangle" },
    { l_drawPoly,           "rl.drawPoly" },
    { l_drawPolyLines,      "rl.drawPolyLines" },
    { l_loadTexture,        "rl.loadTexture" },
    { l_unloadTexture,      "rl.unloadTexture" },
    { l_drawTexture,        "rl.drawTexture" },
    { l_drawTextureEx,      "rl.drawTextureEx" },
    { l_textureSize,        "rl.textureSize" },
    { l_beginMode3D,        "rl.beginMode3D" },
    { l_endMode3D,          "rl.endMode3D" },
    { l_drawCube,           "rl.drawCube" },
    { l_drawCubeWires,      "rl.drawCubeWires" },
    { l_drawSphere,         "rl.drawSphere" },
    { l_drawSphereWires,    "rl.drawSphereWires" },
    { l_drawCylinder,       "rl.drawCylinder" },
    { l_drawPlane,          "rl.drawPlane" },
    { l_drawLine3D,         "rl.drawLine3D" },
    { l_drawGrid,           "rl.drawGrid" },
    { l_beginMode2D,        "rl.beginMode2D" },
    { l_endMode2D,          "rl.endMode2D" },
    { l_beginBlendMode,     "rl.beginBlendMode" },
    { l_endBlendMode,       "rl.endBlendMode" },
    { l_pushMatrix,         "rl.pushMatrix" },
    { l_popMatrix,          "rl.popMatrix" },
    { l_translate,          "rl.translate" },
    { l_rotate,             "rl.rotate" },
    { l_getRandomValue,     "rl.getRandomValue" },
    { l_drawRectangleGradientH, "rl.drawRectangleGradientH" },
    { l_drawRectangleGradientV, "rl.drawRectangleGradientV" },
    { l_drawCircleGradient, "rl.drawCircleGradient" },
    { l_genTextureChecked,  "rl.genTextureChecked" },
    { l_genTextureGradientLinear, "rl.genTextureGradientLinear" },
    { l_genTextureGradientRadial, "rl.genTextureGradientRadial" },
    { l_genTextureCellular, "rl.genTextureCellular" },
    { l_drawTextureRec,     "rl.drawTextureRec" },
    { l_drawTexturePro,     "rl.drawTexturePro" },
    { l_loadModel,          "rl.loadModel" },
    { l_setModelTexture,    "rl.setModelTexture" },
    { l_drawModel,          "rl.drawModel" },
    { l_drawBillboard,      "rl.drawBillboard" },
    { l_loadFont,           "rl.loadFont" },
    { l_drawTextEx,         "rl.drawTextEx" },
    { l_measureTextEx,      "rl.measureTextEx" },
    { l_loadImage,          "rl.loadImage" },
    { l_loadImageAnim,      "rl.loadImageAnim" },
    { l_imageSize,          "rl.imageSize" },
    { l_getImageColor,      "rl.getImageColor" },
    { l_textureFromImage,   "rl.textureFromImage" },
    { l_updateTextureAnim,  "rl.updateTextureAnim" },
    { l_genMeshCubicmap,    "rl.genMeshCubicmap" },
    { l_genMeshHeightmap,   "rl.genMeshHeightmap" },
    { l_loadRenderTexture,  "rl.loadRenderTexture" },
    { l_beginTextureMode,   "rl.beginTextureMode" },
    { l_endTextureMode,     "rl.endTextureMode" },
    { l_drawRenderTexture,  "rl.drawRenderTexture" },
    { l_loadSound,          "rl.loadSound" },
    { l_playSound,          "rl.playSound" },
    { l_stopSound,          "rl.stopSound" },
    { l_pauseSound,         "rl.pauseSound" },
    { l_resumeSound,        "rl.resumeSound" },
    { l_isSoundPlaying,     "rl.isSoundPlaying" },
    { l_setSoundVolume,     "rl.setSoundVolume" },
    { l_setSoundPitch,      "rl.setSoundPitch" },
    { l_setSoundPan,        "rl.setSoundPan" },
    { l_loadMusic,          "rl.loadMusic" },
    { l_playMusic,          "rl.playMusic" },
    { l_stopMusic,          "rl.stopMusic" },
    { l_pauseMusic,         "rl.pauseMusic" },
    { l_resumeMusic,        "rl.resumeMusic" },
    { l_isMusicPlaying,     "rl.isMusicPlaying" },
    { l_setMusicVolume,     "rl.setMusicVolume" },
    { l_setMusicPitch,      "rl.setMusicPitch" },
    { l_seekMusic,          "rl.seekMusic" },
    { l_musicLength,        "rl.musicLength" },
    { l_musicPlayed,        "rl.musicPlayed" },
    { l_setMasterVolume,    "rl.setMasterVolume" },
};

// Called by the platform backend on kEventInitLua
void PdRaylibLuaRegister(PlaydateAPI *playdate)
{
    pd = playdate;

    const char *err = NULL;
    for (unsigned int i = 0; i < sizeof(bindings)/sizeof(bindings[0]); i++)
    {
        if (!pd->lua->addFunction(bindings[i].fn, bindings[i].name, &err))
        {
            pd->system->logToConsole("raylib-lua: failed to register %s: %s", bindings[i].name, err);
        }
    }

    pd->system->logToConsole("raylib-lua: registered %i functions in the rl namespace", (int)(sizeof(bindings)/sizeof(bindings[0])));
}
