#pragma once

#include "config.h"
#include "types.h"

extern bool got_ctrl_break;
extern bool forfeit_match;
extern bool sound_enabled;
extern bool shooting_sound_enabled;
extern BYTE spacebar_state;
extern BYTE fast_forward;
#ifdef CHEAT
extern int single_step;
extern int step_backwards;
#endif

#define KEYSTATE_MOVE_RIGHT (1<<0)
#define KEYSTATE_MOVE_LEFT  (1<<1)
#define KEYSTATE_MOVE_DOWN  (1<<2)
#define KEYSTATE_MOVE_UP    (1<<3)
#define KEYSTATE_FIRE_RIGHT (1<<4)
#define KEYSTATE_FIRE_LEFT  (1<<5)
#define KEYSTATE_FIRE_DOWN  (1<<6)
#define KEYSTATE_FIRE_UP    (1<<7)

#define MAZE_CELL_WIDTH  8
#define MAZE_CELL_HEIGHT 6
#define MAZE_WIDTH_IN_CELLS  16
#define MAZE_HEIGHT_IN_CELLS 20
#define MAZE_WIDTH  (MAZE_CELL_WIDTH  * MAZE_WIDTH_IN_CELLS)
#define MAZE_HEIGHT (MAZE_CELL_HEIGHT * MAZE_HEIGHT_IN_CELLS)

#define VIEWPORT_ROW 3

#ifdef CHEAT_OMNISCIENCE
 #define WINDOW_WIDTH  MAZE_WIDTH
 #define WINDOW_HEIGHT (MAZE_HEIGHT + VIEWPORT_ROW)
#else
 #define WINDOW_WIDTH  40
 #define WINDOW_HEIGHT 25
#endif

#define VIEWPORT_HEIGHT (WINDOW_HEIGHT - VIEWPORT_ROW)

struct MazeTile
{
	BYTE chr;
	BYTE color;
	MazeTile() {}
	MazeTile(BYTE color, BYTE chr) : chr(chr), color(color) {}
};
