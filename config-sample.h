#pragma once

//#define STOP_WAVE_OUT_DURING_SILENCE  // Currently only supported by Windows console build. Enabling this will cause issues with some sound drivers.

#define USE_SCANCODES_FOR_LETTER_KEYS  // useful if you use a non-QWERTY keyboard layout

//#define USE_MODULO_LOOKUP_TABLE  // might result in slightly better speed, but makes the EXE larger

//#define FIX_BUGS
//#define EMULATE_LATENT_BUGS
//#define OBJECT_TABLE_BINARY_COMPATIBILITY  // useful for debugging differences between the DOS version and this port, if any are found

//#define CHEAT_OMNISCIENCE
#define CHEAT_OMNISCIENCE_SHOW_NORMAL_VIEWPORT  // only has an effect if CHEAT_OMNISCIENCE is also enabled
//#define CHEAT

// For playback: Wait for a keypress at start, and behave like a live game at end. Meant for screen recording of a played back replay.
//#define PLAYBACK_FOR_SCREEN_RECORDING

// Windows options
#define WINDOWS_PRECISE_TIMER

// SDL options
//#define TILE_WIDTH  27
#define TILE_HEIGHT 36
#define FONT_FILENAME "SnipesConsole.ttf"
//#define FONT_SIZE TILE_HEIGHT

// If CHEAT_OMNISCIENCE_SHOW_NORMAL_VIEWPORT is enabled in the SDL version, this sets the RGBA of the mask used to darken the area outside the standard viewport
#define CHEAT_OMNISCIENCE_RGBA_OUTSIDE_NORMAL_VIEWPORT 0x60, 0x60, 0x60, 0x28
// If CHEAT_OMNISCIENCE_SHOW_NORMAL_VIEWPORT is enabled in the SDL version, and the following is defined, it sets the RGBA of a rectangle drawn around the standard viewport
//#define CHEAT_OMNISCIENCE_RGBA_AROUND_NORMAAL_VIEWPORT 0xC0, 0xC0, 0xC0, 0xFF
