-- raylib on Playdate, from Lua: demo suite + performance harness.
--
-- Lua ports of the C examples, driven through the `rl` bindings. Starts in
-- auto-cycle mode (3.5s per scene) recording average frame time per scene;
-- after the last scene it writes Data/<bundle>/perf.json and shows the menu.
-- d-pad + A picks a scene manually (disables auto), B returns to the menu.

local pd <const> = playdate

rl.initWindow(400, 240)

-- assets ---------------------------------------------------------------------
local texLogo = rl.loadTexture("resources/raylib_logo.png")
local texScarfy = rl.loadTexture("resources/scarfy.png")
local texBunny = rl.loadTexture("resources/raybunny.png")
local texBill = rl.loadTexture("resources/billboard.png")
local texBgBack = rl.loadTexture("resources/cyberpunk_street_background.png")
local texBgMid = rl.loadTexture("resources/cyberpunk_street_midground.png")
local texBgFore = rl.loadTexture("resources/cyberpunk_street_foreground.png")
local mdlCastle = rl.loadModel("resources/models/obj/castle.obj")
local texCastle = rl.loadTexture("resources/models/obj/castle_diffuse.png")
if mdlCastle > 0 and texCastle > 0 then rl.setModelTexture(mdlCastle, texCastle) end

local genChecked = rl.genTextureChecked(110, 90, 16, 16, 255, 255, 255, 0, 0, 0)
local genLinear = rl.genTextureGradientLinear(110, 90, 0, 255, 255, 255, 0, 0, 0)
local genRadial = rl.genTextureGradientRadial(110, 90, 0.0, 255, 255, 255, 0, 0, 0)
local genCellular = rl.genTextureCellular(110, 90, 24)

local scarfyW, scarfyH = rl.textureSize(texScarfy)
local frameW = scarfyW/6

-- helpers ---------------------------------------------------------------------
local function btn(b) return pd.buttonIsPressed(b) end
local function btnp(b) return pd.buttonJustPressed(b) end
local sin, cos, rad, floor = math.sin, math.cos, math.rad, math.floor

local scenes = {}
local function scene(name, update) scenes[#scenes + 1] = { name = name, update = update } end

-- 1 core: basic window
scene("core_basic_window", function()
    rl.drawText("Congrats! You created", 70, 90, 20, 0, 0, 0)
    rl.drawText("your first Lua window!", 70, 115, 20, 0, 0, 0)
    rl.drawRectangleLines(10, 20, 380, 210, 130, 130, 130)
end)

-- 2 core: buttons
local ballX, ballY = 200, 130
scene("core_input_buttons", function()
    if btn(pd.kButtonRight) then ballX += 3 end
    if btn(pd.kButtonLeft) then ballX -= 3 end
    if btn(pd.kButtonDown) then ballY += 3 end
    if btn(pd.kButtonUp) then ballY -= 3 end
    if btnp(pd.kButtonA) then ballX, ballY = 200, 130 end
    rl.drawText("move the ball with the d-pad", 100, 20, 10, 100, 100, 100)
    rl.drawCircle(ballX, ballY, 24, 190, 33, 55)
end)

-- 3 core: crank
scene("core_input_crank", function()
    local a = rad(pd.getCrankPosition())
    local cx, cy, r = 200, 135, 70
    rl.drawText("turn the crank", 150, 20, 10, 100, 100, 100)
    rl.drawCircleLines(cx, cy, r, 0, 0, 0)
    for i = 0, 7 do
        local t = rad(i*45)
        rl.drawLine(cx + sin(t)*(r - 6), cy - cos(t)*(r - 6), cx + sin(t)*r, cy - cos(t)*r, 1, 0, 0, 0)
    end
    rl.drawLine(cx, cy, cx + sin(a)*(r - 10), cy - cos(a)*(r - 10), 3, 190, 33, 55)
    rl.drawText(string.format("%3d deg", floor(pd.getCrankPosition())), 180, 220, 10, 0, 0, 0)
end)

-- 4 core: 2d camera (buildings + follow player)
local buildings = {}
do
    local x = -1000
    while x < 1000 do
        local w = rl.getRandomValue(40, 100)
        local h = rl.getRandomValue(60, 170)
        buildings[#buildings + 1] = { x = x, w = w, h = h, g = rl.getRandomValue(120, 220) }
        x += w
    end
end
local camPlayerX = 0
scene("core_2d_camera", function()
    if btn(pd.kButtonRight) then camPlayerX += 3 end
    if btn(pd.kButtonLeft) then camPlayerX -= 3 end
    rl.beginMode2D(camPlayerX, 120, 200, 120, 0, 1)
        rl.drawRectangle(-1000, 200, 2000, 40, 80, 80, 80)
        for i = 1, #buildings do
            local b = buildings[i]
            rl.drawRectangle(b.x, 200 - b.h, b.w - 4, b.h, b.g, b.g, b.g)
        end
        rl.drawRectangle(camPlayerX - 10, 170, 20, 30, 190, 33, 55)
    rl.endMode2D()
    rl.drawText("d-pad left/right moves the player", 90, 20, 10, 100, 100, 100)
end)

-- 5 core: 3d camera mode
scene("core_3d_camera_mode", function()
    rl.beginMode3D(0, 10, 10, 0, 0, 0, 45)
        rl.drawCube(0, 0, 0, 2, 2, 2, 190, 33, 55)
        rl.drawCubeWires(0, 0, 0, 2, 2, 2, 0, 0, 0)
        rl.drawGrid(10, 1)
    rl.endMode3D()
    rl.drawText("Welcome to the third dimension!", 80, 30, 10, 100, 100, 100)
end)

-- 6 core: 3d first person (manual camera math)
local fpX, fpZ, fpYaw = 0, 4, 180
local columns = {}
for i = 1, 20 do
    columns[i] = { x = rl.getRandomValue(-15, 15), z = rl.getRandomValue(-15, 15),
                   h = rl.getRandomValue(1, 12), g = rl.getRandomValue(80, 220) }
end
scene("core_3d_first_person", function()
    fpYaw += pd.getCrankChange()*0.5
    if btn(pd.kButtonRight) then fpYaw += 2 end
    if btn(pd.kButtonLeft) then fpYaw -= 2 end
    local dirX, dirZ = sin(rad(fpYaw)), cos(rad(fpYaw))
    if btn(pd.kButtonUp) then fpX += dirX*0.2; fpZ += dirZ*0.2 end
    if btn(pd.kButtonDown) then fpX -= dirX*0.2; fpZ -= dirZ*0.2 end
    rl.beginMode3D(fpX, 1.7, fpZ, fpX + dirX, 1.7, fpZ + dirZ, 60)
        rl.drawPlane(0, 0, 0, 32, 32, 200, 200, 200)
        for i = 1, #columns do
            local c = columns[i]
            rl.drawCube(c.x, c.h/2, c.z, 2, c.h, 2, c.g, c.g, c.g)
            rl.drawCubeWires(c.x, c.h/2, c.z, 2, c.h, 2, 60, 60, 60)
        end
    rl.endMode3D()
    rl.drawText("d-pad walks/turns, crank turns", 90, 20, 10, 100, 100, 100)
end)

-- 7 core: random values
local randValue, randFrames = 0, 0
scene("core_random_values", function()
    randFrames += 1
    if randFrames%100 == 0 then randValue = rl.getRandomValue(-8, 5) end
    rl.drawText("Every 2 seconds a new random value is generated:", 10, 80, 10, 128, 0, 0)
    rl.drawText(string.format("%d", randValue), 170, 110, 80, 130, 130, 130)
end)

-- 8 shapes: basic shapes
local shapesRot = 0
scene("shapes_basic_shapes", function()
    shapesRot += 0.4
    rl.drawText("some basic shapes available on raylib", 40, 20, 10, 100, 100, 100)
    rl.drawCircle(80, 70, 25, 0, 82, 172)
    rl.drawCircleGradient(80, 140, 30, 0, 228, 48, 102, 191, 255)
    rl.drawCircleLines(80, 205, 30, 0, 82, 172)
    rl.drawRectangle(130, 45, 60, 50, 230, 41, 55)
    rl.drawRectangleGradientH(115, 115, 90, 30, 190, 33, 55, 255, 203, 0)
    rl.drawRectangleLines(140, 160, 40, 60, 255, 161, 0)
    rl.drawTriangle(240, 45, 210, 95, 270, 95, 135, 60, 190)
    rl.drawPoly(320, 80, 6, 40, shapesRot, 127, 106, 79)
    rl.drawPolyLines(320, 175, 6, 40, -shapesRot, 3, 211, 176, 131)
end)

-- 9 shapes: bouncing ball
local bbX, bbY, bbVX, bbVY = 200, 100, 5, 4
scene("shapes_bouncing_ball", function()
    bbX += bbVX; bbY += bbVY
    if bbX >= 380 or bbX <= 20 then bbVX = -bbVX end
    if bbY >= 220 or bbY <= 30 then bbVY = -bbVY end
    rl.drawCircle(bbX, bbY, 20, 128, 0, 0)
    rl.drawText("bouncing ball", 160, 20, 10, 100, 100, 100)
end)

-- 10 shapes: colors palette
local palette = {
    {"GRAY",130,130,130},{"YELLOW",253,249,0},{"GOLD",255,203,0},{"ORANGE",255,161,0},
    {"PINK",255,109,194},{"RED",230,41,55},{"MAROON",190,33,55},{"GREEN",0,228,48},
    {"LIME",0,158,47},{"DKGREEN",0,117,44},{"SKYBLUE",102,191,255},{"BLUE",0,121,241},
    {"DKBLUE",0,82,172},{"PURPLE",200,122,255},{"VIOLET",135,60,190},{"DKPURPLE",112,31,126},
    {"BEIGE",211,176,131},{"BROWN",127,106,79},{"DKBROWN",76,63,47},{"WHITE",255,255,255},
    {"BLACK",0,0,0},
}
scene("shapes_colors_palette", function()
    rl.drawText("raylib colors palette", 20, 20, 20, 0, 0, 0)
    for i = 0, #palette - 1 do
        local c = palette[i + 1]
        local x, y = 13 + 55*(i%7), 55 + 55*floor(i/7)
        rl.drawRectangle(x, y, 50, 40, c[2], c[3], c[4])
        rl.drawText(c[1], x + 2, y + 42, 10, 80, 80, 80)
    end
end)

-- 11 shapes: easings ball (elastic-out)
local easeT = 0
local function elasticOut(t, b, c, d)
    if t == 0 then return b end
    t = t/d
    if t >= 1 then return b + c end
    local p = d*0.3
    local s = p/4
    return c*(2^(-10*t))*sin((t*d - s)*(2*math.pi)/p) + c + b
end
scene("shapes_easings_ball", function()
    easeT += 1
    if easeT > 180 then easeT = 0 end
    local x = elasticOut(math.min(easeT, 120), 40, 300, 120)
    rl.drawCircle(x, 120, 22, 190, 33, 55)
    rl.drawText("elastic-out easing (reasings port)", 90, 20, 10, 100, 100, 100)
end)

-- 12 shapes: collision area
local caAY, caAVY = 60, 3
local caBX, caBY = 250, 150
scene("shapes_collision_area", function()
    caAY += caAVY
    if caAY <= 40 or caAY >= 180 then caAVY = -caAVY end
    if btn(pd.kButtonRight) then caBX += 4 end
    if btn(pd.kButtonLeft) then caBX -= 4 end
    if btn(pd.kButtonDown) then caBY += 4 end
    if btn(pd.kButtonUp) then caBY -= 4 end
    local ax, ay, aw, ah = 40, caAY, 150, 60
    local bx, by, bw, bh = caBX, caBY, 90, 60
    rl.drawRectangle(ax, ay, aw, ah, 255, 161, 0)
    rl.drawRectangle(bx, by, bw, bh, 0, 121, 241)
    local ix, iy = math.max(ax, bx), math.max(ay, by)
    local ix2, iy2 = math.min(ax + aw, bx + bw), math.min(ay + ah, by + bh)
    if ix2 > ix and iy2 > iy then
        rl.drawRectangle(ix, iy, ix2 - ix, iy2 - iy, 230, 41, 55)
        rl.drawText(string.format("COLLISION! area: %d", (ix2 - ix)*(iy2 - iy)), 120, 20, 10, 0, 0, 0)
    else
        rl.drawText("d-pad moves the blue box", 120, 20, 10, 100, 100, 100)
    end
end)

-- 13 shapes: raylib logo animation (simplified state machine)
local logoT = 0
scene("shapes_logo_raylib_anim", function()
    logoT += 1
    if logoT > 300 then logoT = 0 end
    local cx, cy, size = 200, 120, 180
    local grow = math.min(logoT/60, 1)*size
    rl.drawRectangle(floor(cx - size/2), floor(cy - size/2), floor(grow), 14, 0, 0, 0)
    rl.drawRectangle(floor(cx - size/2), floor(cy - size/2), 14, floor(grow), 0, 0, 0)
    if logoT > 60 then
        local g2 = math.min((logoT - 60)/60, 1)*size
        rl.drawRectangle(floor(cx + size/2 - 14), floor(cy - size/2), 14, floor(g2), 0, 0, 0)
        rl.drawRectangle(floor(cx - size/2), floor(cy + size/2 - 14), floor(g2), 14, 0, 0, 0)
    end
    if logoT > 150 then
        rl.drawText("raylib", cx + size//2 - 78, cy + size//2 - 40, 20, 0, 0, 0)
        rl.drawText("from Lua", cx + size//2 - 78, cy + size//2 - 64, 10, 100, 100, 100)
    end
end)

-- 14 text: default font
scene("text_font_default", function()
    rl.drawText("raylib default font on Playdate", 20, 30, 20, 0, 0, 0)
    rl.drawText("size 10: the quick brown fox", 20, 70, 10, 80, 80, 80)
    rl.drawText("size 20: jumps over", 20, 90, 20, 80, 80, 80)
    rl.drawText("size 30: the lazy dog", 20, 120, 30, 130, 130, 130)
    rl.drawText("size 40: 0123456789", 20, 160, 40, 0, 0, 0)
end)

-- 15 text: format text
local score, hiscore, lives = 100020, 200450, 5
scene("text_format_text", function()
    rl.drawText(string.format("Score: %08d", score), 80, 60, 20, 190, 33, 55)
    rl.drawText(string.format("HiScore: %08d", hiscore), 80, 100, 20, 0, 117, 44)
    rl.drawText(string.format("Lives: %02d", lives), 80, 140, 30, 0, 82, 172)
    rl.drawText(string.format("Elapsed time: %.2f ms", rl.getFrameTime()*1000), 80, 190, 10, 0, 0, 0)
end)

-- 16 text: writing anim
local writeMsg = "This sample illustrates a text writing animation effect! Check it out! ;)"
local writeFrames = 0
scene("text_writing_anim", function()
    writeFrames += 1
    if btnp(pd.kButtonA) then writeFrames = 0 end
    local n = math.min(floor(writeFrames/4), #writeMsg)
    rl.drawText(string.sub(writeMsg, 1, n), 30, 80, 20, 0, 0, 0)
    rl.drawText("A to restart!", 150, 220, 10, 100, 100, 100)
end)

-- 17 textures: image generation
scene("textures_image_generation", function()
    local names = { "CHECKED", "LINEAR", "RADIAL", "CELLULAR" }
    local texs = { genChecked, genLinear, genRadial, genCellular }
    for i = 0, 3 do
        local x, y = 45 + (i%2)*170, 15 + floor(i/2)*110
        rl.drawTexture(texs[i + 1], x, y)
        rl.drawRectangleLines(x, y, 110, 90, 0, 0, 0)
        rl.drawText(names[i + 1], x + 5, y + 93, 10, 80, 80, 80)
    end
end)

-- 18 textures: raylib logo
scene("textures_logo_raylib", function()
    local w, h = rl.textureSize(texLogo)
    rl.drawTexture(texLogo, 200 - w//2, 120 - h//2)
    rl.drawText("this IS a texture!", 300, 210, 10, 100, 100, 100)
end)

-- 19 textures: srcrec/dstrec
local srcRot = 0
scene("textures_srcrec_dstrec", function()
    srcRot += 1
    rl.drawLine(200, 0, 200, 240, 1, 130, 130, 130)
    rl.drawLine(0, 120, 400, 120, 1, 130, 130, 130)
    rl.drawTexturePro(texScarfy, 0, 0, frameW, scarfyH,
                      200, 120, frameW*1.5, scarfyH*1.5, frameW*0.75, scarfyH*0.75, srcRot)
end)

-- 20 textures: sprite animation
local animFrame, animCounter = 0, 0
scene("textures_sprite_animation", function()
    animCounter += 1
    if animCounter >= 6 then
        animCounter = 0
        animFrame = (animFrame + 1)%6
    end
    rl.drawTexturePro(texScarfy, 0, 0, scarfyW, scarfyH, 20, 30, scarfyW/2, scarfyH/2, 0, 0, 0)
    rl.drawRectangleLines(20 + floor(animFrame*frameW/2), 30, floor(frameW/2), floor(scarfyH/2), 230, 41, 55)
    rl.drawTextureRec(texScarfy, animFrame*frameW, 0, frameW, scarfyH, 168, 120)
end)

-- 21 textures: background scrolling
local scrollB, scrollM, scrollF = 0, 0, 0
scene("textures_background_scrolling", function()
    local bw = rl.textureSize(texBgBack)
    scrollB -= 0.1; scrollM -= 0.5; scrollF -= 1.0
    if scrollB <= -bw then scrollB = 0 end
    if scrollM <= -bw then scrollM = 0 end
    if scrollF <= -bw then scrollF = 0 end
    rl.drawTextureEx(texBgBack, scrollB, 24, 0, 1)
    rl.drawTextureEx(texBgBack, scrollB + bw, 24, 0, 1)
    rl.drawTextureEx(texBgMid, scrollM, 24, 0, 1)
    rl.drawTextureEx(texBgMid, scrollM + bw, 24, 0, 1)
    rl.drawTextureEx(texBgFore, scrollF, 24, 0, 1)
    rl.drawTextureEx(texBgFore, scrollF + bw, 24, 0, 1)
    rl.drawText("BACKGROUND SCROLLING & PARALLAX", 60, 20, 10, 230, 41, 55)
end)

-- 22 textures: blend modes
local blendMode = 0
local blendNames = { "ALPHA", "ADDITIVE", "MULTIPLIED", "ADD COLORS", "SUBTRACT COLORS" }
scene("textures_blend_modes", function()
    if btnp(pd.kButtonA) then blendMode = (blendMode + 1)%5 end
    rl.drawTexture(texBgBack, -56, 24)
    rl.beginBlendMode(blendMode)
    rl.drawTexture(texBgFore, -152, 24)
    rl.endBlendMode()
    rl.drawText("A to cycle: " .. blendNames[blendMode + 1], 130, 224, 10, 0, 0, 0)
end)

-- 23 textures: bunnymark (perf probe)
local bunnies = {}
for i = 1, 500 do
    bunnies[i] = { x = rl.getRandomValue(20, 380), y = rl.getRandomValue(20, 220),
                   vx = rl.getRandomValue(-250, 250)/60, vy = rl.getRandomValue(-250, 250)/60 }
end
scene("textures_bunnymark", function()
    if btn(pd.kButtonA) then
        for i = 1, 50 do
            bunnies[#bunnies + 1] = { x = 200, y = 120,
                vx = rl.getRandomValue(-250, 250)/60, vy = rl.getRandomValue(-250, 250)/60 }
        end
    end
    for i = 1, #bunnies do
        local b = bunnies[i]
        b.x += b.vx; b.y += b.vy
        if b.x < 0 or b.x > 384 then b.vx = -b.vx end
        if b.y < 0 or b.y > 208 then b.vy = -b.vy end
        rl.drawTexture(texBunny, floor(b.x), floor(b.y))
    end
    rl.drawRectangle(0, 0, 400, 18, 0, 0, 0)
    rl.drawText(string.format("bunnies: %d (A for more)", #bunnies), 120, 4, 10, 255, 255, 255)
end)

-- 24 models: geometric shapes
local geoOrbit = 45
scene("models_geometric_shapes", function()
    if pd.isCrankDocked() then geoOrbit += 0.5 else geoOrbit += pd.getCrankChange() end
    local a = rad(geoOrbit)
    rl.beginMode3D(sin(a)*11, 6, cos(a)*11, 0, 0.5, 0, 45)
        rl.drawCube(-3, 0.5, 0, 1, 1, 1, 230, 41, 55)
        rl.drawCubeWires(-3, 0.5, 0, 1, 1, 1, 0, 0, 0)
        rl.drawSphere(0, 1, 0, 1, 0, 82, 172)
        rl.drawSphereWires(0, 1, 0, 1, 0, 0, 0)
        rl.drawCylinder(3, 0, 0, 0.7, 0.7, 2, 190, 33, 55)
        rl.drawGrid(10, 1)
    rl.endMode3D()
end)

-- 25 models: billboard
local billOrbit = 0
scene("models_billboard", function()
    billOrbit += 0.4
    local a = rad(billOrbit)
    rl.beginMode3D(sin(a)*6, 4, cos(a)*6, 0, 2, 0, 45)
        rl.drawGrid(10, 1)
        rl.drawBillboard(texBill, 0, 2, 0, 2)
    rl.endMode3D()
end)

-- 26 models: castle.obj
local castleOrbit = 0
scene("models_loading_castle", function()
    castleOrbit += 0.3
    local a = rad(castleOrbit)
    rl.beginMode3D(sin(a)*55, 35, cos(a)*55, 0, 8, 0, 45)
        if mdlCastle > 0 then rl.drawModel(mdlCastle, 0, 0, 0, 0, 1, 0, 0, 1) end
        rl.drawGrid(12, 5)
    rl.endMode3D()
end)

-- 27 models: waving cubes (perf probe: 225 cubes)
scene("models_waving_cubes", function()
    local t = rl.getTime()
    rl.beginMode3D(sin(t*0.4)*22, 14, cos(t*0.4)*22, 0, 0, 0, 60)
        rl.drawGrid(15, 1.6)
        for x = 0, 14 do
            for z = 0, 14 do
                local h = 2 + 1.6*sin(t*3 + (x + z)*0.4)
                local g = 60 + (x + z)*6
                rl.drawCube((x - 7)*1.6, h/2, (z - 7)*1.6, 1.2, h, 1.2, g, g, g)
            end
        end
    rl.endMode3D()
end)

-- 28 models: rlgl solar system (matrix stack)
local sunRot, earthOrbit, moonOrbit = 0, 0, 0
scene("models_rlgl_solar_system", function()
    sunRot += 0.4; earthOrbit += 1.0; moonOrbit += 4.0
    rl.beginMode3D(6, 6, 6, 0, 0, 0, 45)
        rl.pushMatrix()
            rl.rotate(sunRot, 0, 1, 0)
            rl.drawSphere(0, 0, 0, 1.2, 253, 249, 0)
        rl.popMatrix()
        rl.pushMatrix()
            rl.rotate(earthOrbit, 0, 1, 0)
            rl.translate(3.5, 0, 0)
            rl.drawSphere(0, 0, 0, 0.4, 0, 121, 241)
            rl.rotate(moonOrbit, 0, 1, 0)
            rl.translate(0.9, 0, 0)
            rl.drawSphere(0, 0, 0, 0.15, 130, 130, 130)
        rl.popMatrix()
        rl.drawGrid(8, 1)
    rl.endMode3D()
end)

-- 29 models: orthographic projection toggle
local orthoOn = false
scene("models_orthographic", function()
    if btnp(pd.kButtonA) then orthoOn = not orthoOn end
    local fovy = orthoOn and 15 or 45
    local proj = orthoOn and 1 or 0
    rl.beginMode3D(8, 6, 8, 0, 0.5, 0, fovy, proj)
        rl.drawCube(-3, 0.5, 0, 1, 1, 1, 230, 41, 55)
        rl.drawSphere(0, 1, 0, 1, 0, 82, 172)
        rl.drawCylinder(3, 0, 0, 0.7, 0.7, 2, 190, 33, 55)
        rl.drawGrid(10, 1)
    rl.endMode3D()
    rl.drawText(orthoOn and "ORTHOGRAPHIC (A toggles)" or "PERSPECTIVE (A toggles)", 120, 20, 10, 0, 0, 0)
end)


-- 30 text: font loading (TTF rasterized via stb_truetype)
local fontTtf = rl.loadFont("resources/pixantiqua.ttf", 32)
scene("text_font_loading", function()
    rl.drawText("Default raylib font (size 20)", 20, 50, 20, 0, 0, 0)
    if fontTtf > 0 then
        rl.drawTextEx(fontTtf, "PixAntiqua TTF rasterized at 32px", 20, 100, 32, 1, 0, 0, 0)
        rl.drawTextEx(fontTtf, "loaded with rl.loadFont!", 20, 140, 32, 1, 130, 130, 130)
    else
        rl.drawText("font failed to load", 20, 100, 20, 190, 33, 55)
    end
end)

-- 31 textures: gif player (LoadImageAnim + UpdateTexture)
local gifImage, gifFrames = rl.loadImageAnim("resources/scarfy_run.gif")
local gifTex = (gifImage > 0) and rl.textureFromImage(gifImage) or 0
local gifFrame, gifCounter = 0, 0
scene("textures_gif_player", function()
    if gifTex > 0 then
        gifCounter += 1
        if gifCounter >= 6 then
            gifCounter = 0
            gifFrame = (gifFrame + 1)%gifFrames
            rl.updateTextureAnim(gifTex, gifImage, gifFrame)
        end
        local w, h = rl.textureSize(gifTex)
        rl.drawTexture(gifTex, 200 - w//2, 120 - h//2)
        rl.drawText(string.format("GIF frame %d/%d", gifFrame + 1, gifFrames), 160, 210, 10, 0, 0, 0)
    end
end)

-- 32 models: cubicmap (image -> mesh)
local cubicImg = rl.loadImage("resources/cubicmap.png")
local cubicModel = (cubicImg > 0) and rl.genMeshCubicmap(cubicImg, 1) or 0
local cubicAtlas = rl.loadTexture("resources/cubicmap_atlas.png")
if cubicModel > 0 and cubicAtlas > 0 then rl.setModelTexture(cubicModel, cubicAtlas) end
local cubicOrbit = 0
scene("models_cubicmap", function()
    cubicOrbit += 0.4
    local a = rad(cubicOrbit)
    rl.beginMode3D(sin(a)*24, 15, cos(a)*24, 0, 0, 0, 45)
        if cubicModel > 0 then rl.drawModel(cubicModel, -16, 0, -8, 0, 1, 0, 0, 1) end
    rl.endMode3D()
    rl.drawText("GenMeshCubicmap from an image", 100, 20, 10, 100, 100, 100)
end)

-- 33 models: heightmap (image -> mesh)
local hmImg = rl.loadImage("resources/heightmap.png")
local hmModel = (hmImg > 0) and rl.genMeshHeightmap(hmImg, 16, 6, 16) or 0
local hmTex = rl.loadTexture("resources/heightmap.png")
if hmModel > 0 and hmTex > 0 then rl.setModelTexture(hmModel, hmTex) end
local hmOrbit = 0
scene("models_heightmap", function()
    hmOrbit += 0.4
    local a = rad(hmOrbit)
    rl.beginMode3D(sin(a)*20, 13, cos(a)*20, 0, 2, 0, 45)
        if hmModel > 0 then rl.drawModel(hmModel, -8, 0, -8, 0, 1, 0, 0, 1) end
        rl.drawGrid(16, 1)
    rl.endMode3D()
    rl.drawText("GenMeshHeightmap from an image", 100, 20, 10, 100, 100, 100)
end)

-- 34 models: first person maze with pixel collision (getImageColor)
local mzX, mzZ, mzYaw = 2.5, 2.5, 135
scene("models_first_person_maze", function()
    mzYaw += pd.getCrankChange()*0.5
    if btn(pd.kButtonRight) then mzYaw += 2 end
    if btn(pd.kButtonLeft) then mzYaw -= 2 end
    local dx, dz = sin(rad(mzYaw)), cos(rad(mzYaw))
    local nx, nz = mzX, mzZ
    if btn(pd.kButtonUp) then nx += dx*0.1; nz += dz*0.1 end
    if btn(pd.kButtonDown) then nx -= dx*0.1; nz -= dz*0.1 end
    local r = rl.getImageColor(cubicImg, floor(nx), floor(nz))
    if r < 128 then mzX, mzZ = nx, nz end       -- white pixels are walls
    rl.beginMode3D(mzX, 0.5, mzZ, mzX + dx, 0.5, mzZ + dz, 60)
        if cubicModel > 0 then rl.drawModel(cubicModel, 0, 0, 0, 0, 1, 0, 0, 1) end
    rl.endMode3D()
    rl.drawText("maze w/ pixel collision: d-pad + crank", 90, 20, 10, 100, 100, 100)
end)

-- 35 models: MagicaVoxel
local voxKnight = rl.loadModel("resources/models/vox/chr_knight.vox")
local voxFez = rl.loadModel("resources/models/vox/fez.vox")
local voxSel, voxOrbit = 0, 0
scene("models_loading_vox", function()
    if btnp(pd.kButtonA) then voxSel = 1 - voxSel end
    voxOrbit += 0.5
    local a = rad(voxOrbit)
    local m = (voxSel == 0) and voxKnight or voxFez
    rl.beginMode3D(sin(a)*30, 20, cos(a)*30, 0, 4, 0, 45)
        if m > 0 then rl.drawModel(m, 0, 0, 0, 0, 1, 0, 0, 1) end
        rl.drawGrid(10, 2)
    rl.endMode3D()
    rl.drawText(voxSel == 0 and "chr_knight.vox (A swaps)" or "fez.vox (A swaps)", 130, 20, 10, 0, 0, 0)
end)

-- 36 core: smooth pixel perfect (render texture upscale)
local rtWorld = rl.loadRenderTexture(160, 90)
local ppRot = 0
scene("core_smooth_pixelperfect", function()
    ppRot += 1
    if rtWorld > 0 then
        rl.beginTextureMode(rtWorld)
            rl.clearBackground(245, 245, 245)
            rl.drawPoly(50, 45, 4, 22, ppRot, 230, 41, 55)
            rl.drawPoly(110, 45, 4, 16, -ppRot*1.5, 0, 121, 241)
        rl.endTextureMode()
        rl.drawRenderTexture(rtWorld, 0, 0, 400, 225)
    end
    rl.drawText("160x90 render texture upscaled 2.5x", 90, 228, 10, 0, 0, 0)
end)


-- 37 audio: sounds + streamed music over pd->sound (pd_raudio)
local sndCoin = rl.loadSound("resources/coin.wav")
local musCountry = rl.loadMusic("resources/country.mp3")
local musicStarted = false
local beepTimer = 0
scene("audio_sound_music", function()
    if not musicStarted and musCountry > 0 then rl.playMusic(musCountry); musicStarted = true end
    beepTimer += rl.getFrameTime()
    if beepTimer > 2 then
        beepTimer = 0
        if sndCoin > 0 then rl.playSound(sndCoin) end
    end
    if btnp(pd.kButtonA) and sndCoin > 0 then rl.playSound(sndCoin) end

    rl.drawText("raudio over pd->sound, from Lua", 90, 50, 10, 0, 0, 0)
    local played, len = rl.musicPlayed(musCountry), rl.musicLength(musCountry)
    rl.drawText(string.format("music %s   %.1f / %.1f s",
        (rl.isMusicPlaying(musCountry) == 1) and "PLAYING" or "stopped", played, len), 90, 85, 10, 0, 0, 0)
    rl.drawRectangleLines(50, 105, 300, 14, 0, 0, 0)
    if len > 0 then rl.drawRectangle(50, 105, floor(300*played/len), 14, 80, 80, 80) end
    rl.drawText(string.format("coin: %s", (rl.isSoundPlaying(sndCoin) == 1) and "ON" or "--"), 90, 135, 10, 80, 80, 80)
    rl.drawText("A: coin (also auto every 2s)", 110, 165, 10, 130, 130, 130)
end)

-- harness ---------------------------------------------------------------------
local current = 0           -- 0 = menu
local menuSel = 1
local auto = true           -- auto-cycle + perf capture until any button press
local autoTimer = 0
local autoIdx = 0
local perf = {}             -- name -> { frames, ms }
local perfWritten = false

local function enterScene(i)
    current = i
    autoTimer = 0
end

local function menuUpdate()
    rl.drawText("raylib-lua demo suite", 20, 8, 20, 0, 0, 0)
    rl.drawText("d-pad + A to run a scene, B returns", 20, 32, 10, 100, 100, 100)
    for i = 1, #scenes do
        local col = (i <= 15) and 0 or 1
        local row = (i - 1)%15
        local x, y = 20 + col*195, 48 + row*12
        if i == menuSel then rl.drawRectangle(x - 4, y - 1, 190, 12, 200, 200, 200) end
        rl.drawText(string.format("%2d %s", i, scenes[i].name), x, y, 10, 0, 0, 0)
    end
    if btnp(pd.kButtonDown) then menuSel = menuSel%#scenes + 1 end
    if btnp(pd.kButtonUp) then menuSel = (menuSel - 2)%#scenes + 1 end
    if btnp(pd.kButtonRight) then menuSel = math.min(menuSel + 15, #scenes) end
    if btnp(pd.kButtonLeft) then menuSel = math.max(menuSel - 15, 1) end
    if btnp(pd.kButtonA) then enterScene(menuSel) end
end

local function writePerf()
    local out = {}
    for name, p in pairs(perf) do
        if p.frames > 0 then
            out[name] = { avg_ms = p.ms/p.frames, fps = 1000/(p.ms/p.frames), frames = p.frames }
        end
    end
    pd.datastore.write(out, "perf")
    print("raylib-lua: wrote perf.json")
end

function playdate.update()
    -- any button press cancels auto mode
    if auto and (btnp(pd.kButtonA) or btnp(pd.kButtonB) or btnp(pd.kButtonUp) or
                 btnp(pd.kButtonDown) or btnp(pd.kButtonLeft) or btnp(pd.kButtonRight)) then
        auto = false
        current = 0
    end

    if auto then
        autoTimer += rl.getFrameTime()
        if current == 0 then
            autoIdx += 1
            if autoIdx > #scenes then
                auto = false
                if not perfWritten then perfWritten = true; writePerf() end
            else
                enterScene(autoIdx)
            end
        elseif autoTimer > 3.5 then
            current = 0
        end
    end

    rl.beginDrawing()
    rl.clearBackground(245, 245, 245)

    if current == 0 then
        if not auto then menuUpdate() end
    else
        local s = scenes[current]
        s.update()
        local p = perf[s.name]
        if not p then p = { frames = 0, ms = 0, skip = 10 }; perf[s.name] = p end
        if p.skip > 0 then p.skip -= 1
        else p.frames += 1; p.ms += rl.getFrameTime()*1000 end
        rl.drawText(s.name, 6, 231, 10, 130, 130, 130)
        rl.drawFPS(352, 228)
        if not auto and btnp(pd.kButtonB) then current = 0 end
    end

    rl.endDrawing()
end
