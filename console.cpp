#include <Windows.h>
#include <stdio.h>
#include "config.h"
#include "Snipes.h"
#include "macros.h"

HANDLE input;
HANDLE output;

CONSOLE_CURSOR_INFO cursorInfo;
COORD windowSize;

BOOL WINAPI ConsoleHandlerRoutine(DWORD dwCtrlType)
{
	if (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_BREAK_EVENT)
	{
		forfeit_match = true;
		return TRUE;
	}
	return FALSE;
}

void WriteTextMem(Uint count, WORD row, WORD column, MazeTile *src)
{
	COORD size;
	size.X = count;
	size.Y = 1;
	COORD srcPos;
	srcPos.X = 0;
	srcPos.Y = 0;
	SMALL_RECT rect;
	rect.Left   = column;
	rect.Top    = row;
	rect.Right  = column + count;
	rect.Bottom = row + 1;
	static CHAR_INFO buf[WINDOW_WIDTH];
	for (Uint i=0; i<count; i++)
	{
		buf[i].Char.AsciiChar = src[i].chr;
		buf[i].Attributes     = src[i].color;
#if defined(CHEAT_OMNISCIENCE) && defined(CHEAT_OMNISCIENCE_SHOW_NORMAL_VIEWPORT)
		if (!(inrangex(column+i, WINDOW_WIDTH/2 - 40/2 + 1,
		                         WINDOW_WIDTH/2 + 40/2 + 1) &&
		      inrangex(row, VIEWPORT_ROW + VIEWPORT_HEIGHT/2 - (25 - VIEWPORT_ROW)/2 + 1,
		                    VIEWPORT_ROW + VIEWPORT_HEIGHT/2 + (25 - VIEWPORT_ROW)/2 + 1)) && row >= VIEWPORT_ROW)
			buf[i].Attributes += 0x40;
#endif
	}
	WriteConsoleOutput(output, buf, size, srcPos, &rect);
}

void outputText(BYTE color, WORD count, WORD row, WORD column, const char *src)
{
	COORD pos;
	pos.X = column;
	pos.Y = row;
	SetConsoleCursorPosition(output, pos);
	SetConsoleTextAttribute(output, color);
	DWORD operationSize;
	WriteConsole(output, src, count, &operationSize, 0);
}

void outputNumber(BYTE color, bool zeroPadding, WORD count, WORD row, WORD column, Uint number)
{
	char textbuf[strlength("4294967295")+1];
	sprintf_s(textbuf, sizeof(textbuf), zeroPadding ? "%0*u" : "%*u", count, number);
	outputText(color, count, row, column, textbuf);
}

void EraseBottomTwoLines()
{
	SetConsoleTextAttribute(output, 7);
	COORD pos;
	pos.X = 0;
	for (pos.Y = WINDOW_HEIGHT-2; pos.Y < WINDOW_HEIGHT; pos.Y++)
	{
		SetConsoleCursorPosition(output, pos);
		DWORD operationSize;
		for (Uint i=0; i < WINDOW_WIDTH; i++)
			WriteConsole(output, " ", 1, &operationSize, 0);
	}
}

void CheckForBreak()
{
	DWORD numwritten;
	Sleep(1); // allow ConsoleHandlerRoutine to be triggered
	if (forfeit_match)
		WriteConsole(output, "\r\n", 2, &numwritten, 0);
	got_ctrl_break = forfeit_match;
}

DWORD ReadTextFromConsole(char buffer[], DWORD bufsize)
{
	DWORD numread, numreadWithoutNewline;
	ReadConsole(input, buffer, bufsize, &numread, NULL);
	if (!numread)
	{
		CheckForBreak();
		return numread;
	}

	numreadWithoutNewline = numread;
	if (buffer[numread-1] == '\n')
	{
		numreadWithoutNewline--;
		if (numread > 1 && buffer[numread-2] == '\r')
			numreadWithoutNewline--;
		CheckForBreak();
		return numreadWithoutNewline;
	}
	else
	if (buffer[numread-1] == '\r')
		numreadWithoutNewline--;

	if (buffer[numread-1] != '\n')
	{
		char dummy;
		do
			ReadConsole(input, &dummy, 1, &numread, NULL);
		while (numread && dummy != '\n');
	}
	CheckForBreak();
	return numreadWithoutNewline;
}

void SetConsoleOutputTextColor(WORD wAttributes)
{
	SetConsoleTextAttribute(output, wAttributes);
}

void OpenDirectConsole()
{
	SetConsoleMode(output, 0);
	cursorInfo.bVisible = FALSE;
	SetConsoleCursorInfo(output, &cursorInfo);
}

void CloseDirectConsole(Uint lineNum)
{
	cursorInfo.bVisible = TRUE;
	SetConsoleCursorInfo(output, &cursorInfo);
	COORD pos;
	pos.X = 0;
	pos.Y = lineNum;
	SetConsoleCursorPosition(output, pos);
	SetConsoleTextAttribute(output, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED);
	SetConsoleMode(output, ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT);
}

void ClearConsole()
{
	COORD pos;
	pos.X = 0;
	pos.Y = 0;
	DWORD numwritten;
	FillConsoleOutputAttribute(output, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED, windowSize.X*windowSize.Y, pos, &numwritten);
}

int OpenConsole()
{
	input  = GetStdHandle(STD_INPUT_HANDLE);
	output = GetStdHandle(STD_OUTPUT_HANDLE);

	SetConsoleCP      (437);
	SetConsoleOutputCP(437);
	
	GetConsoleCursorInfo(output, &cursorInfo);

	windowSize.X = WINDOW_WIDTH;
	windowSize.Y = WINDOW_HEIGHT;
	SMALL_RECT window;
	window.Left   = 0;
	window.Top    = 0;
	window.Right  = windowSize.X-1;
	window.Bottom = windowSize.Y-1;
	SetConsoleWindowInfo(output, TRUE, &window);
	SetConsoleScreenBufferSize(output, windowSize);

	SetConsoleCtrlHandler(ConsoleHandlerRoutine, TRUE);

	return 0;
}

void CloseConsole()
{
}
