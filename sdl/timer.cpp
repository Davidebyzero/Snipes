#include "../timer.h"

#ifdef __APPLE__
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>

QWORD perf_freq;

WORD GetTickCountWord()
{
	return (WORD)(SDL_GetTicks() * 13125uLL / (11*65535));
}

int OpenTimer()
{
	return 0;
}
void CloseTimer()
{
}

void SleepTimeslice()
{
	SDL_Delay(1);
}
