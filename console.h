#pragma once

#include <Windows.h>
#include "types.h"
#include "macros.h"

void WriteTextMem(Uint count, WORD row, WORD column, MazeTile *src);

void outputText(BYTE color, WORD count, WORD row, WORD column, const char *src);
void outputNumber(BYTE color, bool zeroPadding, WORD count, WORD row, WORD column, Uint number);
void EraseBottomTwoLines();

DWORD ReadTextFromConsole(char buffer[], DWORD bufsize);

void SetConsoleOutputTextColor(WORD wAttributes);
template <size_t LENGTH>
void WriteTextToConsole(char const (&text)[LENGTH])
{
	extern HANDLE output;
	DWORD numwritten;
	WriteConsole(output, text, strlength(text), &numwritten, 0);
}

void OpenDirectConsole();
void CloseDirectConsole(Uint lineNum);

void ClearConsole();

int OpenConsole();
void CloseConsole();
