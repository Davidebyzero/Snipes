#include <Windows.h>
#include "../config.h"
#include "../Snipes.h"
#include "../types.h"
#include "../keyboard.h"

extern HANDLE input;

BYTE keyState[0x100];
#ifdef USE_SCANCODES_FOR_LETTER_KEYS
BYTE keyscanState[0x100];
#endif

void ClearKeyboard()
{
	memset(keyState, 0, sizeof(keyState));
#ifdef USE_SCANCODES_FOR_LETTER_KEYS
	memset(keyscanState, 0, sizeof(keyscanState));
#endif
}
Uint PollKeyboard()
{
	for (;;)
	{
		DWORD numread = 0;
		INPUT_RECORD record;
		if (!PeekConsoleInput(input, &record, 1, &numread))
			break;
		if (!numread)
			break;
		ReadConsoleInput(input, &record, 1, &numread);
		if (!numread)
			break;
		if (record.EventType == KEY_EVENT)
		{
			if (record.Event.KeyEvent.bKeyDown)
			{
				if (record.Event.KeyEvent.wVirtualKeyCode < _countof(keyState))
					keyState[record.Event.KeyEvent.wVirtualKeyCode] |= record.Event.KeyEvent.dwControlKeyState & ENHANCED_KEY ? 2 : 1;
#ifdef USE_SCANCODES_FOR_LETTER_KEYS
				if (record.Event.KeyEvent.wVirtualScanCode < _countof(keyscanState))
					keyscanState[record.Event.KeyEvent.wVirtualScanCode] |= record.Event.KeyEvent.dwControlKeyState & ENHANCED_KEY ? 2 : 1;
#endif

				if (record.Event.KeyEvent.wVirtualKeyCode == VK_F1)
					sound_enabled ^= true;
				else
				if (record.Event.KeyEvent.wVirtualKeyCode == VK_F2)
					shooting_sound_enabled ^= true;
				else
				if (record.Event.KeyEvent.wVirtualKeyCode == VK_ESCAPE)
					forfeit_match = true;
#ifdef CHEAT
				else
				if (record.Event.KeyEvent.wVirtualKeyCode == VK_OEM_PERIOD)
					single_step++;
				else
				if (record.Event.KeyEvent.wVirtualKeyCode == VK_OEM_COMMA)
					step_backwards++;
				else
				if (record.Event.KeyEvent.wVirtualKeyCode == 0x6B)
				{
					if (frame == 0)
						increment_initial_seed++;
				}
				else
				if (record.Event.KeyEvent.wVirtualKeyCode == 0x6D)
				{
					if (frame == 0)
						increment_initial_seed--;
				}
#endif
			}
			else
			{
				if (record.Event.KeyEvent.wVirtualKeyCode < _countof(keyState))
					keyState[record.Event.KeyEvent.wVirtualKeyCode] &= record.Event.KeyEvent.dwControlKeyState & ENHANCED_KEY ? ~2 : ~1;
#ifdef USE_SCANCODES_FOR_LETTER_KEYS
				if (record.Event.KeyEvent.wVirtualScanCode < _countof(keyscanState))
					keyscanState[record.Event.KeyEvent.wVirtualScanCode] &= record.Event.KeyEvent.dwControlKeyState & ENHANCED_KEY ? ~2 : ~1;
#endif
			}
		}
	}
	Uint state = 0;
	if (keyState[VK_RIGHT]) state |= KEYSTATE_MOVE_RIGHT;
	if (keyState[VK_LEFT ]) state |= KEYSTATE_MOVE_LEFT;
	if (keyState[VK_DOWN ]) state |= KEYSTATE_MOVE_DOWN;
	if (keyState[VK_CLEAR]) state |= KEYSTATE_MOVE_DOWN; // center key on numeric keypad with NumLock off
	if (keyState[0xFF    ]) state |= KEYSTATE_MOVE_DOWN; // center key on cursor pad, on non-inverted-T keyboards
	if (keyState[VK_UP   ]) state |= KEYSTATE_MOVE_UP;
#ifndef USE_SCANCODES_FOR_LETTER_KEYS
	if (keyState['D'     ]) state |= KEYSTATE_FIRE_RIGHT;
	if (keyState['A'     ]) state |= KEYSTATE_FIRE_LEFT;
	if (keyState['S'     ]) state |= KEYSTATE_FIRE_DOWN;
	if (keyState['X'     ]) state |= KEYSTATE_FIRE_DOWN;
	if (keyState['W'     ]) state |= KEYSTATE_FIRE_UP;
#else
	if (keyscanState[0x20]) state |= KEYSTATE_FIRE_RIGHT;
	if (keyscanState[0x1E]) state |= KEYSTATE_FIRE_LEFT;
	if (keyscanState[0x1F]) state |= KEYSTATE_FIRE_DOWN;
	if (keyscanState[0x2D]) state |= KEYSTATE_FIRE_DOWN;
	if (keyscanState[0x11]) state |= KEYSTATE_FIRE_UP;
#endif
	spacebar_state = keyState[VK_SPACE];
	if ((state & (KEYSTATE_MOVE_RIGHT | KEYSTATE_MOVE_LEFT)) == (KEYSTATE_MOVE_RIGHT | KEYSTATE_MOVE_LEFT) ||
		(state & (KEYSTATE_MOVE_DOWN  | KEYSTATE_MOVE_UP  )) == (KEYSTATE_MOVE_DOWN  | KEYSTATE_MOVE_UP  ))
		state &= ~(KEYSTATE_MOVE_RIGHT | KEYSTATE_MOVE_LEFT | KEYSTATE_MOVE_DOWN | KEYSTATE_MOVE_UP);
	if ((state & (KEYSTATE_FIRE_RIGHT | KEYSTATE_FIRE_LEFT)) == (KEYSTATE_FIRE_RIGHT | KEYSTATE_FIRE_LEFT) ||
		(state & (KEYSTATE_FIRE_DOWN  | KEYSTATE_FIRE_UP  )) == (KEYSTATE_FIRE_DOWN  | KEYSTATE_FIRE_UP  ))
		state &= ~(KEYSTATE_FIRE_RIGHT | KEYSTATE_FIRE_LEFT | KEYSTATE_FIRE_DOWN | KEYSTATE_FIRE_UP);
	fast_forward = keyState['F'];
	return state;
}

void WaitForKeyPress()
{
	// TODO: implement this
}

int OpenKeyboard()
{
	ClearKeyboard();
	return 0;
}
void CloseKeyboard()
{
}
