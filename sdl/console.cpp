#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "../config.h"
#include "../Snipes.h"
#include "../macros.h"
#include "../console.h"
#include "../platform.h"
#include "../timer.h"
#include "sdl.h"

#define DEFAULT_TEXT_COLOR 0x7 // (FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED)

// TODO: Handle Ctrl+C / Ctrl+Break

static MazeTile Screen[WINDOW_HEIGHT][WINDOW_WIDTH];
static bool ScreenChanged = true;

static BYTE OutputTextColor = DEFAULT_TEXT_COLOR;
static Uint OutputCursorX = 0;
static Uint OutputCursorY = 0;
static bool OutputCursorVisible = true;
bool Paused = false;

char InputBuffer[InputBufferSize];
Uint InputBufferReadIndex = 0, InputBufferWriteIndex = 0;

static bool Exiting = false;

void WriteTextMem(Uint count, WORD row, WORD column, MazeTile *src)
{
	MazeTile* dst = &Screen[row][column];
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
	ScreenChanged = true;
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
	ScreenChanged = true;
}

void outputNumber(BYTE color, bool zeroPadding, WORD count, WORD row, WORD column, Uint number)
{
	char textbuf[strlength("4294967295")+1];
	sprintf(textbuf, zeroPadding ? "%0*u" : "%*u", count, number);
	outputText(color, count, row, column, textbuf);
}

void EraseBottomTwoLines()
{
	for (Uint y = WINDOW_HEIGHT-2; y < WINDOW_HEIGHT; y++)
		for (Uint x = 0; x < WINDOW_WIDTH; x++)
			Screen[y][x] = MazeTile(7, ' ');
	ScreenChanged = true;
}

void CheckForBreak()
{
	// SleepTimeslice(); // allow ConsoleHandlerRoutine to be triggered
	// if (forfeit_match)
	// 	WriteConsole(output, "\r\n", 2, &numwritten, 0);
	got_ctrl_break = forfeit_match;
}

DWORD ReadTextFromConsole(char buffer[], DWORD bufsize)
{
	// TODO: Semaphores?
	SDL_StartTextInput();

	DWORD numread = 0;
	while (true)
	{
		while (InputBufferReadIndex == InputBufferWriteIndex)
		{
			CheckForBreak();
			if (forfeit_match)
				goto done;
			SleepTimeslice();
		}
		char c = InputBuffer[InputBufferReadIndex];
		InputBufferReadIndex = (InputBufferReadIndex+1) % InputBufferSize;
		if (c == '\n')
		{
			WriteTextToConsole("\r\n", 2);;
			break;
		}
		else
		if (c == '\b') // Backspace
		{
			if (numread)
			{
				WriteTextToConsole(&c, 1);;
				numread--;
			}
		}
		else
		{
			if (numread < bufsize)
			{
				WriteTextToConsole(&c, 1);;
				buffer[numread] = c;
				numread++;
			}
		}
	}

done:
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
	{
		switch (text[n])
		{
			case '\r':
				OutputCursorX = 0;
				break;
			case '\n':
				OutputCursorY++;
				break;
			case '\b':
				if (OutputCursorX)
				{
					OutputCursorX--;
					outputText(OutputTextColor, 1, OutputCursorY, OutputCursorX, " ");
				}
				break;
			default:
				outputText(OutputTextColor, 1, OutputCursorY, OutputCursorX, text+n);
				OutputCursorX++;
				// TODO: wrap?
				break;
		}

		if (OutputCursorY == WINDOW_HEIGHT) // Scroll down
		{
			memmove(&Screen[0], &Screen[1], sizeof(Screen[0]) * (WINDOW_HEIGHT-1));
			OutputCursorY--;
		}
	}
	ScreenChanged = true;
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
	ScreenChanged = true;
}

void HandleKey(SDL_KeyboardEvent* e);

static const SDL_Color ScreenColors[16] =
{
	{ 0x00, 0x00, 0x00, 0xFF },
	{ 0x00, 0x00, 0xAA, 0xFF },
	{ 0x00, 0xAA, 0x00, 0xFF },
	{ 0x00, 0xAA, 0xAA, 0xFF },
	{ 0xAA, 0x00, 0x00, 0xFF },
	{ 0xAA, 0x00, 0xAA, 0xFF },
	{ 0xAA, 0x55, 0x00, 0xFF },
	{ 0xAA, 0xAA, 0xAA, 0xFF },
	{ 0x55, 0x55, 0x55, 0xFF },
	{ 0x55, 0x55, 0xFF, 0xFF },
	{ 0x55, 0xFF, 0x55, 0xFF },
	{ 0x55, 0xFF, 0xFF, 0xFF },
	{ 0xFF, 0x55, 0x55, 0xFF },
	{ 0xFF, 0x55, 0xFF, 0xFF },
	{ 0xFF, 0xFF, 0x55, 0xFF },
	{ 0xFF, 0xFF, 0xFF, 0xFF },
};

static SDL_Texture* Glyphs[256][256] = {{}};

static char OutputCursorPreviouslyVisible = -1;

static void RenderCharacterAt(SDL_Renderer *ren, TTF_Font* font, Uint x, Uint y)
{
	MazeTile tile = Screen[y][x];
	SDL_Color fg = ScreenColors[tile.color & 15];
	SDL_Color bg = ScreenColors[tile.color >> 4];
	SDL_SetRenderDrawColor(ren, bg.r, bg.g, bg.b, bg.a);
	SDL_Rect rect = { (int)x * TILE_WIDTH, (int)y * TILE_HEIGHT, TILE_WIDTH, TILE_HEIGHT };
	SDL_RenderFillRect(ren, &rect);

	SDL_Texture*& t = Glyphs[tile.color][tile.chr];
	if (!t)
	{
		static const wchar_t Chars[257] =
			L" ☺☻♥♦♣♠•◘○◙♂♀♪♫☼"
			L"►◄↕‼¶§▬↨↑↓→←∟↔▲▼"
			L" !\"#$%&'()*+,-./"
			L"0123456789:;<=>?"
			L"@ABCDEFGHIJKLMNO"
			L"PQRSTUVWXYZ[\\]^_"
			L"`abcdefghijklmno"
			L"pqrstuvwxyz{|}~⌂"
			L"ÇüéâäàåçêëèïîìÄÅ"
			L"ÉæÆôöòûùÿÖÜ¢£¥₧ƒ"
			L"áíóúñÑªº¿⌐¬½¼¡«»"
			L"░▒▓│┤╡╢╖╕╣║╗╝╜╛┐"
			L"└┴┬├─┼╞╟╚╔╩╦╠═╬╧"
			L"╨╤╥╙╘╒╓╫╪┘┌█▄▌▐▀"
			L"αßΓπΣσµτΦΘΩδ∞φε∩"
			L"≡±≥≤⌠⌡÷≈°∙·√ⁿ²■ ";

		wchar_t str[2];
		str[0] = Chars[tile.chr];
		str[1] = 0;
		SDL_Surface *s = TTF_RenderUNICODE_Shaded(font, (Uint16*)str, fg, bg);
		t = SDL_CreateTextureFromSurface(ren, s);
		SDL_FreeSurface(s);
	}

	int w, h;
	SDL_QueryTexture(t, NULL, NULL, &w, &h);
	SDL_Rect src = { 0, 0, w, h };
	SDL_Rect dst = { (int)x * TILE_WIDTH + (TILE_WIDTH - w) / 2, (int)y * TILE_HEIGHT + (TILE_HEIGHT - h) / 2, w, h };
	if (h > TILE_HEIGHT) // not sure if this will always happen, so make the fix conditional
	{
		src.y++;
		src.h--;
		dst.h--;
	}
	SDL_RenderCopy(ren, t, &src, &dst);
}

static int SDLCALL ConsoleThreadFunc(void*)
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
	{
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return 1;
	}

	if (TTF_Init() != 0)
	{
		fprintf(stderr, "TTF_Init: %s\n", SDL_GetError());
		SDL_Quit();
		return 1;
	}

	TTF_Font* font = TTF_OpenFont(FONT_FILENAME, FONT_SIZE);
	if (!font)
	{
		fprintf(stderr, "TTF_OpenFont: %s\n", SDL_GetError());
		TTF_Quit();
		SDL_Quit();
		return 1;
	}
	
	SDL_Window *win = SDL_CreateWindow("Snipes", 100, 100, WINDOW_WIDTH * TILE_WIDTH, WINDOW_HEIGHT * TILE_HEIGHT , SDL_WINDOW_SHOWN);
	if (!win)
	{
		fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
		TTF_CloseFont(font);
		TTF_Quit();
		SDL_Quit();
		return 1;
	}

	SDL_Renderer *ren = SDL_CreateRenderer(win, -1, 0);
	if (!ren)
	{
		SDL_DestroyWindow(win);
		fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
		TTF_CloseFont(font);
		TTF_Quit();
		SDL_Quit();
		return 1;
	}

	memset(Glyphs, 0, sizeof(Glyphs));

	while (!Exiting)
	{
		SDL_Event e;
		while (SDL_PollEvent(&e))
		{
			switch (e.type)
			{
				case SDL_QUIT:
					forfeit_match = got_ctrl_break = true;
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
				case SDL_WINDOWEVENT:
					switch (e.window.event)
					{
						case SDL_WINDOWEVENT_FOCUS_LOST:
							Paused = true;
							break;
						case SDL_WINDOWEVENT_FOCUS_GAINED:
							Paused = false;
							break;
					}
					break;
			}
		}

		SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);

		char outputCursorNowVisible = OutputCursorVisible && SDL_GetTicks() % 500 < 250;
		if (!ScreenChanged)
		{
			if (!outputCursorNowVisible)
				RenderCharacterAt(ren, font, OutputCursorX, OutputCursorY);
		}
		else
		{
			ScreenChanged = false;

			SDL_RenderClear(ren);

			for (Uint y = 0; y < WINDOW_HEIGHT; y++)
				for (Uint x = 0; x < WINDOW_WIDTH; x++)
					RenderCharacterAt(ren, font, x, y);
		}

		if (outputCursorNowVisible && (ScreenChanged || !OutputCursorPreviouslyVisible))
		{
			SDL_Color c = ScreenColors[OutputTextColor];
			SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
			SDL_Rect rect = { (int)OutputCursorX * TILE_WIDTH, (int)OutputCursorY * TILE_HEIGHT, TILE_WIDTH, TILE_HEIGHT };
			SDL_RenderFillRect(ren, &rect);
		}
		OutputCursorPreviouslyVisible = outputCursorNowVisible;

		SDL_RenderPresent(ren);

		SleepTimeslice();
	}

	for (Uint color=0; color<256; color++)
		for (Uint chr=0; chr<256; chr++)
			if (Glyphs[color][chr])
				SDL_DestroyTexture(Glyphs[color][chr]);

	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	TTF_CloseFont(font);
	TTF_Quit();
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
