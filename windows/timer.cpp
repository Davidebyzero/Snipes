#include <Windows.h>
#include "../types.h"
#include "../config.h"

QWORD perf_freq; // ticks per 11*65535 seconds

WORD GetTickCountWord()
{
	QWORD time;
	QueryPerformanceCounter((LARGE_INTEGER*)&time);
	return (WORD)(time * 13125000 / perf_freq);
}

int OpenTimer()
{
#ifdef WINDOWS_PRECISE_TIMER
	timeBeginPeriod(1);
#endif

	QueryPerformanceFrequency((LARGE_INTEGER*)&perf_freq);
	perf_freq *= 11 * 65535;

	return 0;
}
void CloseTimer()
{
#ifdef WINDOWS_PRECISE_TIMER
	timeEndPeriod(1);
#endif
}

void SleepTimeslice()
{
	Sleep(1);
}
