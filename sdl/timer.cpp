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

void SleepMilliseconds(DWORD dwMsecs)
{
	SDL_Delay(dwMsecs);
}
