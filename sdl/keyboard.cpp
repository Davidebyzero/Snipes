#include <map>
#include <SDL2/SDL.h>
#include "../keyboard.h"
#include "../Snipes.h"
#include "sdl.h"

std::map<Uint, BYTE> keyState;
std::map<Uint, BYTE> keyscanState;

void ClearKeyboard()
{
	keyState.clear();
#ifdef USE_SCANCODES_FOR_LETTER_KEYS
	keyscanState.clear();
#endif
	InputBufferReadIndex = InputBufferWriteIndex = 0;
}

Uint PollKeyboard()
{
	Uint state = 0;
	if (keyState[SDLK_RIGHT]) state |= KEYSTATE_MOVE_RIGHT;
	if (keyState[SDLK_LEFT ]) state |= KEYSTATE_MOVE_LEFT;
	if (keyState[SDLK_DOWN ]) state |= KEYSTATE_MOVE_DOWN;
	if (keyState[SDLK_CLEAR]) state |= KEYSTATE_MOVE_DOWN; // center key on numeric keypad with NumLock off
//	if (keyState[0xFF              ]) state |= KEYSTATE_MOVE_DOWN; // center key on cursor pad, on non-inverted-T keyboards
	if (keyState[SDLK_UP   ]) state |= KEYSTATE_MOVE_UP;
#ifndef USE_SCANCODES_FOR_LETTER_KEYS
	if (keyState[SDLK_d    ]) state |= KEYSTATE_FIRE_RIGHT;
	if (keyState[SDLK_a    ]) state |= KEYSTATE_FIRE_LEFT;
	if (keyState[SDLK_s    ]) state |= KEYSTATE_FIRE_DOWN;
	if (keyState[SDLK_x    ]) state |= KEYSTATE_FIRE_DOWN;
	if (keyState[SDLK_w    ]) state |= KEYSTATE_FIRE_UP;
#else
	if (keyscanState[SDL_SCANCODE_D    ]) state |= KEYSTATE_FIRE_RIGHT;
	if (keyscanState[SDL_SCANCODE_A    ]) state |= KEYSTATE_FIRE_LEFT;
	if (keyscanState[SDL_SCANCODE_S    ]) state |= KEYSTATE_FIRE_DOWN;
	if (keyscanState[SDL_SCANCODE_X    ]) state |= KEYSTATE_FIRE_DOWN;
	if (keyscanState[SDL_SCANCODE_W    ]) state |= KEYSTATE_FIRE_UP;
#endif
	spacebar_state = keyState[SDLK_SPACE];
	if ((state & (KEYSTATE_MOVE_RIGHT | KEYSTATE_MOVE_LEFT)) == (KEYSTATE_MOVE_RIGHT | KEYSTATE_MOVE_LEFT) ||
		(state & (KEYSTATE_MOVE_DOWN  | KEYSTATE_MOVE_UP  )) == (KEYSTATE_MOVE_DOWN  | KEYSTATE_MOVE_UP  ))
		state &= ~(KEYSTATE_MOVE_RIGHT | KEYSTATE_MOVE_LEFT | KEYSTATE_MOVE_DOWN | KEYSTATE_MOVE_UP);
	if ((state & (KEYSTATE_FIRE_RIGHT | KEYSTATE_FIRE_LEFT)) == (KEYSTATE_FIRE_RIGHT | KEYSTATE_FIRE_LEFT) ||
		(state & (KEYSTATE_FIRE_DOWN  | KEYSTATE_FIRE_UP  )) == (KEYSTATE_FIRE_DOWN  | KEYSTATE_FIRE_UP  ))
		state &= ~(KEYSTATE_FIRE_RIGHT | KEYSTATE_FIRE_LEFT | KEYSTATE_FIRE_DOWN | KEYSTATE_FIRE_UP);
	fast_forward = keyState[SDLK_f];
	return state;
}

void HandleKey(SDL_KeyboardEvent* e)
{
	if (e->type == SDL_KEYDOWN)
	{
		keyState[e->keysym.sym] |= 1;
#ifdef USE_SCANCODES_FOR_LETTER_KEYS
		keyscanState[e->keysym.scancode] |= 1;
#endif

		if (e->keysym.sym == SDLK_F1)
			sound_enabled ^= true;
		else
		if (e->keysym.sym == SDLK_F2)
			shooting_sound_enabled ^= true;
		else
		if (e->keysym.sym == SDLK_ESCAPE)
			forfeit_match = true;
#ifdef CHEAT
		else
		if (e->keysym.sym == SDLK_PERIOD)
			single_step++;
		else
		if (e->keysym.sym == SDLK_COMMA)
			step_backwards++;
#endif
		else
		if (e->keysym.sym == SDLK_RETURN)
		{
			InputBuffer[InputBufferWriteIndex] = '\n';
			InputBufferWriteIndex = (InputBufferWriteIndex+1) % InputBufferSize;
		}

	}
	else
	{
		keyState[e->keysym.sym] &= ~1;
#ifdef USE_SCANCODES_FOR_LETTER_KEYS
		keyscanState[e->keysym.scancode] &= ~1;
#endif
	}
}

int OpenKeyboard()
{
	ClearKeyboard();
	return 0;
}
void CloseKeyboard()
{
}
