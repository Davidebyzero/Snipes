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

// Windows options
#define WINDOWS_PRECISE_TIMER

// SDL options
//#define TILE_WIDTH  27
#define TILE_HEIGHT 36
#define FONT_FILENAME "SnipesConsole.ttf"
//#define FONT_SIZE TILE_HEIGHT
