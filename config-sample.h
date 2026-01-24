#pragma once

//#define STOP_WAVE_OUT_DURING_SILENCE  // Currently only supported by Windows console build. Enabling this will cause issues with some sound drivers.

#define USE_SCANCODES_FOR_LETTER_KEYS  // useful if you use a non-QWERTY keyboard layout

//#define USE_MODULO_LOOKUP_TABLE  // might result in slightly better speed, but makes the EXE larger

//#define FIX_SNIPE_PORTAL_IDENTIFICATION_BUG
//#define FIX_SMALL_SNIPE_LOGIC_COMPILER_BUG
//#define EMULATE_LATENT_BUGS
//#define OBJECT_TABLE_BINARY_COMPATIBILITY  // useful for debugging differences between the DOS version and this port, if any are found

//#define CHEAT_OMNISCIENCE
#define CHEAT_OMNISCIENCE_SHOW_NORMAL_VIEWPORT  // only has an effect if CHEAT_OMNISCIENCE is also enabled
//#define CHEAT

// For playback: Wait for a keypress at start, and behave like a live game at end. Meant for screen recording of a played back replay.
//#define PLAYBACK_FOR_SCREEN_RECORDING

// Store Recording files to a single folder.
// This should ensure playback that the game doesn't develop a __MACOSX problem where there are SnipesGameFiles littered everywhere.
// For Linux: (note, you can't use the ~/SnipesGameFiles naming format)
#define REPLAY_FOLDER "/home/username/SnipesGameFiles"
//
// For Windows:
// #define PLAYBACKFOLDER "C:\SnipesGameFiles"

// Windows options
#define WINDOWS_PRECISE_TIMER

// SDL options
#define FONT_FILENAME "SnipesConsole.ttf"
//#define TILE_WIDTH  27
//#define TILE_HEIGHT 36
//#define FONT_SIZE TILE_HEIGHT
#define ALLOWABLE_BORDER_EXCESS 2 // when tile size is automatically chosen, due to being undefined above, this decides how much overflow (in pixels) beyond the usable desktop area is allowed

// SDL2_ttf line drawing glyph glitch workarounds
#define FONT_FIXUP_JOINING // offset line-drawing corner glyphs if the current font size triggers an off-by-one-pixel misalignment glitch (e.g. "╔═╗" doesn't join up properly)
#define FONT_FIXUP_VERTICALLY // vertically smear the edges of line-drawing characters to make sure they don't leave gaps
//#define FONT_FIXUP_HORIZONTALLY // horizontally smear the edges of line-drawing characters to make sure they don't leave gaps (probably not necessary thanks to TTF_HINTING_MONO)

// If CHEAT_OMNISCIENCE_SHOW_NORMAL_VIEWPORT is enabled in the SDL version, this sets the RGBA of the mask used to darken the area outside the standard viewport
#define CHEAT_OMNISCIENCE_RGBA_OUTSIDE_NORMAL_VIEWPORT 0x60, 0x60, 0x60, 0x28
// If CHEAT_OMNISCIENCE_SHOW_NORMAL_VIEWPORT is enabled in the SDL version, and the following is defined, it sets the RGBA of a rectangle drawn around the standard viewport
//#define CHEAT_OMNISCIENCE_RGBA_AROUND_NORMAAL_VIEWPORT 0xC0, 0xC0, 0xC0, 0xFF
