#include <map>
#ifdef __APPLE__
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>
#include "../keyboard.h"
#include "../timer.h"
#include "../Snipes.h"
#include "sdl.h"

std::map<Uint, BYTE> keyState;
#ifdef USE_SCANCODES_FOR_LETTER_KEYS
BYTE keyscanState[SDL_NUM_SCANCODES];
#endif
Uint16 modifierState;
static bool anyKeyPressed = false;

void ClearKeyboard()
{
	keyState.clear();
#ifdef USE_SCANCODES_FOR_LETTER_KEYS
	memset(keyscanState, 0, sizeof(keyscanState));
#endif
	modifierState = 0;
	InputBufferReadIndex = InputBufferWriteIndex = 0;
}

extern bool Paused;

Uint PollKeyboard()
{
	while (Paused)
		SleepTimeslice();

	Uint state = 0;
	if (keyState[SDLK_RIGHT]) state |= KEYSTATE_MOVE_RIGHT;
	if (keyState[SDLK_LEFT ]) state |= KEYSTATE_MOVE_LEFT;
	if (keyState[SDLK_DOWN ]) state |= KEYSTATE_MOVE_DOWN;
	if (keyState[SDLK_UP   ]) state |= KEYSTATE_MOVE_UP;
	if (!(modifierState & KMOD_NUM))
	{
		if (keyState[SDLK_KP_6 ]) state |= KEYSTATE_MOVE_RIGHT;
		if (keyState[SDLK_KP_4 ]) state |= KEYSTATE_MOVE_LEFT;
		if (keyState[SDLK_KP_5 ]) state |= KEYSTATE_MOVE_DOWN; // unfortunately, SDL does not differentiate between NumPad5, and the middle key on a non-inverted-T cursor pad, so this could be either one
		if (keyState[SDLK_KP_2 ]) state |= KEYSTATE_MOVE_DOWN;
		if (keyState[SDLK_KP_8 ]) state |= KEYSTATE_MOVE_UP;
	}
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

void WaitForKeyPress()
{
	anyKeyPressed = false;
	while (!anyKeyPressed)
		SleepTimeslice();
}

void HandleKey(SDL_KeyboardEvent* e)
{
	modifierState = e->keysym.mod;
	if (e->type == SDL_KEYDOWN)
	{
		if (!keyState[e->keysym.sym])
		{
			switch (e->keysym.sym)
			{
			case SDLK_LCTRL:
			case SDLK_LSHIFT:
			case SDLK_LALT:
			case SDLK_LGUI:
			case SDLK_RCTRL:
			case SDLK_RSHIFT:
			case SDLK_RALT:
			case SDLK_RGUI:
				break;
			default:
				anyKeyPressed = true;
				break;
			}
		}

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
		else
		if (e->keysym.sym == SDLK_c || e->keysym.sym == SDLK_SCROLLLOCK) // SDL does not differentiate between Ctrl+ScrollLock and Ctrl+Pause
		{
			if ((e->keysym.mod & (KMOD_LCTRL | KMOD_RCTRL)) && !(e->keysym.mod & (KMOD_LALT | KMOD_RALT)))
				forfeit_match = true;
		}
#ifdef CHEAT
		else
		if (e->keysym.sym == SDLK_t && (e->keysym.mod & (KMOD_LCTRL | KMOD_RCTRL)))
			rerecordingMode = true;
		else
		if (e->keysym.sym == SDLK_PERIOD)
			single_step++;
		else
		if (e->keysym.sym == SDLK_COMMA)
			step_backwards++;
		else
		if (e->keysym.sym == SDLK_KP_PLUS)
		{
			if (frame == 1 && rerecordingMode)
				increment_initial_seed++;
		}
		else
		if (e->keysym.sym == SDLK_KP_MINUS)
		{
			if (frame == 1 && rerecordingMode)
				increment_initial_seed--;
		}
#endif
		else
		if (e->keysym.sym == SDLK_RETURN)
		{
			InputBuffer[InputBufferWriteIndex] = '\n';
			InputBufferWriteIndex = (InputBufferWriteIndex+1) % InputBufferSize;
		}
		else
		if (e->keysym.sym == SDLK_BACKSPACE)
		{
			InputBuffer[InputBufferWriteIndex] = '\b';
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
