#include <stdio.h>
#include <SDL2/SDL.h>
#include "../config.h"
#include "../Snipes.h"
#include "../macros.h"
#include "../console.h"
#include "sdl.h"

#define DEFAULT_TEXT_COLOR 0x7 // (FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED)

// TODO: Handle Ctrl+C / Ctrl+Break

static MazeTile Screen[WINDOW_HEIGHT][WINDOW_WIDTH];

static BYTE OutputTextColor = DEFAULT_TEXT_COLOR;
static Uint OutputCursorX = 0;
static Uint OutputCursorY = 0;
static bool OutputCursorVisible = true;

char InputBuffer[InputBufferSize];
Uint InputBufferReadIndex = 0, InputBufferWriteIndex = 0;

static bool Exiting = false;

#define TILE_WIDTH  16
#define TILE_HEIGHT 16

void WriteTextMem(Uint count, WORD row, WORD column, MazeTile *src)
{
	MazeTile* dst = &Screen[row][column];
	// COORD size;
	// size.X = count;
	// size.Y = 1;
	// COORD srcPos;
	// srcPos.X = 0;
	// srcPos.Y = 0;
	// SMALL_RECT rect;
	// rect.Left   = column;
	// rect.Top    = row;
	// rect.Right  = column + count;
	// rect.Bottom = row + 1;
	// static CHAR_INFO buf[WINDOW_WIDTH];
	for (Uint i=0; i<count; i++)
	{
		if ((size_t)(dst - &Screen[0][0]) >= WINDOW_WIDTH*WINDOW_HEIGHT)
			break; // Out of bounds
		MazeTile tile = src[i];
#ifdef CHEAT
		if (single_step>=0 && tile.chr==0xFF)
			tile.chr = 0xF0;
#endif
#if defined(CHEAT_OMNISCIENCE) && defined(CHEAT_OMNISCIENCE_SHOW_NORMAL_VIEWPORT)
		if (!(inrangex(column+i, WINDOW_WIDTH/2 - 40/2,
		                         WINDOW_WIDTH/2 + 40/2) &&
		      inrangex(row, VIEWPORT_ROW + VIEWPORT_HEIGHT/2 - (25 - VIEWPORT_ROW)/2 + 1,
		                    VIEWPORT_ROW + VIEWPORT_HEIGHT/2 + (25 - VIEWPORT_ROW)/2 + 1)) && row >= VIEWPORT_ROW)
			tile.color += 0x40;
#endif
		*dst++ = tile;
	}
}

void outputText(BYTE color, WORD count, WORD row, WORD column, const char *src)
{
	MazeTile* dst = &Screen[row][column];
	for (Uint i=0; i<count; i++)
	{
		if ((size_t)(dst - &Screen[0][0]) >= WINDOW_WIDTH*WINDOW_HEIGHT)
			break; // Out of bounds
		*dst++ = MazeTile(color, src[i]);
	}
}

void outputNumber(BYTE color, bool zeroPadding, WORD count, WORD row, WORD column, Uint number)
{
	char textbuf[strlength("4294967295")+1];
	snprintf(textbuf, sizeof(textbuf), zeroPadding ? "%0*u" : "%*u", count, number);
	outputText(color, count, row, column, textbuf);
}

void EraseBottomTwoLines()
{
	for (Uint y = WINDOW_HEIGHT-2; y < WINDOW_HEIGHT; y++)
		for (Uint x = 0; x < WINDOW_WIDTH; x++)
			Screen[y][x] = MazeTile(7, ' ');
}

void CheckForBreak()
{
	// SDL_Delay(1); // allow ConsoleHandlerRoutine to be triggered
	// if (forfeit_match)
	// 	WriteConsole(output, "\r\n", 2, &numwritten, 0);
	got_ctrl_break = forfeit_match;
}

DWORD ReadTextFromConsole(char buffer[], DWORD bufsize)
{
	// TODO: Semaphores?
	SDL_StartTextInput();

	DWORD numread = 0;
	while (numread < bufsize)
	{
		while (InputBufferReadIndex == InputBufferWriteIndex)
			SDL_Delay(1);
		char c = InputBuffer[InputBufferReadIndex];
		InputBufferReadIndex = (InputBufferReadIndex+1) % InputBufferSize;
		if (c == '\n')
			break;
		WriteTextToConsole(&c, 1);;
		buffer[numread++] = c;
	}

	SDL_StopTextInput();
	return numread;
}

void SetConsoleOutputTextColor(WORD wAttributes)
{
	OutputTextColor = (BYTE)wAttributes;
}

void WriteTextToConsole(char const *text, size_t length)
{
	for (Uint n=0; n<length; n++)
		switch (text[n])
		{
			case '\r':
				OutputCursorX = 0;
				break;
			case '\n':
				OutputCursorY++;
				break;
			default:
				outputText(OutputTextColor, 1, OutputCursorY, OutputCursorX, text+n);
				OutputCursorX++;
				// TODO: wrap?
				break;
		}
}

void OpenDirectConsole()
{
	OutputCursorVisible = false;
}

void CloseDirectConsole(Uint lineNum)
{
	OutputCursorVisible = true;
	OutputCursorX = 0;
	OutputCursorY = lineNum;
	OutputTextColor = DEFAULT_TEXT_COLOR;
}

void ClearConsole()
{
	OutputCursorX = 0;
	OutputCursorY = 0;
	OutputTextColor = DEFAULT_TEXT_COLOR;
	
	for (Uint y = 0; y < WINDOW_HEIGHT; y++)
		for (Uint x = 0; x < WINDOW_WIDTH; x++)
			Screen[y][x].color = DEFAULT_TEXT_COLOR;
}

void HandleKey(SDL_KeyboardEvent* e);

static int ConsoleThreadFunc(void* ptr)
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
	{
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return 1;
	}

	SDL_Window *win = SDL_CreateWindow("Snipes", 100, 100, WINDOW_WIDTH * TILE_WIDTH, WINDOW_HEIGHT * TILE_HEIGHT , SDL_WINDOW_SHOWN);
	if (!win)
	{
		fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
		SDL_Quit();
		return 1;
	}

	SDL_Renderer *ren = SDL_CreateRenderer(win, -1, 0);
	if (!ren)
	{
		SDL_DestroyWindow(win);
		fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
		SDL_Quit();
		return 1;
	}

	while (!Exiting)
	{
		SDL_Event e;
		while (SDL_PollEvent(&e))
		{
			switch (e.type)
			{
				case SDL_QUIT:
					Exiting = true;
					break;
				case SDL_KEYDOWN:
				case SDL_KEYUP:
					HandleKey((SDL_KeyboardEvent*)&e);
					break;
				case SDL_TEXTINPUT:
					/* Add new text onto the end of our text */
					for (const char* s = e.text.text; *s; s++)
					{
						InputBuffer[InputBufferWriteIndex] = *s;
						InputBufferWriteIndex = (InputBufferWriteIndex+1) % InputBufferSize;
					}
					break;
			}
		}

		SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
		SDL_RenderClear(ren);

		for (Uint y = 0; y < WINDOW_HEIGHT; y++)
			for (Uint x = 0; x < WINDOW_WIDTH; x++)
			{
				SDL_SetRenderDrawColor(ren, Screen[y][x].color*16, Screen[y][x].chr/16*16, Screen[y][x].chr%16*16, 255);
				SDL_Rect rect = { x * TILE_WIDTH, y * TILE_HEIGHT, TILE_WIDTH, TILE_HEIGHT };
				SDL_RenderFillRect(ren, &rect);
			}

		SDL_RenderPresent(ren);
	}

	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	SDL_Quit();

	return 0;
}

static SDL_Thread *ConsoleThread;

int OpenConsole()
{
	Exiting = false;

	ConsoleThread = SDL_CreateThread(ConsoleThreadFunc, "ConsoleThread", NULL);
	if (!ConsoleThread)
	{
		fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
		SDL_Quit();
		return 1;
	}

	return 0;
}

void CloseConsole()
{
	Exiting = true;
	SDL_WaitThread(ConsoleThread, NULL);
}
