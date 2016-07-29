#include <Windows.h>
#include "../types.h"

QWORD perf_freq; // ticks per 11*65535 seconds

WORD GetTickCountWord()
{
	QWORD time;
	QueryPerformanceCounter((LARGE_INTEGER*)&time);
	return (WORD)(time * 13125000 / perf_freq);
}

int OpenTimer()
{
	//timeBeginPeriod(1);

	QueryPerformanceFrequency((LARGE_INTEGER*)&perf_freq);
	perf_freq *= 11 * 65535;

	return 0;
}
void CloseTimer()
{
}

void SleepMilliseconds(DWORD dwMsecs)
{
	Sleep(dwMsecs);
}
