﻿#include <stdio.h>
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
SDL_mutex *ScreenMutex;

int FontSize, TileWidth, TileHeight;

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
	SDL_LockMutex(ScreenMutex);
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
#if 0 // defined(CHEAT_OMNISCIENCE) && defined(CHEAT_OMNISCIENCE_SHOW_NORMAL_VIEWPORT)
		if (!(inrangex(column+i, WINDOW_WIDTH/2 - 40/2,
		                         WINDOW_WIDTH/2 + 40/2) &&
		      inrangex(row, VIEWPORT_ROW + VIEWPORT_HEIGHT/2 - (25 - VIEWPORT_ROW)/2,
		                    VIEWPORT_ROW + VIEWPORT_HEIGHT/2 + (25 - VIEWPORT_ROW)/2)) && row >= VIEWPORT_ROW)
			tile.color += 0x40;
#endif
		*dst++ = tile;
	}
	SDL_UnlockMutex(ScreenMutex);
}

void outputText(BYTE color, WORD count, WORD row, WORD column, const char *src)
{
	SDL_LockMutex(ScreenMutex);
	MazeTile* dst = &Screen[row][column];
	for (Uint i=0; i<count; i++)
	{
		if ((size_t)(dst - &Screen[0][0]) >= WINDOW_WIDTH*WINDOW_HEIGHT)
			break; // Out of bounds
		*dst++ = MazeTile(color, src[i]);
	}
	SDL_UnlockMutex(ScreenMutex);
}

void outputNumber(BYTE color, bool zeroPadding, WORD count, WORD row, WORD column, Uint number)
{
	SDL_LockMutex(ScreenMutex);
	char textbuf[strlength("4294967295")+1];
	sprintf(textbuf, zeroPadding ? "%0*u" : "%*u", count, number);
	outputText(color, count, row, column, textbuf);
	SDL_UnlockMutex(ScreenMutex);
}

void EraseBottomTwoLines()
{
	SDL_LockMutex(ScreenMutex);
	for (Uint y = WINDOW_HEIGHT-2; y < WINDOW_HEIGHT; y++)
		for (Uint x = 0; x < WINDOW_WIDTH; x++)
			Screen[y][x] = MazeTile(7, ' ');
	SDL_UnlockMutex(ScreenMutex);
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
	SDL_LockMutex(ScreenMutex);
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
	SDL_UnlockMutex(ScreenMutex);
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

	SDL_LockMutex(ScreenMutex);
	for (Uint y = 0; y < WINDOW_HEIGHT; y++)
		for (Uint x = 0; x < WINDOW_WIDTH; x++)
			Screen[y][x].color = DEFAULT_TEXT_COLOR;
	SDL_UnlockMutex(ScreenMutex);
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

static void RenderCharacterAt(SDL_Renderer *ren, TTF_Font* font, Uint x, Uint y)
{
	MazeTile tile = Screen[y][x];
	SDL_Color fg = ScreenColors[tile.color & 15];
	SDL_Color bg = ScreenColors[tile.color >> 4];
	SDL_Rect rect = { (int)x * TileWidth, (int)y * TileHeight, TileWidth, TileHeight };
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
	SDL_Rect dst = { (int)x * TileWidth + (TileWidth - w) / 2, (int)y * TileHeight + (TileHeight - h) / 2, w, h };
	if (h > (int)TileHeight) // not sure if this will always happen, so make the fix conditional
	{
		src.y += TileHeight % 19 * 8 / 19 % 2;
		src.h--;
		dst.h--;
	}
	SDL_RenderCopy(ren, t, &src, &dst);
}

static void ClearGlyphs()
{
	memset(Glyphs, 0, sizeof(Glyphs));
}
static void DestroyGlyphs()
{
	for (Uint color=0; color<256; color++)
		for (Uint chr=0; chr<256; chr++)
			if (Glyphs[color][chr])
				SDL_DestroyTexture(Glyphs[color][chr]);
}

static int SDLCALL ConsoleThreadFunc(void*)
{
	if (TTF_Init() != 0)
	{
		fprintf(stderr, "TTF_Init: %s\n", SDL_GetError());
		return 1;
	}

	TTF_Font* font = TTF_OpenFont(FONT_FILENAME, FontSize);
	if (!font)
	{
		fprintf(stderr, "TTF_OpenFont: %s\n", SDL_GetError());
		TTF_Quit();
		return 1;
	}
	
	SDL_Window *win = SDL_CreateWindow("Snipes", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH * TileWidth, WINDOW_HEIGHT * TileHeight, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
	if (!win)
	{
		fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
		TTF_CloseFont(font);
		TTF_Quit();
		return 1;
	}

	SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!ren)
	{
		SDL_DestroyWindow(win);
		fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
		TTF_CloseFont(font);
		TTF_Quit();
		return 1;
	}

	ClearGlyphs();

	while (!Exiting)
	{
		SDL_Event e;
		while (SDL_PollEvent(&e))
		{
			switch (e.type)
			{
			case SDL_QUIT:
				instant_quit = forfeit_match = got_ctrl_break = true;
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
				case SDL_WINDOWEVENT_RESIZED:
					{
						int newWidth  = e.window.data1;
						int newHeight = e.window.data2;
						{
							double  widthRatio = (double)newWidth  / (WINDOW_WIDTH  * TileWidth);
							double heightRatio = (double)newHeight / (WINDOW_HEIGHT * TileHeight);
							if ( widthRatio < 1)  widthRatio = 1. /  widthRatio;
							if (heightRatio < 1) heightRatio = 1. / heightRatio;
							
							if (heightRatio > widthRatio)
							{
								TileHeight = (newHeight + WINDOW_HEIGHT/2) / WINDOW_HEIGHT;
								TileWidth = ((TileHeight*3+2)/4);
							}
							else
							{
								TileWidth = (newWidth + WINDOW_WIDTH/2) / WINDOW_WIDTH;
								TileHeight = ((TileWidth*8+3)/6);
							}
						}

						FontSize = TileHeight;
						DestroyGlyphs();
						ClearGlyphs();

						TTF_Font* newFont = TTF_OpenFont(FONT_FILENAME, FontSize);
						if (!newFont)
						{
							fprintf(stderr, "TTF_OpenFont: %s\n", SDL_GetError());
							instant_quit = forfeit_match = got_ctrl_break = true;
						}
						else
						{
							TTF_CloseFont(font);
							font = newFont;
						}

						SDL_SetWindowSize(win, WINDOW_WIDTH * TileWidth, WINDOW_HEIGHT * TileHeight);
					}
					break;
				}
				break;
			}
		}

		SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);

		SDL_RenderClear(ren);

		SDL_LockMutex(ScreenMutex);
		for (Uint y = 0; y < WINDOW_HEIGHT; y++)
			for (Uint x = 0; x < WINDOW_WIDTH; x++)
				RenderCharacterAt(ren, font, x, y);
		SDL_UnlockMutex(ScreenMutex);
#if defined(CHEAT_OMNISCIENCE) && defined(CHEAT_OMNISCIENCE_SHOW_NORMAL_VIEWPORT)
		{
			SDL_Rect rect;

			// darken outside area

			SDL_SetRenderDrawColor(ren, CHEAT_OMNISCIENCE_RGBA_OUTSIDE_NORMAL_VIEWPORT);
			SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

			rect.w = WINDOW_WIDTH * TileWidth;  rect.x = 0; rect.y = VIEWPORT_ROW * TileHeight; rect.h = (VIEWPORT_HEIGHT/2 - (25 - VIEWPORT_ROW)/2) * TileHeight;
			SDL_RenderFillRect(ren, &rect);

			rect.y = (VIEWPORT_ROW + VIEWPORT_HEIGHT/2 + (25 - VIEWPORT_ROW)/2) * TileHeight;
			SDL_RenderFillRect(ren, &rect);

			rect.x = 0; rect.y = (VIEWPORT_ROW + VIEWPORT_HEIGHT/2 - (25 - VIEWPORT_ROW)/2) * TileHeight; rect.w = (WINDOW_WIDTH/2 - 40/2) * TileWidth; rect.h = ((25 - VIEWPORT_ROW)/2*2) * TileHeight;
			SDL_RenderFillRect(ren, &rect);

			rect.x = (WINDOW_WIDTH/2 + 40/2) * TileWidth;
			SDL_RenderFillRect(ren, &rect);

	#ifdef CHEAT_OMNISCIENCE_RGBA_AROUND_NORMAAL_VIEWPORT
			// draw gray rectangle around inside area
			rect.x = (WINDOW_WIDTH/2 - 40/2) * TileWidth - 1; rect.y = (VIEWPORT_ROW + VIEWPORT_HEIGHT/2 - (25 - VIEWPORT_ROW)/2  ) * TileHeight - 1;
			rect.w = (                 80/2) * TileWidth + 2; rect.h = (                                   (25 - VIEWPORT_ROW)/2*2) * TileHeight + 2;
			SDL_SetRenderDrawColor(ren, CHEAT_OMNISCIENCE_RGBA_AROUND_NORMAAL_VIEWPORT);
			SDL_RenderDrawRect(ren, &rect);
	#endif CHEAT_OMNISCIENCE_RGBA_AROUND_NORMAAL_VIEWPORT

			SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
		}
#endif

		if (OutputCursorVisible && SDL_GetTicks() % 500 < 250)
		{
			SDL_Color c = ScreenColors[OutputTextColor];
			SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
			SDL_Rect rect = { (int)OutputCursorX * TileWidth, (int)OutputCursorY * TileHeight, TileWidth, TileHeight };
			SDL_RenderFillRect(ren, &rect);
		}

#if 0
		if (screenshot_filename)
		{
			SDL_Surface *infoSurface = SDL_GetWindowSurface(win);
			unsigned char *pixels = new BYTE [infoSurface->w * infoSurface->h * infoSurface->format->BytesPerPixel];
			SDL_RenderReadPixels(ren, &infoSurface->clip_rect, infoSurface->format->format, pixels, infoSurface->w * infoSurface->format->BytesPerPixel);
			SDL_Surface *saveSurface = SDL_CreateRGBSurfaceFrom(pixels, infoSurface->w, infoSurface->h, infoSurface->format->BitsPerPixel, infoSurface->w * infoSurface->format->BytesPerPixel, infoSurface->format->Rmask, infoSurface->format->Gmask, infoSurface->format->Bmask, infoSurface->format->Amask);
			SDL_SaveBMP(saveSurface, screenshot_filename);
			SDL_FreeSurface(saveSurface);
			screenshot_filename = NULL;
		}
#endif
		SDL_RenderPresent(ren);

		SleepTimeslice();
	}

	DestroyGlyphs();

	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	TTF_CloseFont(font);
	TTF_Quit();

	return 0;
}

static SDL_Thread *ConsoleThread;

int OpenConsole()
{
	Exiting = false;

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
	{
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return 1;
	}
	else
	{
		SDL_DisplayMode dm;
		if (SDL_GetDesktopDisplayMode(0, &dm) != 0)
		{
			fprintf(stderr, "SDL_GetDesktopDisplayMode: %s\n", SDL_GetError());
			SDL_Quit();
			return 1;
		}
#ifdef TILE_HEIGHT
		if (TILE_HEIGHT * WINDOW_HEIGHT > dm.h)
		{
			fprintf(stderr, "Error: Configured tile height (%d) exceeds desktop height (%d)\n", TILE_HEIGHT, dm.h);
			SDL_Quit();
			return 1;
		}
		TileHeight = TILE_HEIGHT;
#else
		TileHeight = dm.h / WINDOW_HEIGHT;
#endif

#ifndef TILE_WIDTH
		TileWidth = ((TileHeight*3+2)/4);
#else
		TileWidth = TILE_WIDTH;
#endif
#ifndef FONT_SIZE
		FontSize = TileHeight;
#else
		FontSize = FONT_SIZE;
#endif

		ConsoleThread = SDL_CreateThread(ConsoleThreadFunc, "ConsoleThread", NULL);
		if (!ConsoleThread)
		{
			fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
			SDL_Quit();
			return 1;
		}

		ScreenMutex = SDL_CreateMutex();

		return 0;
	}
}

void CloseConsole()
{
	Exiting = true;
	SDL_WaitThread(ConsoleThread, NULL);
	SDL_DestroyMutex(ScreenMutex);
	SDL_Quit();
}
