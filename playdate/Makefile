# raylib for Playdate — simulator build.
#
# Compiles raylib with PLATFORM_PLAYDATE + the rlsw software renderer into
# per-example pdex.dylib bundles, then packages each as a .pdx with pdc.
#
#   make            build every example .pdx
#   make <name>     build one example (e.g. make core_basic_window)
#   make run-<name> build and open in the Playdate Simulator
#   make clean
#
# Every pdx is universal: simulator dylib + device pdex.bin (untested on hardware).

SDK ?= $(HOME)/Developer/PlaydateSDK
PDC ?= pdc
SIMULATOR ?= $(SDK)/bin/Playdate Simulator.app

RAYLIB_SRC := ../src
BUILD := build

EXAMPLES := core_basic_window \
            core_input_buttons \
            core_input_crank \
            core_2d_camera \
            core_2d_camera_platformer \
            core_3d_camera_mode \
            core_world_screen \
            core_random_values \
            shapes_basic_shapes \
            shapes_logo_raylib_anim \
            shapes_bouncing_ball \
            shapes_easings_ball \
            shapes_colors_palette \
            text_font_default \
            text_sprite_fonts \
            text_writing_anim \
            text_rectangle_bounds \
            textures_image_generation \
            textures_logo_raylib \
            textures_srcrec_dstrec \
            textures_sprite_animation \
            textures_background_scrolling \
            textures_bunnymark \
            textures_blend_modes \
            models_geometric_shapes \
            models_billboard_rendering \
            models_cubicmap_rendering \
            models_orthographic_projection \
            core_basic_screen_manager \
            core_2d_camera_mouse_zoom \
            core_smooth_pixelperfect \
            shapes_collision_area \
            shapes_easings_rectangles \
            textures_gif_player \
            textures_image_drawing \
            textures_image_processing \
            textures_particles_blending \
            text_font_loading \
            text_format_text \
            models_first_person_maze \
            models_heightmap_rendering \
            models_box_collisions \
            models_waving_cubes \
            models_rlgl_solar_system \
            models_loading \
            models_loading_vox \
            models_yaw_pitch_roll \
            core_3d_camera_first_person \
            text_codepoints_loading \
            shapes_easings_box \
            shapes_easings_testbed \
            audio_sound_music \
            raygui_controls_test_suite \
            physac_demo \
            games_maze_raider

# raylib modules compiled in (raudio excluded: Playdate audio goes through pd->sound)
RL_MODULES := rcore rshapes rtextures rtext rmodels pd_raudio

CC := clang

DEFS := -DPLATFORM_PLAYDATE -DGRAPHICS_API_OPENGL_SOFTWARE \
        -DTARGET_SIMULATOR=1 -DTARGET_EXTENSION=1 \
        -DPD_FRAMEDUMP

CFLAGS := -std=gnu11 -g -O2 -fPIC \
    -I$(RAYLIB_SRC) -I$(SDK)/C_API -I. \
    -Wall -Wno-unused-parameter -Wno-unused-but-set-variable \
    -fno-strict-aliasing \
    $(DEFS)

RL_OBJS := $(addprefix $(BUILD)/obj/,$(addsuffix .o,$(RL_MODULES)))

PDXS := $(addsuffix .pdx,$(EXAMPLES))

all: $(PDXS)

$(BUILD)/obj/%.o: $(RAYLIB_SRC)/%.c
	@mkdir -p $(BUILD)/obj
	$(CC) -c $(CFLAGS) $< -o $@

# Extra header dependencies for the core object (platform backend is #included)
$(BUILD)/obj/rcore.o: $(RAYLIB_SRC)/platforms/rcore_playdate.c $(RAYLIB_SRC)/external/rlsw.h $(RAYLIB_SRC)/rlgl.h

$(BUILD)/%/Source/pdex.dylib: examples/%.c $(RL_OBJS) pd_raylib.h
	@mkdir -p $(dir $@)
	$(CC) -dynamiclib -rdynamic $(CFLAGS) -Dmain=rl_playdate_main $< $(RL_OBJS) -o $@

$(BUILD)/%/Source/pdxinfo:
	@mkdir -p $(dir $@)
	@printf 'name=raylib %s\nauthor=plaidate\ndescription=raylib on Playdate test\nbundleID=com.plaidate.raylibpd.%s\nversion=0.1\nbuildNumber=1\n' \
	    "$*" "$(subst _,,$*)" > $@

# ---- device (ARM Cortex-M7) ----
# Flags mirror $(SDK)/C_API/buildsupport/common.mk; the linker script provides
# ENTRY(eventHandlerShim), implemented in the SDK's setup.c

ARM_CC ?= arm-none-eabi-gcc

MCFLAGS := -mthumb -mcpu=cortex-m7 -mfloat-abi=hard -mfpu=fpv5-sp-d16 -D__FPU_USED=1

DEV_DEFS := -DPLATFORM_PLAYDATE -DGRAPHICS_API_OPENGL_SOFTWARE \
            -DTARGET_PLAYDATE=1 -DTARGET_EXTENSION=1 \
            -DPD_FRAMEDUMP

DEV_CFLAGS := -std=gnu11 $(MCFLAGS) -O2 -falign-functions=16 -fomit-frame-pointer \
    -gdwarf-2 -mword-relocations -fno-common -ffunction-sections -fdata-sections \
    -fno-math-errno -fsingle-precision-constant -ffp-contract=fast \
    -I$(RAYLIB_SRC) -I$(SDK)/C_API -I. \
    -Wall -Wno-unused-parameter -Wno-unknown-pragmas \
    -fno-strict-aliasing \
    $(DEV_DEFS)

DEV_OBJS := $(addprefix $(BUILD)/obj-arm/,$(addsuffix .o,$(RL_MODULES))) $(BUILD)/obj-arm/setup.o

$(BUILD)/obj-arm/%.o: $(RAYLIB_SRC)/%.c
	@mkdir -p $(BUILD)/obj-arm
	$(ARM_CC) -c $(DEV_CFLAGS) $< -o $@

$(BUILD)/obj-arm/rcore.o: $(RAYLIB_SRC)/platforms/rcore_playdate.c $(RAYLIB_SRC)/external/rlsw.h $(RAYLIB_SRC)/rlgl.h

# The core object holds the rlsw rasterizer span loops; -O3 unrolls them.
# Other modules stay at -O2 to keep code resident in the F746's 4KB I-cache
$(BUILD)/obj-arm/rcore.o: DEV_CFLAGS += -O3

$(BUILD)/obj-arm/setup.o: $(SDK)/C_API/buildsupport/setup.c
	@mkdir -p $(BUILD)/obj-arm
	$(ARM_CC) -c $(DEV_CFLAGS) $< -o $@

$(BUILD)/%/Source/pdex.elf: examples/%.c $(DEV_OBJS)
	@mkdir -p $(dir $@)
	$(ARM_CC) $(DEV_CFLAGS) -Dmain=rl_playdate_main $< $(DEV_OBJS) \
	    -nostartfiles -T$(SDK)/C_API/buildsupport/link_map.ld \
	    -Wl,-Map=$(BUILD)/$*/pdex.map,--cref,--gc-sections,--no-warn-mismatch,--emit-relocs \
	    -lm -o $@

# Universal pdx: simulator dylib + device binary in one bundle; examples with
# assets have a resources/<name>/ dir copied into the built pdx AFTER pdc runs
# (pdc would compile .png assets into .pdi; raw files in the pdx are readable
# via pd->file, which the platform's LoadFileData callback uses)
%.pdx: $(BUILD)/%/Source/pdex.dylib $(BUILD)/%/Source/pdex.elf $(BUILD)/%/Source/pdxinfo
	@if [ -d resources-src/$* ]; then cp -R resources-src/$*/. $(BUILD)/$*/Source/; fi
	$(PDC) $(BUILD)/$*/Source $@
	@if [ -d resources/$* ]; then cp -R resources/$*/. $@/; fi

# ---- Lua bindings (simulator) ----
# raylib compiled with PD_RAYLIB_LUA: no green thread, playdate.update() in Lua
# drives raylib through the `rl` namespace registered by lua/raylib_lua.c

LUA_CFLAGS := $(filter-out -DPD_FRAMEDUMP,$(CFLAGS)) -DPD_RAYLIB_LUA -DPD_FRAMEDUMP

LUA_OBJS := $(addprefix $(BUILD)/obj-lua/,$(addsuffix .o,$(RL_MODULES))) $(BUILD)/obj-lua/raylib_lua.o

$(BUILD)/obj-lua/%.o: $(RAYLIB_SRC)/%.c
	@mkdir -p $(BUILD)/obj-lua
	$(CC) -c $(LUA_CFLAGS) $< -o $@

$(BUILD)/obj-lua/rcore.o: $(RAYLIB_SRC)/platforms/rcore_playdate.c $(RAYLIB_SRC)/external/rlsw.h $(RAYLIB_SRC)/rlgl.h

$(BUILD)/obj-lua/raylib_lua.o: lua/raylib_lua.c
	@mkdir -p $(BUILD)/obj-lua
	$(CC) -c $(LUA_CFLAGS) $< -o $@

raylib_lua.pdx: $(LUA_OBJS) lua/main.lua
	@mkdir -p $(BUILD)/raylib_lua/Source
	$(CC) -dynamiclib -rdynamic $(LUA_CFLAGS) $(LUA_OBJS) -o $(BUILD)/raylib_lua/Source/pdex.dylib
	@cp lua/main.lua $(BUILD)/raylib_lua/Source/main.lua
	@if [ -d resources-src/raylib_lua ]; then cp -R resources-src/raylib_lua/. $(BUILD)/raylib_lua/Source/; fi
	@printf 'name=raylib lua demos\nauthor=plaidate\ndescription=raylib driven from Playdate Lua\nbundleID=com.plaidate.raylibpd.luademos\nversion=0.1\nbuildNumber=1\n' > $(BUILD)/raylib_lua/Source/pdxinfo
	$(PDC) $(BUILD)/raylib_lua/Source $@
	@if [ -d resources/raylib_lua ]; then cp -R resources/raylib_lua/. $@/; fi

# Shorthand: make core_basic_window
$(EXAMPLES): %: %.pdx

run-%: %.pdx
	open -a "$(SIMULATOR)" $<

clean:
	rm -rf $(BUILD) $(PDXS)

.PHONY: all clean $(EXAMPLES) run-%
