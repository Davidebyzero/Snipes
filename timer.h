#pragma once

#include "types.h"

WORD GetTickCountWord();

int  OpenTimer();
void CloseTimer();

void SleepTimeslice();
