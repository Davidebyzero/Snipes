#include <stdio.h>
#include <string.h>
#ifdef __APPLE__
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>
#include <SDL_ttf.h>
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
			
			// Process SDL events and render while waiting for input
			ProcessSDLEvents();
			RenderFrame();
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

static SDL_Texture* Glyphs[256] = {};
static bool GlyphIsEmpty[256] = {};

static Uint8 *SingleHorzLineEdge = NULL;
static Uint8 *DoubleHorzLineEdge = NULL;

static void FixUpLineJoiningGlitch(SDL_Surface *s, Uint8 *HorzLineEdge, bool useRightEdge)
{
	Uint64 diff[2] = {}; // initialize all element to zero
	for (int d=0; d<=1; d++)
	{
		Uint8 *alpha = (Uint8*)s->pixels + 3 + useRightEdge*(s->w - 1)*4;
		for (int y0=0; y0<s->h; y0++)
		{
			int y = y0 + d;
			if (y >= s->h)
				continue;
			int alphaDiff = *alpha - SingleHorzLineEdge[y];
			diff[d] += (Uint)(alphaDiff * alphaDiff);
		}
		diff[1] = diff[1] * (s->h-1) / s->h; // compensate for there having been 1 extra sampling
	}
	if (diff[1] < diff[0]) // is the glitch active with this font size?
		memmove((Uint8*)s->pixels + s->pitch, s->pixels, s->pitch * (s->h - 1)); // fix it by shifting the glyph down 1 pixel
}
static void FixUpGlyphVerticallyBottom(SDL_Surface *s)
{
	// Clone the bottommost-penultimate 1-pixel tall horizontal strip into the bottommost one
	Uint8 *pixel = (Uint8*)s->pixels;
	for (int x=0; x<s->w; x++)
		pixel[(TileHeight - 1)*s->pitch + x] = pixel[(TileHeight - 2)*s->pitch + x];
}
static void FixUpGlyphVerticallyTop(SDL_Surface *s)
{
	// Clone the topmost-penultimate 1-pixel tall horizontal strip into the topmost one
	Uint8 *pixel = (Uint8*)s->pixels;
	for (int x=0; x<s->w; x++)
		pixel[0 * s->pitch + x] = pixel[1 * s->pitch + x];
}

static SDL_Texture*& GetGlyph(SDL_Renderer *ren, TTF_Font* font, BYTE chr)
{
	SDL_Texture*& t = Glyphs[chr];
	if (t)
		return t;
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
	str[0] = Chars[chr];
	str[1] = 0;
	SDL_Color white = {0xFF, 0xFF, 0xFF, 0xFF};
	SDL_Color black = {0x00, 0x00, 0x00, 0x00};
	SDL_Surface *s = TTF_RenderUNICODE_Shaded(font, (Uint16*)str, white, black);
#ifdef FONT_FIXUP_VERTICALLY
	if (wcschr(L"│┤╡╢╖╕╣║╗┐┬├┼╞╟╔╦╠╬╤╥╒╓╫╪┌", str[0]) != NULL) FixUpGlyphVerticallyBottom(s);
	if (wcschr(L"│┤╡╢╣║╝╜╛└┴├┼╞╟╚╩╠╬╧╨╙╘╫╪┘", str[0]) != NULL) FixUpGlyphVerticallyTop   (s);
#endif
#ifdef FONT_FIXUP_HORIZONTALLY
	if (wcschr(L"└┴┬├─┼╞╟╚╔╩╦╠═╬╧╨╤╥╙╘╒╓╫╪┌", str[0]) != NULL)
	{
		// Clone the rightmost-penultimate 1-pixel wide vertical strip into the rightmost one
		Uint8 *pixel = (Uint8*)s->pixels;
		for (int y=0; y<s->h; y++, pixel += s->pitch)
			pixel[TileWidth-1] = pixel[TileWidth-2];
	}
	if (wcschr(L"└┴┬├─┼╞╟╚╔╩╦╠═╬╧╨╤╥╙╘╒╓╫╪┌", str[0]) != NULL)
	{
		// Clone the leftmost-penultimate 1-pixel wide vertical strip into the leftmost one
		Uint8 *pixel = (Uint8*)s->pixels;
		for (int y=0; y<s->h; y++, pixel += s->pitch)
			pixel[0] = pixel[1];
	}
#endif
#ifdef FONT_FIXUP_JOINING
	if (chr == 0xC4 && SingleHorzLineEdge==NULL) // ─
	{
		SingleHorzLineEdge = new Uint8 [TileHeight];
		Uint8 *alpha = (Uint8*)s->pixels;
		for (int y=0; y<s->h; y++, alpha += s->pitch)
			SingleHorzLineEdge[y] = *alpha;
	}
	else
	if (chr == 0xCD && DoubleHorzLineEdge==NULL) // ═
	{
		DoubleHorzLineEdge = new Uint8 [TileHeight];
		Uint8 *alpha = (Uint8*)s->pixels;
		for (int y=0; y<s->h; y++, alpha += s->pitch)
			DoubleHorzLineEdge[y] = *alpha;
	}
	else
	if (wcschr(L"┤╢╖┐┬┼╥╫", str[0]) != NULL)
	{
		GetGlyph(ren, font, 0xC4); // ─ // make sure SingleHorzLineEdge is initialized
		FixUpLineJoiningGlitch(s, SingleHorzLineEdge, false);
		FixUpGlyphVerticallyTop(s);
	}
	if (wcschr(L"╡╕╣╗╦╬╤╪", str[0]) != NULL)
	{
		GetGlyph(ren, font, 0xCD); // ═ // make sure DoubleHorzLineEdge is initialized
		FixUpLineJoiningGlitch(s, DoubleHorzLineEdge, false);
		FixUpGlyphVerticallyTop(s);
	}
	else
	if (wcschr(L"├╟╓┌", str[0]) != NULL)
	{
		GetGlyph(ren, font, 0xC4); // ─ // make sure SingleHorzLineEdge is initialized
		FixUpLineJoiningGlitch(s, SingleHorzLineEdge, true);
		FixUpGlyphVerticallyTop(s);
	}
	else
	if (wcschr(L"╞╔╠╒", str[0]) != NULL)
	{
		GetGlyph(ren, font, 0xCD); // ═ // make sure DoubleHorzLineEdge is initialized
		FixUpLineJoiningGlitch(s, DoubleHorzLineEdge, true);
		FixUpGlyphVerticallyTop(s);
	}
#endif
	// Create an RGBA surface from the 8-bit surface created by TTF_RenderUNICODE_Shaded(), initializing the RGB values to maximum white and the alpha channel from the source surface.
	// We assume that its palette maps 0-255 linearly to grayscale 0-255.
	SDL_Surface *s_rgba = SDL_CreateRGBSurfaceWithFormat(0, s->w, s->h, 32, SDL_PIXELFORMAT_RGBA32);
	SDL_FillRect(s_rgba, NULL, 0xFFFFFFFF);
	Uint8 *srcPixel = (Uint8*)s     ->pixels;
	Uint8 *dstAlpha = (Uint8*)s_rgba->pixels + 3;
	Uint total = 0;
	for (int y=0; y<s->h; y++, srcPixel += s->pitch, dstAlpha += s_rgba->pitch)
		for (int x=0; x<s->w; x++)
			total += dstAlpha[x*4] = srcPixel[x];
	GlyphIsEmpty[chr] = total == 0;

	t = SDL_CreateTextureFromSurface(ren, s_rgba);
	SDL_FreeSurface(s);
	return t;
}

static void RenderCharacterAt(SDL_Renderer *ren, TTF_Font* font, Uint x, Uint y)
{
	MazeTile tile = Screen[y][x];
	Uint fgScreen = tile.color & 0xF;
	Uint bgScreen = tile.color >> 4;
	SDL_Color fg = ScreenColors[fgScreen];
	SDL_Color bg = ScreenColors[bgScreen];
	SDL_Color black = {0, 0, 0, 0xFF};
	SDL_Rect rect = { (int)x * TileWidth, (int)y * TileHeight, TileWidth, TileHeight };
	SDL_SetRenderDrawColor(ren, bg.r, bg.g, bg.b, 0xFF);
	if (bgScreen != 0)
		SDL_RenderFillRect(ren, &rect);
	if (GlyphIsEmpty[tile.chr]) // as opposed to tile.chr==0||tile.chr==' ', this leaves full flexibility up to the font
		return;

	SDL_Texture*& t = GetGlyph(ren, font, tile.chr);

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
	SDL_SetTextureColorMod(t, fg.r, fg.g, fg.b);
	SDL_RenderCopy(ren, t, &src, &dst);
}

static void ClearGlyphs()
{
	memset(Glyphs, 0, sizeof(Glyphs));
	memset(GlyphIsEmpty, 0, sizeof(GlyphIsEmpty));
	if (SingleHorzLineEdge) delete [] SingleHorzLineEdge; SingleHorzLineEdge = NULL;
	if (DoubleHorzLineEdge) delete [] DoubleHorzLineEdge; DoubleHorzLineEdge = NULL;
}
static void DestroyGlyphs()
{
	for (Uint color=0; color<256; color++)
		for (Uint chr=0; chr<256; chr++)
			if (Glyphs[color])
				SDL_DestroyTexture(Glyphs[color]);
}

#ifdef __APPLE__
static SDL_Window *main_window = NULL;
static bool window_created = false;
#endif

// Helper function to load font with fallbacks
TTF_Font* LoadFontWithFallbacks(int size)
{
	TTF_Font* font = TTF_OpenFont(FONT_FILENAME, size);
	if (!font)
	{
		fprintf(stderr, "WARNING: Could not load %s\n", FONT_FILENAME);
		fprintf(stderr, "The custom font is required for authentic visuals.\n");
		fprintf(stderr, "Download from: http://kingbird.myphotos.cc/ee22d44076adb8a34d8e20df4be3730a/SnipesConsole.ttf\n");
		fprintf(stderr, "Attempting to use system fallback fonts (visuals may differ)...\n");
		// Try common system monospace fonts as fallbacks
		const char* fallback_fonts[] = {
			"/System/Library/Fonts/Courier.ttc",
			"/System/Library/Fonts/SFNSMono.ttf",
			"/System/Library/Fonts/Menlo.ttc",
			"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", // Linux
			NULL
		};

		for (int i = 0; fallback_fonts[i] != NULL; i++)
		{
			font = TTF_OpenFont(fallback_fonts[i], size);
			if (font)
			{
				fprintf(stderr, "Using fallback font: %s (NOTICE: Game visuals will differ from original)\n", fallback_fonts[i]);
				break;
			}
		}
	}
	return font;
}

int ConsoleThreadFunc(void*)
{
	SDL_Rect usableRect;
	if (SDL_GetDisplayUsableBounds(0, &usableRect) != 0)
	{
		fprintf(stderr, "SDL_GetDesktopDisplayMode: %s\n", SDL_GetError());
		SDL_Quit();
		return 1;
	}
	{
#ifdef TILE_HEIGHT
		if (TILE_HEIGHT * WINDOW_HEIGHT > usableRect.h)
		{
			fprintf(stderr, "Error: Configured tile height (%d) exceeds usable height (%d)\n", TILE_HEIGHT, usableRect.h);
			SDL_Quit();
			return 1;
		}
		TileHeight = TILE_HEIGHT;
#else
		TileHeight = usableRect.h / WINDOW_HEIGHT;
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
	}
	SDL_Window *win;
	int top, left, bottom, right, totalWidth, totalHeight;
	for (Uint i=0; i<2; i++)
	{
		win = SDL_CreateWindow("Snipes", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH * TileWidth, WINDOW_HEIGHT * TileHeight, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
		if (!win)
		{
			fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
			return 1;
		}
		SDL_GetWindowBordersSize(win, &top, &left, &bottom, &right);
		totalHeight = WINDOW_HEIGHT * TileHeight + (top + bottom);
#ifdef TILE_HEIGHT
		break;
#else
		if (i || totalHeight <= usableRect.h + ALLOWABLE_BORDER_EXCESS)
			break;
		TileHeight = (usableRect.h - (top + bottom)) / WINDOW_HEIGHT;
		TileWidth = ((TileHeight*3+2)/4);
		SDL_DestroyWindow(win);
#endif
	}
	totalWidth = WINDOW_WIDTH * TileWidth + (left + right);
	SDL_SetWindowPosition(win, (usableRect.w - totalWidth) / 2 + left, (usableRect.h - totalHeight) / 2 + top); // center the window within the usable desktop area

	if (TTF_Init() != 0)
	{
		fprintf(stderr, "TTF_Init: %s\n", SDL_GetError());
		return 1;
	}

	TTF_Font* font = LoadFontWithFallbacks(FontSize);
	if (!font)
	{
		fprintf(stderr, "TTF_OpenFont: Could not load any fonts\n");
		TTF_Quit();
		return 1;
	}
	TTF_SetFontHinting(font, TTF_HINTING_MONO);

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

						TTF_Font* newFont = LoadFontWithFallbacks(FontSize);
						if (!newFont)
						{
							fprintf(stderr, "TTF_OpenFont: Could not load any fonts\n");
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
	
	// For macOS, we'll initialize everything on the main thread
	ScreenMutex = SDL_CreateMutex();
	
	// On macOS, the console window will be created when we run the main loop
	return 0;
}

void CloseConsole()
{
	Exiting = true;
	SDL_DestroyMutex(ScreenMutex);
	SDL_Quit();
}

// For macOS - run the SDL window creation and event loop on main thread
SDL_Window* global_window = NULL;
SDL_Renderer* global_renderer = NULL;
TTF_Font* global_font = NULL;

int CreateSDLWindow()
{
	SDL_Rect usableRect;
	if (SDL_GetDisplayUsableBounds(0, &usableRect) != 0)
	{
		fprintf(stderr, "SDL_GetDisplayUsableBounds: %s\n", SDL_GetError());
		return 1;
	}
	
#ifdef TILE_HEIGHT
	if (TILE_HEIGHT * WINDOW_HEIGHT > usableRect.h)
	{
		fprintf(stderr, "Error: Configured tile height (%d) exceeds usable height (%d)\n", TILE_HEIGHT, usableRect.h);
		return 1;
	}
	TileHeight = TILE_HEIGHT;
#else
	TileHeight = usableRect.h / WINDOW_HEIGHT;
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

	int top, left, bottom, right, totalWidth, totalHeight;
	for (Uint i=0; i<2; i++)
	{
		global_window = SDL_CreateWindow("Snipes", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 
		                                 WINDOW_WIDTH * TileWidth, WINDOW_HEIGHT * TileHeight, 
		                                 SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
		if (!global_window)
		{
			fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
			return 1;
		}
		SDL_GetWindowBordersSize(global_window, &top, &left, &bottom, &right);
		totalHeight = WINDOW_HEIGHT * TileHeight + (top + bottom);
#ifdef TILE_HEIGHT
		break;
#else
		if (i || totalHeight <= usableRect.h + ALLOWABLE_BORDER_EXCESS)
			break;
		TileHeight = (usableRect.h - (top + bottom)) / WINDOW_HEIGHT;
		TileWidth = ((TileHeight*3+2)/4);
		FontSize = TileHeight;
		SDL_DestroyWindow(global_window);
#endif
	}
	totalWidth = WINDOW_WIDTH * TileWidth + (left + right);
	SDL_SetWindowPosition(global_window, (usableRect.w - totalWidth) / 2 + left, (usableRect.h - totalHeight) / 2 + top);

	if (TTF_Init() != 0)
	{
		fprintf(stderr, "TTF_Init: %s\n", SDL_GetError());
		SDL_DestroyWindow(global_window);
		return 1;
	}

	global_font = LoadFontWithFallbacks(FontSize);
	if (!global_font)
	{
		fprintf(stderr, "TTF_OpenFont: Could not load any fonts\n");
		TTF_Quit();
		SDL_DestroyWindow(global_window);
		return 1;
	}
	TTF_SetFontHinting(global_font, TTF_HINTING_MONO);

	global_renderer = SDL_CreateRenderer(global_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!global_renderer)
	{
		fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
		TTF_CloseFont(global_font);
		TTF_Quit();
		SDL_DestroyWindow(global_window);
		return 1;
	}

	ClearGlyphs();
	return 0;
}

void ProcessSDLEvents()
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
			for (const char* s = e.text.text; *s; s++)
			{
				InputBuffer[InputBufferWriteIndex] = *s;
				InputBufferWriteIndex = (InputBufferWriteIndex+1) % InputBufferSize;
			}
			break;
		case SDL_WINDOWEVENT:
			switch (e.window.event)
			{
			case SDL_WINDOWEVENT_SIZE_CHANGED:
				{
					TileWidth = e.window.data1 / WINDOW_WIDTH;
					TileHeight = e.window.data2 / WINDOW_HEIGHT;
					if (TileWidth > TileHeight)
					{
						TileWidth = TileHeight;
					}
					else
					{
						if (TileWidth != TileHeight)
						{
							TileHeight = ((TileWidth*8+3)/6);
						}
					}

					FontSize = TileHeight;
					DestroyGlyphs();
					ClearGlyphs();

					TTF_Font* newFont = LoadFontWithFallbacks(FontSize);
					if (!newFont)
					{
						fprintf(stderr, "TTF_OpenFont: Could not load any fonts\n");
						instant_quit = forfeit_match = got_ctrl_break = true;
					}
					else
					{
						TTF_CloseFont(global_font);
						global_font = newFont;
					}

					SDL_SetWindowSize(global_window, WINDOW_WIDTH * TileWidth, WINDOW_HEIGHT * TileHeight);
				}
				break;
			}
			break;
		}
	}
}

void RenderFrame()
{
	SDL_SetRenderDrawColor(global_renderer, 0, 0, 0, 255);
	SDL_RenderClear(global_renderer);

	SDL_LockMutex(ScreenMutex);
	for (Uint y = 0; y < WINDOW_HEIGHT; y++)
		for (Uint x = 0; x < WINDOW_WIDTH; x++)
			RenderCharacterAt(global_renderer, global_font, x, y);
	SDL_UnlockMutex(ScreenMutex);

	if (OutputCursorVisible && SDL_GetTicks() % 500 < 250)
	{
		SDL_Color c = ScreenColors[OutputTextColor];
		SDL_SetRenderDrawColor(global_renderer, c.r, c.g, c.b, c.a);
		SDL_Rect rect = { (int)OutputCursorX * TileWidth, (int)OutputCursorY * TileHeight, TileWidth, TileHeight };
		SDL_RenderFillRect(global_renderer, &rect);
	}

	SDL_RenderPresent(global_renderer);
}

void CleanupSDL()
{
	DestroyGlyphs();
	if (global_renderer) SDL_DestroyRenderer(global_renderer);
	if (global_font) TTF_CloseFont(global_font);
	TTF_Quit();
	if (global_window) SDL_DestroyWindow(global_window);
}
