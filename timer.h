#pragma once

#include "types.h"

WORD GetTickCountWord();

int  OpenTimer();
void CloseTimer();

void SleepMilliseconds(DWORD dwMsecs);
