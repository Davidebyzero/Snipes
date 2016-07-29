#include "../timer.h"

#include <SDL2/SDL.h>

QWORD perf_freq;

WORD GetTickCountWord()
{
	return SDL_GetTicks();
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
