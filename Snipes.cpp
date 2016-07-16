#include <Windows.h>
#include <MMSystem.h>
#include <stdio.h>
#include <math.h>
#include <wchar.h>
#pragma comment(lib,"winmm.lib")

typedef unsigned int Uint;
typedef unsigned long long QWORD;

#define inrange(n,a,b) ((Uint)((n)-(a))<=(Uint)((b)-(a)))

template <size_t size>
char (*__strlength_helper(char const (&_String)[size]))[size];
#define strlength(_String) (sizeof(*__strlength_helper(_String))-1)

#define STRING_WITH_LEN(s) s, strlength(s)

QWORD perf_freq;

WORD GetTickCountWord()
{
	QWORD time;
	QueryPerformanceCounter((LARGE_INTEGER*)&time);
	return (WORD)(time * 13125000 / perf_freq);
}

HANDLE input;
HANDLE output;

#define TONE_SAMPLE_RATE 48000
#define WAVE_BUFFER_LENGTH 200
#define WAVE_BUFFER_COUNT 11
HWAVEOUT waveOutput;
WAVEHDR waveHeader[WAVE_BUFFER_COUNT];
static double toneFreq = 0;
Uint tonePhase;
SHORT toneBuf[WAVE_BUFFER_LENGTH * WAVE_BUFFER_COUNT];
void CALLBACK WaveOutProc(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
	if (uMsg != WOM_DONE)
		return;
	if (toneFreq == 0.)
		return;
	WAVEHDR *currentWaveHeader = (WAVEHDR*)dwParam1;
	for (Uint i=0; i<WAVE_BUFFER_LENGTH; i++)
	{
		((SHORT*)currentWaveHeader->lpData)[i] = fmod(tonePhase * toneFreq, 1.) < 0.5 ? 0 : 0x2000;
		tonePhase++;
	}
	waveOutWrite(hwo, currentWaveHeader, sizeof(SHORT)*WAVE_BUFFER_LENGTH);
}
void PlayTone(Uint freqnum)
{
	bool soundAlreadyPlaying = toneFreq != 0.;
	toneFreq = (13125000. / (TONE_SAMPLE_RATE * 11)) / freqnum;
	tonePhase = 0;
	if (soundAlreadyPlaying)
		return;
	for (Uint i=0; i<WAVE_BUFFER_COUNT; i++)
		WaveOutProc(waveOutput, WOM_DONE, 0, (DWORD_PTR)&waveHeader[i], 0);
}
void ClearSound()
{
	toneFreq = 0.;
}

#define KEYSTATE_MOVE_RIGHT (1<<0)
#define KEYSTATE_MOVE_LEFT  (1<<1)
#define KEYSTATE_MOVE_DOWN  (1<<2)
#define KEYSTATE_MOVE_UP    (1<<3)
#define KEYSTATE_FIRE_RIGHT (1<<4)
#define KEYSTATE_FIRE_LEFT  (1<<5)
#define KEYSTATE_FIRE_DOWN  (1<<6)
#define KEYSTATE_FIRE_UP    (1<<7)

static bool forfeit_match = false;
static bool sound_enabled = true;
static bool shooting_sound_enabled = false;
static bool spacebar_state = false;
static BYTE keyboard_state = 0;
Uint PollKeyboard()
{
	for (;;)
	{
		DWORD numread = 0;
		INPUT_RECORD record;
		if (!PeekConsoleInput(input, &record, 1, &numread))
			break;
		if (!numread)
			break;
		ReadConsoleInput(input, &record, 1, &numread);
		if (!numread)
			break;
		if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown)
		{
			if (record.Event.KeyEvent.wVirtualKeyCode == VK_F1)
				sound_enabled ^= true;
			else
			if (record.Event.KeyEvent.wVirtualKeyCode == VK_F2)
				shooting_sound_enabled ^= true;
			else
			if (record.Event.KeyEvent.wVirtualKeyCode == VK_ESCAPE)
				forfeit_match = true;
		}
	}
	Uint state = 0;
	if (GetKeyState(VK_RIGHT) < 0) state |= KEYSTATE_MOVE_RIGHT;
	if (GetKeyState(VK_LEFT ) < 0) state |= KEYSTATE_MOVE_LEFT;
	if (GetKeyState(VK_DOWN ) < 0) state |= KEYSTATE_MOVE_DOWN;
	if (GetKeyState(VK_CLEAR) < 0) state |= KEYSTATE_MOVE_DOWN;
	if (GetKeyState(VK_UP   ) < 0) state |= KEYSTATE_MOVE_UP;
	if (GetKeyState('D'     ) < 0) state |= KEYSTATE_FIRE_RIGHT;
	if (GetKeyState('A'     ) < 0) state |= KEYSTATE_FIRE_LEFT;
	if (GetKeyState('S'     ) < 0) state |= KEYSTATE_FIRE_DOWN;
	if (GetKeyState('X'     ) < 0) state |= KEYSTATE_FIRE_DOWN;
	if (GetKeyState('W'     ) < 0) state |= KEYSTATE_FIRE_UP;
	spacebar_state = GetKeyState(VK_SPACE) < 0;
	if ((state & (KEYSTATE_MOVE_RIGHT | KEYSTATE_MOVE_LEFT)) == (KEYSTATE_MOVE_RIGHT | KEYSTATE_MOVE_LEFT) ||
		(state & (KEYSTATE_MOVE_DOWN  | KEYSTATE_MOVE_UP  )) == (KEYSTATE_MOVE_DOWN  | KEYSTATE_MOVE_UP  ))
		state &= ~(KEYSTATE_MOVE_RIGHT | KEYSTATE_MOVE_LEFT | KEYSTATE_MOVE_DOWN | KEYSTATE_MOVE_UP);
	if ((state & (KEYSTATE_FIRE_RIGHT | KEYSTATE_FIRE_LEFT)) == (KEYSTATE_FIRE_RIGHT | KEYSTATE_FIRE_LEFT) ||
		(state & (KEYSTATE_FIRE_DOWN  | KEYSTATE_FIRE_UP  )) == (KEYSTATE_FIRE_DOWN  | KEYSTATE_FIRE_UP  ))
		state &= ~(KEYSTATE_FIRE_RIGHT | KEYSTATE_FIRE_LEFT | KEYSTATE_FIRE_DOWN | KEYSTATE_FIRE_UP);
	return state;
}

#define WINDOW_WIDTH  40
#define WINDOW_HEIGHT 25

#define VIEWPORT_ROW    3
#define VIEWPORT_HEIGHT 22

void WriteTextMem(Uint count, Uint dst, WORD *src)
{
	COORD pos;
	pos.X = dst / 2 % WINDOW_WIDTH;
	pos.Y = dst / 2 / WINDOW_WIDTH;
	SetConsoleCursorPosition(output, pos);
	while (count--)
	{
		SetConsoleTextAttribute(output, ((BYTE*)src)[1]);
		DWORD operationSize;
		WriteConsole(output, (BYTE*)src, 1, &operationSize, 0);
		src++;
	}
}

DWORD ReadConsole_wrapper(char buffer[], DWORD bufsize)
{
	DWORD numread, numreadWithoutNewline;
	ReadConsole(input, buffer, bufsize, &numread, NULL);
	if (!numread)
		return numread;

	numreadWithoutNewline = numread;
	if (buffer[numread-1] == '\n')
	{
		numreadWithoutNewline--;
		if (numread > 1 && buffer[numread-2] == '\r')
			numreadWithoutNewline--;
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
	return numreadWithoutNewline;
}

static WORD random_seed_lo = 33, random_seed_hi = 467;
WORD GetRandomMasked(WORD mask)
{
	random_seed_lo *= 2;
	if (random_seed_lo  > 941)
		random_seed_lo -= 941;
	random_seed_hi *= 2;
	if (random_seed_hi  > 947)
		random_seed_hi -= 947;
	return (random_seed_lo + random_seed_hi) & mask;
}
template <WORD RANGE>
WORD GetRandomRanged()
{
	WORD mask = RANGE-1;
	mask |= mask >> 1;
	mask |= mask >> 2;
	mask |= mask >> 4;
	mask |= mask >> 8;
	for (;;)
	{
		WORD number = GetRandomMasked(mask);
		if (number < RANGE)
			return number;
	}
}

Uint skillLevelLetter = 0;
Uint skillLevelNumber = 1;

void ParseSkillLevel(char *skillLevel, DWORD skillLevelLength)
{
	Uint skillLevelNumberTmp = 0;
	for (; skillLevelLength; skillLevel++, skillLevelLength--)
	{
		if (skillLevelNumberTmp >= 0x80)
		{
			// strange behavior, but this is what the original game does
			skillLevelNumber = 1;
			return;
		}
		char ch = *skillLevel;
		if (inrange(ch, 'a', 'z'))
			skillLevelLetter = ch - 'a';
		else
		if (inrange(ch, 'A', 'Z'))
			skillLevelLetter = ch - 'A';
		else
		if (inrange(ch, '0', '9'))
			skillLevelNumberTmp = skillLevelNumberTmp * 10 + (ch - '0');
	}
	if (inrange(skillLevelNumberTmp, 1, 9))
		skillLevelNumber = skillLevelNumberTmp;
}

void ReadSkillLevel()
{
	char skillLevel[0x80] = {0};
	DWORD skillLevelLength = ReadConsole_wrapper(skillLevel, _countof(skillLevel));

	if (skillLevel[skillLevelLength-1] == '\n')
		skillLevelLength--;
	if (skillLevel[skillLevelLength-1] == '\r')
		skillLevelLength--;

	for (Uint i=0; i<skillLevelLength; i++)
		if (skillLevel[i] != ' ')
		{
			ParseSkillLevel(skillLevel + i, skillLevelLength - i);
			break;
		}
}

static BYTE skillThing1Table  ['Z'-'A'+1] = {2, 3, 4, 3, 4, 4, 3, 4, 3, 4, 4, 5, 3, 4, 3, 4, 3, 4, 3, 4, 4, 5, 4, 4, 5, 5};
static bool skillThing2Table  ['Z'-'A'+1] = {0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1};
static bool rubberBulletTable ['Z'-'A'+1] = {0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
static BYTE skillThing3Table  ['Z'-'A'+1] = {0x7F, 0x7F, 0x7F, 0x3F, 0x3F, 0x1F, 0x7F, 0x7F, 0x3F, 0x3F, 0x1F, 0x1F, 0x7F, 0x7F, 0x3F, 0x3F, 0x7F, 0x7F, 0x3F, 0x3F, 0x1F, 0x1F, 0x3F, 0x1F, 0x1F, 0x0F};
static BYTE maxSnipesTable    ['9'-'1'+1] = { 10,  20,  30,  40,  60,  80, 100, 120, 150};
static BYTE numGeneratorsTable['9'-'1'+1] = {  3,   3,   4,   4,   5,   5,   6,   8,  10};
static BYTE numLivesTable     ['9'-'1'+1] = {  5,   5,   5,   5,   5,   4,   4,   3,   2};

bool enableElectricWalls, skillThing2, skillThing7, enableRubberBullets;
BYTE skillThing1, skillThing3, maxSnipes, numGenerators, numLives;

BYTE data_2AA;
WORD frame;
static bool data_C75, data_C73, data_C72, data_CBF;
static BYTE data_2B4, data_2B3, data_2B2, data_2C0, data_C6C, data_C6D, data_C6F, data_C71, data_C6E, data_C70, data_B65, data_B68, data_B67, data_B66, data_B64, data_C74, data_DF0 = 0xFF, data_DF1, data_C96, data_B69, data_C77, data_C78;
static WORD data_290, data_28E, data_348, data_346, data_1CA, data_1CC, data_B5C;
static SHORT data_292, data_34E;
BYTE *data_34A;
const WORD *data_34C;

#define MAX_OBJECTS 0x100

static BYTE data_B6C[MAX_OBJECTS];

const size_t data_350_size = 8 * 40;
static BYTE data_350[8 * MAX_OBJECTS];

#define MAZE_CELL_WIDTH  8
#define MAZE_CELL_HEIGHT 6
#define MAZE_WIDTH  (MAZE_CELL_WIDTH  * 16)
#define MAZE_HEIGHT (MAZE_CELL_HEIGHT * 20)

static WORD maze[MAZE_WIDTH * MAZE_HEIGHT];

void outputText(BYTE color, WORD count, WORD dst, char *src)
{
	COORD pos;
	pos.X = dst / 2 % WINDOW_WIDTH;
	pos.Y = dst / 2 / WINDOW_WIDTH;
	SetConsoleCursorPosition(output, pos);
	SetConsoleTextAttribute(output, color);
	DWORD operationSize;
	WriteConsole(output, src, count, &operationSize, 0);
}

void outputNumber(BYTE color, bool zeroPadding, WORD count, WORD dst, Uint number)
{
	char textbuf[strlength("4294967295")+1];
	sprintf_s(textbuf, sizeof(textbuf), zeroPadding ? "%0*u" : "%*u", count, number);
	outputText(color, count, dst, textbuf);
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

static char statusLine[] = "\xDA\xBF\xB3\x01\x1A\xB3\x02\xB3""Skill""\xC0\xD9\xB3\x01\x1A\xB3\x02\xB3""Time  Men Left                 Score     0  0000001 Man Left\x65";

void outputHUD()
{
	char skillLetter = skillLevelLetter + 'A';
	memset((char*)maze, ' ', 40);
	outputText  (0x17,    40, 2* 0, (char*)maze);
	outputText  (0x17,    40, 2*40, (char*)maze);
	outputText  (0x17,     2, 2* 0, statusLine);
	outputNumber(0x13,  0, 2, 2* 3, 0);
	outputText  (0x17,     1, 2* 6, statusLine+2);
	outputText  (0x13,     2, 2* 8, statusLine+3);
	outputNumber(0x13,  0, 4, 2*11, 0);
	outputText  (0x17,     1, 2*16, statusLine+5);
	outputText  (0x13,     1, 2*18, statusLine+6);
	outputNumber(0x13,  0, 4, 2*20, 0);
	outputText  (0x17,     1, 2*25, statusLine+7);
	outputText  (0x17,     5, 2*27, statusLine+8);
	outputNumber(0x17,  0, 1, 2*38, skillLevelNumber);
	outputText  (0x17,     1, 2*37, &skillLetter);
	outputText  (0x17,     2, 2*40, statusLine+13);
	outputText  (0x17,     1, 2*46, statusLine+15);
	outputText  (0x17,     2, 2*48, statusLine+16);
	outputText  (0x17,     1, 2*56, statusLine+18);
	outputText  (0x17,     1, 2*58, statusLine+19);
	outputText  (0x17,     1, 2*65, statusLine+20);
	outputText  (0x17,     4, 2*67, statusLine+21);
	memcpy(maze, statusLine+25, 40);
	((char*)maze)[0] = numLives + '0';
	((char*)maze)[1] = 0;
	if (numLives == 1)
		((char*)maze)[6] = 'a';
	outputText  (0x17, 40, 2*80, (char*)maze);

	data_2B4 = 0xFF;
	data_2B3 = 0xFF;
	data_2B2 = 0xFF;
	data_290 = 0xFFFF;
	data_28E = 0xFFFF;
	data_2C0 = 0xFF;
	data_292 = -1;
}

void CreateMaze_helper(SHORT &data_1E0, BYTE &data_2AF)
{
	switch (data_2AF)
	{
	case 0:
		data_1E0 -= 0x10;
		if (data_1E0 < 0)
			data_1E0 += data_350_size;
		break;
	case 1:
		data_1E0 += 0x10;
		if (data_1E0 >= data_350_size)
			data_1E0 -= data_350_size;
		break;
	case 2:
		if ((data_1E0 & 0xF) == 0)
			data_1E0 += 0xF;
		else
			data_1E0--;
		break;
	case 3:
		if ((data_1E0 & 0xF) == 0xF)
			data_1E0 -= 0xF;
		else
			data_1E0++;
		break;
	}
}

void CreateMaze()
{
	static WORD data_1DE, data_1E0, data_1E2, data_1E4, data_1E6, data_1EA;
	static BYTE data_2AF, data_2B0;

	memset(data_350, 0xF, data_350_size);
	data_350[0] = 0xE;
	data_350[1] = 0xD;
	data_1EA = 0x13E;

main_283:
	if (!data_1EA)
		goto main_372;
	data_1DE = GetRandomRanged<data_350_size>();
	if (data_350[data_1DE] != 0xF)
		goto main_283;
	data_1E2 = GetRandomMasked(3);
	data_1E4 = 0;
	for (;;)
	{
		if (data_1E4 > 3)
			goto main_283;
		data_2AF = data_1E2 & 3;
		data_1E0 = data_1DE;
		CreateMaze_helper((SHORT&)data_1E0, data_2AF);
		if (data_350[data_1E0] != 0xF)
			break;
		data_1E2++;
		data_1E4++;
	}
	data_1EA--;
	static BYTE data_EA3[] = {8, 4, 2, 1};
	static BYTE data_EA7[] = {4, 8, 1, 2};
	data_350[data_1E0] ^= data_EA7[data_2AF];
	data_350[data_1DE] ^= data_EA3[data_2AF];
	data_1E2 = data_1DE;
main_30E:
	data_2AF = (BYTE)GetRandomMasked(3);
	data_2B0 = (BYTE)GetRandomMasked(3) + 1;
	data_1E0 = data_1E2;
	for (;;)
	{
		CreateMaze_helper((SHORT&)data_1E0, data_2AF);
		if (!data_2B0 || data_350[data_1E0] != 0xF)
			break;
		data_350[data_1E0] ^= data_EA7[data_2AF];
		data_350[data_1E2] ^= data_EA3[data_2AF];
		data_1EA--;
		data_2B0--;
		data_1E2 = data_1E0;
	}
	if (data_2B0)
		goto main_283;
	goto main_30E;
main_372:
	for (data_1DE = 1; data_1DE <= 0x40; data_1DE++)
	{
		data_1E0 = GetRandomRanged<data_350_size>();
		data_2AF = (BYTE)GetRandomMasked(3);
		data_350[data_1E0] &= ~data_EA3[data_2AF];
		CreateMaze_helper((SHORT&)data_1E0, data_2AF);
		data_350[data_1E0] &= ~data_EA7[data_2AF];
	}
	for (Uint i=0; i<_countof(maze); i++)
		maze[i] = 0x920;
	data_1E4 = 0;
	data_1E2 = 0;
	for (data_1DE = 0; data_1DE < MAZE_HEIGHT/MAZE_CELL_HEIGHT; data_1DE++)
	{
		for (data_1E0 = 0; data_1E0 < MAZE_WIDTH/MAZE_CELL_WIDTH; data_1E0++)
		{
			if (data_350[data_1E2] & 8)
				for (Uint i=0; i<MAZE_CELL_WIDTH-1; i++)
					maze[data_1E4 + 1 + i] = 0x9CD;
			if (data_350[data_1E2] & 2)
			{
				data_1E6 = data_1E4 + MAZE_WIDTH;
				for (WORD data_1E8 = 1; data_1E8 <= MAZE_CELL_HEIGHT-1; data_1E8++)
				{
					maze[data_1E6] = 0x9BA;
					data_1E6 += MAZE_WIDTH;
				}
			}
			if (data_1DE)
				data_1E6 = data_1E2 - (!data_1E0 ? 1 : 0x11);
			else
			{
				if (!data_1E0)
					data_1E6 = data_350_size - 1;
				else
					data_1E6 = data_1E2 + data_350_size - 0x11;
			}
			static BYTE data_EAB[] = {0x20, 0xBA, 0xBA, 0xBA, 0xCD, 0xBC, 0xBB, 0xB9, 0xCD, 0xC8, 0xC9, 0xCC, 0xCD, 0xCA, 0xCB, 0xCE};
			maze[data_1E4] = data_EAB[(data_350[data_1E2] & 0xA) | (data_350[data_1E6] & 0x5)] + 0x900;
			data_1E4 += MAZE_CELL_WIDTH;
			data_1E2++;
		}
		data_1E4 += MAZE_WIDTH * (MAZE_CELL_HEIGHT-1);
	}
}

BYTE main_CB0()
{
	BYTE data_C76 = data_C6C;
	if (data_C76)
		data_C6C = data_350[data_C76 * 8];
	return data_C76;
}

#define SPRITE_SIZE(x,y) (((x)<<8)+(y))

static const WORD data_1002[] = {SPRITE_SIZE(2,2), 0x0FDA, 0x0EBF, 0x0DC0, 0x0CD9};
static const WORD data_100C[] = {SPRITE_SIZE(2,2), 0x0EFF, 0x0EFF, 0x0EFF, 0x0EFF};
static const WORD data_1016[] = {SPRITE_SIZE(2,2), 0x0EDA, 0x0EBF, 0x0EC0, 0x0ED9};
static const WORD data_1020[] = {SPRITE_SIZE(2,2), 0x0EFF, 0x0EFF, 0x0EFF, 0x0EFF};
static const WORD data_102A[] = {SPRITE_SIZE(2,2), 0x0DDA, 0x0DBF, 0x0DC0, 0x0DD9};
static const WORD data_1034[] = {SPRITE_SIZE(2,2), 0x0EFF, 0x0EFF, 0x0EFF, 0x0EFF};
static const WORD data_103E[] = {SPRITE_SIZE(2,2), 0x0CDA, 0x0CBF, 0x0CC0, 0x0CD9};
static const WORD data_1048[] = {SPRITE_SIZE(2,2), 0x0EFF, 0x0EFF, 0x0EFF, 0x0EFF};
static const WORD data_1052[] = {SPRITE_SIZE(2,2), 0x0BDA, 0x0BBF, 0x0BC0, 0x0BD9};
static const WORD data_105C[] = {SPRITE_SIZE(2,2), 0x0EFF, 0x0EFF, 0x0EFF, 0x0EFF};
static const WORD data_1066[] = {SPRITE_SIZE(2,2), 0x0ADA, 0x0ABF, 0x0AC0, 0x0AD9};
static const WORD data_1070[] = {SPRITE_SIZE(2,2), 0x0EFF, 0x0EFF, 0x0EFF, 0x0EFF};
static const WORD data_107A[] = {SPRITE_SIZE(2,2), 0x09DA, 0x09BF, 0x09C0, 0x09D9};
static const WORD data_1084[] = {SPRITE_SIZE(2,2), 0x0EFF, 0x0EFF, 0x0EFF, 0x0EFF};
static const WORD data_108E[] = {SPRITE_SIZE(2,2), 0x04DA, 0x04BF, 0x04C0, 0x04D9};
static const WORD data_1098[] = {SPRITE_SIZE(2,2), 0x0EFF, 0x0EFF, 0x0EFF, 0x0EFF};
static const WORD *data_10A2[] = {data_1002, data_100C, data_1016, data_1020, data_102A, data_1034, data_103E, data_1048, data_1052, data_105C, data_1066, data_1070, data_107A, data_1084, data_108E, data_1098};
static const WORD data_10E2[] = {SPRITE_SIZE(2,2), 0x0F93, 0x0F93, 0x0F11, 0x0F10};
static const WORD data_10EC[] = {SPRITE_SIZE(2,2), 0x0F4F, 0x0F4F, 0x0F11, 0x0F10};
static const WORD *data_10F6[] = {data_10E2, data_10EC};
static const WORD data_10FE[] = {SPRITE_SIZE(1,1), 0x202};
static const WORD data_1108[] = {SPRITE_SIZE(2,1), 0x201, 0x218};
static const WORD data_1112[] = {SPRITE_SIZE(2,1), 0x201, 0x21A};
static const WORD data_111C[] = {SPRITE_SIZE(2,1), 0x201, 0x219};
static const WORD data_1126[] = {SPRITE_SIZE(2,1), 0x21B, 0x201};
static const WORD *data_1130[] = {data_1108, data_1112, data_1112, data_1112, data_111C, data_1126, data_1126, data_1126};
static const WORD data_1150[] = {SPRITE_SIZE(1,1), 0x0E09};
static const WORD data_115A[] = {SPRITE_SIZE(1,1), 0x0B0F};
static const WORD *data_11D4[] = {data_1150, data_115A, data_1150, data_115A};
static const WORD data_1164[] = {SPRITE_SIZE(1,1), 0x0A18};
static const WORD data_116E[] = {SPRITE_SIZE(1,1), 0x0A2F};
static const WORD data_1178[] = {SPRITE_SIZE(1,1), 0x0A1A};
static const WORD data_1182[] = {SPRITE_SIZE(1,1), 0x0A5C};
static const WORD data_118C[] = {SPRITE_SIZE(1,1), 0x0A19};
static const WORD data_1196[] = {SPRITE_SIZE(1,1), 0x0A2F};
static const WORD data_11A0[] = {SPRITE_SIZE(1,1), 0x0A1B};
static const WORD data_11AA[] = {SPRITE_SIZE(1,1), 0x0A5C};
static const WORD *data_11B4[] = {data_1164, data_116E, data_1178, data_1182, data_118C, data_1196, data_11A0, data_11AA};

static const WORD data_12C2[] = {SPRITE_SIZE(2,2), 0x0FB0, 0x0FB2, 0x0FB2, 0x0FB0};
static const WORD data_12CC[] = {SPRITE_SIZE(2,2), 0x0BB2, 0x0BB0, 0x0BB0, 0x0BB2};
static const WORD data_12D6[] = {SPRITE_SIZE(2,2), 0x0CB0, 0x0CB2, 0x0CB2, 0x0CB0};
static const WORD data_12E0[] = {SPRITE_SIZE(2,2), 0x04B2, 0x04B0, 0x04B0, 0x04B2};
static const WORD data_12EA[] = {SPRITE_SIZE(2,2), 0x062A, 0x060F, 0x062A, 0x060F};
static const WORD data_12F4[] = {SPRITE_SIZE(2,2), 0x0807, 0x0820, 0x0807, 0x0820};
static const WORD *data_12FE[] = {data_12C2, data_12CC, data_12D6, data_12E0, data_12EA, data_12F4};
static const WORD data_1316[] = {SPRITE_SIZE(2,1), 0x0FB0, 0x0FB2};
static const WORD data_1320[] = {SPRITE_SIZE(2,1), 0x0BB2, 0x0BB0};
static const WORD data_132A[] = {SPRITE_SIZE(2,1), 0x0CB0, 0x0CB2};
static const WORD data_1334[] = {SPRITE_SIZE(2,1), 0x04B2, 0x04B0};
static const WORD data_133E[] = {SPRITE_SIZE(2,1), 0x062A, 0x060F};
static const WORD data_1348[] = {SPRITE_SIZE(2,1), 0x0807, 0x0820};
static const WORD *data_1352[] = {data_1316, data_1320, data_132A, data_1334, data_133E, data_1348};
static const WORD data_136A[] = {SPRITE_SIZE(1,1), 0x0FB2};
static const WORD data_1374[] = {SPRITE_SIZE(1,1), 0x0B0F};
static const WORD data_137E[] = {SPRITE_SIZE(1,1), 0x0C09};
static const WORD data_1388[] = {SPRITE_SIZE(1,1), 0x0407};
static const WORD *data_1392[] = {data_136A, data_136A, data_136A, data_1374, data_137E, data_1388};

// fake pointers - hacky, but they work for now; should definitely replace them with real pointers once the porting is complete

#define FAKE_POINTER_data_1002 0x1002
#define FAKE_POINTER_data_100C 0x100C
#define FAKE_POINTER_data_1016 0x1016
#define FAKE_POINTER_data_1020 0x1020
#define FAKE_POINTER_data_102A 0x102A
#define FAKE_POINTER_data_1034 0x1034
#define FAKE_POINTER_data_103E 0x103E
#define FAKE_POINTER_data_1048 0x1048
#define FAKE_POINTER_data_1052 0x1052
#define FAKE_POINTER_data_105C 0x105C
#define FAKE_POINTER_data_1066 0x1066
#define FAKE_POINTER_data_1070 0x1070
#define FAKE_POINTER_data_107A 0x107A
#define FAKE_POINTER_data_1084 0x1084
#define FAKE_POINTER_data_108E 0x108E
#define FAKE_POINTER_data_1098 0x1098
#define FAKE_POINTER_data_10E2 0x10E2
#define FAKE_POINTER_data_10EC 0x10EC
#define FAKE_POINTER_data_10FE 0x10FE
#define FAKE_POINTER_data_1108 0x1108
#define FAKE_POINTER_data_1112 0x1112
#define FAKE_POINTER_data_111C 0x111C
#define FAKE_POINTER_data_1126 0x1126
#define FAKE_POINTER_data_1150 0x1150
#define FAKE_POINTER_data_115A 0x115A
#define FAKE_POINTER_data_1164 0x1164
#define FAKE_POINTER_data_116E 0x116E
#define FAKE_POINTER_data_1178 0x1178
#define FAKE_POINTER_data_1182 0x1182
#define FAKE_POINTER_data_118C 0x118C
#define FAKE_POINTER_data_1196 0x1196
#define FAKE_POINTER_data_11A0 0x11A0
#define FAKE_POINTER_data_11AA 0x11AA
#define FAKE_POINTER_data_12C2 0x12C2
#define FAKE_POINTER_data_12CC 0x12CC
#define FAKE_POINTER_data_12D6 0x12D6
#define FAKE_POINTER_data_12E0 0x12E0
#define FAKE_POINTER_data_12EA 0x12EA
#define FAKE_POINTER_data_12F4 0x12F4
#define FAKE_POINTER_data_1316 0x1316
#define FAKE_POINTER_data_1320 0x1320
#define FAKE_POINTER_data_132A 0x132A
#define FAKE_POINTER_data_1334 0x1334
#define FAKE_POINTER_data_133E 0x133E
#define FAKE_POINTER_data_1348 0x1348
#define FAKE_POINTER_data_136A 0x136A
#define FAKE_POINTER_data_1374 0x1374
#define FAKE_POINTER_data_137E 0x137E
#define FAKE_POINTER_data_1388 0x1388

const WORD *FakePointerToPointer(WORD fakePtr)
{
	switch (fakePtr)
	{
	case FAKE_POINTER_data_1002: return data_1002;
	case FAKE_POINTER_data_100C: return data_100C;
	case FAKE_POINTER_data_1016: return data_1016;
	case FAKE_POINTER_data_1020: return data_1020;
	case FAKE_POINTER_data_102A: return data_102A;
	case FAKE_POINTER_data_1034: return data_1034;
	case FAKE_POINTER_data_103E: return data_103E;
	case FAKE_POINTER_data_1048: return data_1048;
	case FAKE_POINTER_data_1052: return data_1052;
	case FAKE_POINTER_data_105C: return data_105C;
	case FAKE_POINTER_data_1066: return data_1066;
	case FAKE_POINTER_data_1070: return data_1070;
	case FAKE_POINTER_data_107A: return data_107A;
	case FAKE_POINTER_data_1084: return data_1084;
	case FAKE_POINTER_data_108E: return data_108E;
	case FAKE_POINTER_data_1098: return data_1098;
	case FAKE_POINTER_data_10E2: return data_10E2;
	case FAKE_POINTER_data_10EC: return data_10EC;
	case FAKE_POINTER_data_10FE: return data_10FE;
	case FAKE_POINTER_data_1108: return data_1108;
	case FAKE_POINTER_data_1112: return data_1112;
	case FAKE_POINTER_data_111C: return data_111C;
	case FAKE_POINTER_data_1126: return data_1126;
	case FAKE_POINTER_data_1150: return data_1150;
	case FAKE_POINTER_data_115A: return data_115A;
	case FAKE_POINTER_data_1164: return data_1164;
	case FAKE_POINTER_data_116E: return data_116E;
	case FAKE_POINTER_data_1178: return data_1178;
	case FAKE_POINTER_data_1182: return data_1182;
	case FAKE_POINTER_data_118C: return data_118C;
	case FAKE_POINTER_data_1196: return data_1196;
	case FAKE_POINTER_data_11A0: return data_11A0;
	case FAKE_POINTER_data_11AA: return data_11AA;
	case FAKE_POINTER_data_12C2: return data_12C2;
	case FAKE_POINTER_data_12CC: return data_12CC;
	case FAKE_POINTER_data_12D6: return data_12D6;
	case FAKE_POINTER_data_12E0: return data_12E0;
	case FAKE_POINTER_data_12EA: return data_12EA;
	case FAKE_POINTER_data_12F4: return data_12F4;
	case FAKE_POINTER_data_1316: return data_1316;
	case FAKE_POINTER_data_1320: return data_1320;
	case FAKE_POINTER_data_132A: return data_132A;
	case FAKE_POINTER_data_1334: return data_1334;
	case FAKE_POINTER_data_133E: return data_133E;
	case FAKE_POINTER_data_1348: return data_1348;
	case FAKE_POINTER_data_136A: return data_136A;
	case FAKE_POINTER_data_1374: return data_1374;
	case FAKE_POINTER_data_137E: return data_137E;
	case FAKE_POINTER_data_1388: return data_1388;
	default:
		return NULL;
	}
}

WORD PointerToFakePointer(const WORD *ptr)
{
	if (ptr == data_1002) return FAKE_POINTER_data_1002;
	if (ptr == data_100C) return FAKE_POINTER_data_100C;
	if (ptr == data_1016) return FAKE_POINTER_data_1016;
	if (ptr == data_1020) return FAKE_POINTER_data_1020;
	if (ptr == data_102A) return FAKE_POINTER_data_102A;
	if (ptr == data_1034) return FAKE_POINTER_data_1034;
	if (ptr == data_103E) return FAKE_POINTER_data_103E;
	if (ptr == data_1048) return FAKE_POINTER_data_1048;
	if (ptr == data_1052) return FAKE_POINTER_data_1052;
	if (ptr == data_105C) return FAKE_POINTER_data_105C;
	if (ptr == data_1066) return FAKE_POINTER_data_1066;
	if (ptr == data_1070) return FAKE_POINTER_data_1070;
	if (ptr == data_107A) return FAKE_POINTER_data_107A;
	if (ptr == data_1084) return FAKE_POINTER_data_1084;
	if (ptr == data_108E) return FAKE_POINTER_data_108E;
	if (ptr == data_1098) return FAKE_POINTER_data_1098;
	if (ptr == data_10E2) return FAKE_POINTER_data_10E2;
	if (ptr == data_10EC) return FAKE_POINTER_data_10EC;
	if (ptr == data_10FE) return FAKE_POINTER_data_10FE;
	if (ptr == data_1108) return FAKE_POINTER_data_1108;
	if (ptr == data_1112) return FAKE_POINTER_data_1112;
	if (ptr == data_111C) return FAKE_POINTER_data_111C;
	if (ptr == data_1126) return FAKE_POINTER_data_1126;
	if (ptr == data_1150) return FAKE_POINTER_data_1150;
	if (ptr == data_115A) return FAKE_POINTER_data_115A;
	if (ptr == data_1164) return FAKE_POINTER_data_1164;
	if (ptr == data_116E) return FAKE_POINTER_data_116E;
	if (ptr == data_1178) return FAKE_POINTER_data_1178;
	if (ptr == data_1182) return FAKE_POINTER_data_1182;
	if (ptr == data_118C) return FAKE_POINTER_data_118C;
	if (ptr == data_1196) return FAKE_POINTER_data_1196;
	if (ptr == data_11A0) return FAKE_POINTER_data_11A0;
	if (ptr == data_11AA) return FAKE_POINTER_data_11AA;
	if (ptr == data_12C2) return FAKE_POINTER_data_12C2;
	if (ptr == data_12CC) return FAKE_POINTER_data_12CC;
	if (ptr == data_12D6) return FAKE_POINTER_data_12D6;
	if (ptr == data_12E0) return FAKE_POINTER_data_12E0;
	if (ptr == data_12EA) return FAKE_POINTER_data_12EA;
	if (ptr == data_12F4) return FAKE_POINTER_data_12F4;
	if (ptr == data_1316) return FAKE_POINTER_data_1316;
	if (ptr == data_1320) return FAKE_POINTER_data_1320;
	if (ptr == data_132A) return FAKE_POINTER_data_132A;
	if (ptr == data_1334) return FAKE_POINTER_data_1334;
	if (ptr == data_133E) return FAKE_POINTER_data_133E;
	if (ptr == data_1348) return FAKE_POINTER_data_1348;
	if (ptr == data_136A) return FAKE_POINTER_data_136A;
	if (ptr == data_1374) return FAKE_POINTER_data_1374;
	if (ptr == data_137E) return FAKE_POINTER_data_137E;
	if (ptr == data_1388) return FAKE_POINTER_data_1388;
	__debugbreak();
	return 0;
}

static const BYTE data_11E8[] = {
	0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,
	0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,
	0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0
};
static const BYTE data_CE5[] = {
	1,2,1,1,2,1,1,2,1,1,2,1,1,2,1,1,2,1,1,2,1,1,2,1,1,2,1,1,2,1,1,2,1,1,2,1,1,2,1,1,2,1,
	1,2,1,1,2,1,1,2,1,1,2,1,1,2,1,1,2,1,1,2,1,1,2,1,1,2,1,1,2,1,1,2,1,1,2,1,1,2,1,1,2,1,
	1,2,1,1,2,1,1,2,1,1,2,1,1,2,1,1,2,1,1,2,1,1,2,1,1,2,1,1,2,1,1,2,1,1,2,1,1
};

static const BYTE data_1261[] = {4, 3, 4, 4, 4, 4, 4, 5, 6, 7, 6, 5, 6, 6, 6, 6, 0, 0, 0, 1, 0, 7, 0, 0, 2, 2, 2, 2, 2, 3, 2, 1};
static const BYTE data_1281[] = {0xB9, 0xBA, 0xBB, 0xBC, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE};
static const BYTE data_128C[] = {1, 2, 0x18, 0x1A, 0x19, 0x1B};
static const BYTE data_1292[] = {0xB9, 0xBA, 0xBB, 0xBC, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE};
static const BYTE data_129D[] = {1, 2, 0x18, 0x1A, 0x19, 0x1B};

void main_CCF(BYTE arg)
{
	data_350[arg * 8] = data_C6C;
	data_C6C = arg;
}

bool main_CED(WORD arg1, BYTE arg2)
{
	BYTE data_C79 = ((BYTE*)data_34C)[0] - 1;
	BYTE data_C7A = ((BYTE*)data_34C)[1] - 1;
	WORD data_B50 = (WORD)arg1 * MAZE_WIDTH;
	for (BYTE data_C7B = 0; data_C7B <= data_C79; data_C7B++)
	{
		BYTE data_C7D = arg2;
		for (BYTE data_C7C = 0; data_C7C <= data_C7A; data_C7C++)
		{
			if ((BYTE&)maze[data_C7D + data_B50] != ' ')
				return true;
			if (++data_C7D >= MAZE_WIDTH)
				data_C7D = 0;
		}
		data_B50 += MAZE_WIDTH;
		if (data_B50 >= _countof(maze))
			data_B50 -= _countof(maze);
	}
	return false;
}

void PlotObjectToMaze() // plots object data_34A with sprite data_34C
{
	BYTE spriteHeight = ((BYTE*)data_34C)[0];
	BYTE spriteWidth  = ((BYTE*)data_34C)[1];
	BYTE data_C83 = 0;
	WORD data_B52 = (WORD)data_34A[3] * MAZE_WIDTH;
	for (BYTE data_C81 = 0; data_C81 < spriteHeight; data_C81++)
	{
		BYTE data_C80 = data_34A[2];
		for (BYTE data_C82 = 0; data_C82 < spriteWidth; data_C82++)
		{
			maze[data_B52 + data_C80] = data_34C[1 + data_C83];
			data_C83++;
			if (++data_C80 >= MAZE_WIDTH)
				data_C80 = 0;
		}
		data_B52 += MAZE_WIDTH;
		if (data_B52 >= _countof(maze))
			data_B52 -= _countof(maze);
	}
}

void main_F77()
{
	data_34A[2] = GetRandomMasked(0xF) * 8 + 4;
	while (main_CED(data_34A[3] = (BYTE)GetRandomRanged<0x14>() * 6 + 3, data_34A[2])) {}
}

void CreateGenerators()
{
	for (WORD data_B58 = 0; data_B58 <= 0xFC; data_B58++)
		data_350[(data_B58 + 1) * 8] = data_B58 + 2;
	data_350[2024] = 0;
	data_C6C = 1;
	data_C6D = 0;
	data_C6F = 0;
	data_C71 = 0;
	data_C6E = 0;
	data_C70 = 0;
	for (WORD data_B58 = 1; data_B58 <= numGenerators; data_B58++)
	{
		BYTE data_B5A = main_CB0();
		data_350[data_B5A * 8] = data_C70;
		data_C70 = data_B5A;
		data_34A = &data_350[data_B5A * 8];
		(WORD&)data_34A[6] = FAKE_POINTER_data_1002;
		data_34C = data_1002;
		main_F77();
		data_34A[1] = 0;
		data_34A[5] = (BYTE)GetRandomMasked(0xF);
		data_34A[4] = 1;
		PlotObjectToMaze();
	}
	data_B65 = numGenerators;
	data_348 = 0;
	data_B68 = 0;
	data_B67 = 0;
	data_346 = 0;
	data_B66 = 0;
	data_B64 = 0;
	data_34E = 0;
	data_C75 = false;
	data_C74 = 0;
	data_C73 = false;
	data_C72 = false;
	(WORD&)data_350[6] = FAKE_POINTER_data_10E2;
	data_34C = data_10E2;
	data_34A = data_350;
	main_F77();
	PlotObjectToMaze();
	data_1CA = data_350[2];
	data_1CC = data_350[3];
	data_350[5] = 1;
	data_350[1] = 1;
}

void SetSoundEffectState(BYTE arg1, BYTE arg2)
{
	if (data_DF0 != 0xFF && arg2 < data_DF0)
		return;
	if (!shooting_sound_enabled && !arg2)
		return;
	data_DF1 = arg1;
	data_DF0 = arg2;
}

bool updateHUD() // returns true if the match has been won
{
	frame++;
	if (data_28E != data_346)
		outputNumber(0x13, 0, 4, 2* 11, data_28E = data_346);
	if (data_290 != data_348)
		outputNumber(0x13, 0, 4, 2* 20, data_290 = data_348);
	if (data_2B3 != data_B65)
	{
		outputNumber(0x17, 0, 2, 2* 43, data_2B3 = data_B65);
		outputNumber(0x13, 0, 2, 2*  3, numGenerators - data_B65);
	}
	if (data_2B2 != data_B64)
		outputNumber(0x17, 0, 3, 2* 52, data_2B2 = data_B64);
	if (data_2B4 != data_B68)
		outputNumber(0x17, 0, 3, 2* 61, data_2B4 = data_B68);
	if (data_292 != data_34E)
	{
		data_292 = data_34E;
		if (data_292 > 0)
			outputNumber(0x17, 1, 5, 2*113, data_292);
		else
			outputText  (0x17,    6, 2*113, statusLine+65);
	}
	if (data_2C0 != data_B66)
	{
		data_2C0 = data_B66;
		BYTE livesRemaining = numLives - data_2C0;
		if (livesRemaining == 1)
			outputText  (0x1C,   10, 2* 80, statusLine+71);
		else
		{
			{}	outputNumber(0x17, 0, 1, 2* 80, livesRemaining);
			if (!livesRemaining)
			{
				outputNumber(0x1C, 0, 1, 2* 80, 0);
				outputText  (0x1C,    1, 2*113, statusLine+81);
			}
		}
	}
	outputNumber(0x17, 0, 5, 2*74, frame); // Time

	if (data_B64 || data_B65 || data_B68)
		return false;

	EraseBottomTwoLines();

	COORD pos;
	pos.X = 0;
	pos.Y = 25-2;
	SetConsoleCursorPosition(output, pos);
	SetConsoleTextAttribute(output, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED);
	SetConsoleMode(output, ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT);
	DWORD operationSize;
	WriteConsole(output, STRING_WITH_LEN("Congratulations --- YOU ACTUALLY WON!!!\r\n"), &operationSize, 0);
	return true;
}

void main_1C28(BYTE arg)
{
	if (!arg)
	{
		arg = 0xFE;
		data_350[0x7F2] = data_350[2];
		data_350[0x7F3] = data_350[3];
		data_350[0x7F4] = 0x16;
		(WORD&)data_350[0x7F6] = FAKE_POINTER_data_12C2;
		data_C72 = true;
	}
	data_34A = &data_350[arg * 8];
	data_34A[0] = data_C6F;
	data_C6F = arg;
	const WORD *data_CC4 = FakePointerToPointer((WORD&)data_34A[6]);
	BYTE data_CC8 = ((BYTE*)data_CC4)[0];
	BYTE data_CC9 = ((BYTE*)data_CC4)[1];
	BYTE *data_CC2 = data_34A;
	if (data_CC8 == 2 && data_CC9 == 2)
	{
		(WORD&)data_CC2[6] = FAKE_POINTER_data_12C2;
		data_34C = data_12C2;
		data_CC2[4] = 0x16;
		data_CC2[5] = 0;
		SetSoundEffectState(0, 4);
	}
	if (data_CC8 == 1 && data_CC9 == 2)
	{
		(WORD&)data_CC2[6] = FAKE_POINTER_data_1316;
		data_34C = data_1316;
		data_CC2[4] = 0xC;
		data_CC2[5] = 0;
		SetSoundEffectState(0, 3);
	}
	if (data_CC8 == 1 && data_CC9 == 1)
	{
		(WORD&)data_CC2[6] = FAKE_POINTER_data_136A;
		data_34C = data_136A;
		data_CC2[4] = 0xB;
		data_CC2[5] = 2;
		SetSoundEffectState(2, 2);
	}
	PlotObjectToMaze();
}

void main_1D04(BYTE *arg1, BYTE arg2)
{
	BYTE data_CCA = *arg1;
	if (arg2 == data_CCA)
	{
		*arg1 = data_350[arg2 * 8];
		return;
	}
	for (;;)
	{
		BYTE data_CCB = data_350[data_CCA * 8];
		if (!data_CCB)
			return;
		if (arg2 == data_CCB)
			break;
		data_CCA = data_CCB;
	}
	data_350[data_CCA * 8] = data_350[arg2 * 8];
}

void main_1E8F(BYTE *arg1, BYTE arg2)
{
	if (!arg2)
	{
		main_1C28(0);
		return;
	}
	main_1D04(arg1, arg2);
	if (arg1 != &data_C6D)
	{
		main_1C28(arg2);
		return;
	}
	main_CCF(arg2);
}

bool main_154F(BYTE arg)
{
	switch (data_C96 = arg)
	{
	case 0:
		data_B5C -= MAZE_WIDTH;
		if (--data_34A[3] == 0xFF)
		{
			data_34A[3] = MAZE_HEIGHT - 1;
			data_B5C += _countof(maze);
		}
		break;
	case 1:
		data_B5C++;
		if (++data_34A[2] >= MAZE_WIDTH)
		{
			data_34A[2] = 0;
			data_B5C -= MAZE_WIDTH;
		}
		break;
	case 2:
		data_B5C += MAZE_WIDTH;
		if (++data_34A[3] >= MAZE_HEIGHT)
		{
			data_34A[3] = 0;
			data_B5C -= _countof(maze);
		}
		break;
	case 3:
		data_B5C--;
		if (--data_34A[2] == 0xFF)
		{
			data_34A[2] = MAZE_WIDTH - 1;
			data_B5C += MAZE_WIDTH;
		}
		break;
	}
	return (BYTE&)maze[data_B5C] != ' ';
}

void main_125C()
{
	BYTE data_C94 = 0;
	BYTE data_C93 = data_C6D;
	for (;;)
	{
		if (!data_C93)
			return;
		data_34A = &data_350[data_C93 * 8];
		data_B5C = data_34A[3] * MAZE_WIDTH + data_34A[2];
		if ((BYTE&)maze[data_B5C] == 0xB2)
		{
			BYTE data_C95 = data_34A[0];
			maze[data_B5C] = 0x920;
			main_1E8F(&data_C6D, data_C93);
			data_B67--;
			data_C93 = data_C95;
			continue;
		}
		maze[data_B5C] = 0x920;
		switch (data_34A[4])
		{
		case 1:
			if (main_154F(1) || data_11E8[data_34A[3]] && main_154F(1))
				goto main_13EF;
			goto main_1390;
		case 3:
			if (main_154F(2) || data_11E8[data_34A[3]] && main_154F(1))
				goto main_13EF;
			// fall through
		case 2:
			if (main_154F(1))
				goto main_13EF;
			goto main_139A;
		case 4:
			if (main_154F(2))
				goto main_13EF;
			goto main_139A;
		case 5:
			if (main_154F(2) || data_11E8[data_34A[3]] && main_154F(3))
				goto main_13EF;
			// fall through
		case 6:
			if (main_154F(3))
				goto main_13EF;
			goto main_139A;
		case 7:
			if (main_154F(3) || data_11E8[data_34A[3]] && main_154F(3))
				goto main_13EF;
			// fall through
		case 0:
		main_1390:
			if (main_154F(0))
				goto main_13EF;
		main_139A:
			if (data_34A[1] >= 6)
				data_34C = FakePointerToPointer((WORD&)data_34A[6]);
			else
			{
				if (++data_34A[5] > 3)
					data_34A[5] = 0;
				data_34C = data_11D4[data_34A[5]];
			}
			maze[data_B5C] = data_34C[1];
			data_C94 = data_C93;
			data_C93 = data_34A[0];
			continue;
		}
	main_13EF:
		BYTE find_this = (BYTE&)maze[data_B5C];
		if (!memchr(data_1281, find_this, _countof(data_1281)))
		{
			if (!data_34A[1])
			{
				if (memchr(data_128C, find_this, _countof(data_128C)))
				{
					data_34E++;
					goto main_149B;
				}
				WORD find_this = maze[data_B5C];
				if (!wmemchr((wchar_t*)&data_1002[1], (wchar_t&)find_this, _countof(data_1002)-1) && (BYTE&)find_this != 0xFF)
					goto main_149B;
				data_34E += 50;
				goto main_149B;
			}
			if (skillThing7)
				goto main_149B;
			WORD find_this = maze[data_B5C];
			if (wmemchr((wchar_t*)&data_1002[1], (wchar_t&)find_this, _countof(data_1002)-1) && (BYTE&)find_this != 0xFF)
				goto main_150E;
		main_149B:
			maze[data_B5C] = 0x0FB2;
			goto main_150E;
		}
		if (!enableRubberBullets || data_34A[1] || !data_B6C[data_C93] || !(data_34A[4] & 1))
			goto main_150E;
		data_B6C[data_C93]--;
		data_34A[4] = data_1261[data_C96 * 8 + data_34A[4]];
		SetSoundEffectState(1, 0);
		main_154F((data_C96 + 2) & 3);
		goto main_139A;
	main_150E:
		data_B67--;
		if (!data_C94)
			data_C6D = data_34A[0];
		else
			data_350[data_C94 * 8] = data_34A[0];
		BYTE data_C95 = data_34A[0];
		main_CCF(data_C93);
		data_C93 = data_C95;
	}
}

BYTE main_2381(BYTE *di, WORD &cx)
{
	SHORT bx = 1;
	SHORT ax = di[2] - data_1CA;
	if (ax <= 0)
	{
		bx = 0;
		ax = -ax;
	}
	if (ax > 0x3F)
	{
		bx ^= 1;
		ax = -ax + MAZE_WIDTH;
	}
	(BYTE&)cx = (BYTE&)ax;
	(BYTE&)ax = di[3];
	ax -= data_1CC;
	if (ax < 0)
	{
		bx |= 2;
		ax = -ax;
	}
	if (ax > 0x3B)
	{
		bx |= 2;
		ax = -ax + MAZE_HEIGHT;
	}
	((BYTE*)&cx)[1] = (BYTE&)ax;
	data_B69 = (BYTE&)ax += (BYTE&)cx;
	if (!(BYTE&)cx)
		return (BYTE&)bx * 2;
	if (!((BYTE*)&cx)[1])
		return (BYTE&)bx * 4 + 2;
	static const BYTE data_CD1[] = {1, 7, 3, 5};
	return data_CD1[bx];
}

struct main_227E_retval {bool al; BYTE ah; WORD cx;};
main_227E_retval main_227E(BYTE *di)
{
	union
	{
		WORD ax;
		struct {BYTE al, ah;};
	};
	union
	{
		WORD cx;
		struct {BYTE cl, ch;};
	};
	union
	{
		WORD dx;
		struct {BYTE dl, dh;};
	};
	cx = (WORD&)di[2];
	ax = cx;
	dx = 0;
	int tmp;
	switch (di[4])
	{
	case 1:
		tmp = cl + data_CE5[ch];
		if (tmp >= MAZE_WIDTH)
			tmp -= MAZE_WIDTH;
		cl = tmp;
		// fall through
	case 0:
		dh++;
		tmp = ch-1;
		if (tmp < 0)
			tmp = MAZE_HEIGHT - 1;
		ch = tmp;
		ah = ch;
		break;
	case 2:
		dl++;
		tmp = cl + 1;
		if (tmp >= MAZE_WIDTH)
			tmp = 0;
		cl = tmp;
		break;
	case 3:
		dh++;
		tmp = ch + 1;
		if (tmp >= MAZE_HEIGHT)
			tmp = 0;
		ch = tmp;
		dl = data_CE5[ch];
		tmp = cl + dl;
		if (tmp >= MAZE_WIDTH)
			tmp -= MAZE_WIDTH;
		cl = tmp;
		break;
	case 4:
		dh++;
		tmp = ch + 1;
		if (tmp >= MAZE_HEIGHT)
			tmp = 0;
		ch = tmp;
		break;
	case 5:
		dh++;
		tmp = ch + 1;
		if (tmp >= MAZE_HEIGHT)
			tmp = 0;
		ch = tmp;
		dl = data_CE5[ch];
		tmp = cl - dl;
		if (tmp < 0)
			tmp += MAZE_WIDTH;
		cl = tmp;
		al = cl;
		break;
	case 6:
		dl++;
		tmp = cl - 1;
		if (tmp < 0)
			tmp = MAZE_WIDTH - 1;
		cl = tmp;
		al = cl;
		break;
	case 7:
		dl = data_CE5[ch];
		tmp = cl - dl;
		if (tmp < 0)
			tmp += MAZE_WIDTH;
		cl = tmp;
		dh++;
		tmp = ch - 1;
		if (tmp < 0)
			tmp = MAZE_HEIGHT - 1;
		ch = tmp;
		ax = cx;
		break;
	}
	const WORD *ptr = FakePointerToPointer((WORD&)di[6]);
	dl += ((BYTE*)ptr)[1];
	dh += ((BYTE*)ptr)[0];
	WORD *si = &maze[ah * MAZE_WIDTH];
main_233B:
	size_t bx = al;
	ah = dl;
main_2343:
	if ((BYTE&)si[bx] != ' ')
	{
		main_227E_retval retval;
		retval.al = true;
		retval.ah = (BYTE&)si[bx];
		retval.cx = cx;
		return retval;
	}
	if (--ah)
	{
		if (++bx >= MAZE_WIDTH)
			bx = 0;
		goto main_2343;
	}
	if (--dh)
	{
		si += MAZE_WIDTH;
		if (si >= &maze[_countof(maze)])
			si -=       _countof(maze);
		goto main_233B;
	}
	data_C77 = cl;
	data_C78 = ch;
	main_227E_retval retval;
	retval.al = false;
	retval.ah = 0;
	retval.cx = cx;
	return retval;
}

void main_1613(BYTE arg)
{
	BYTE data_C97 = data_34A[4];
	BYTE data_C98 = data_34A[2];
	BYTE data_C99 = data_34A[3];
	switch (data_C97)
	{
	case 1:
		data_C98 += ((BYTE*)data_34C)[1];
		goto main_16A1;
	case 2:
		data_C98 += ((BYTE*)data_34C)[1];
		break;
	case 3:
		data_C98 += ((BYTE*)data_34C)[1];
		goto main_168F;
	case 4:
		data_C99 += ((BYTE*)data_34C)[0];
		data_C98 = data_C98 + ((BYTE*)data_34C)[1] - 1;
		break;
	case 5:
		data_C98--;
	main_168F:
		data_C99 += ((BYTE*)data_34C)[0];
		break;
	case 6:
		data_C98--;
		break;
	case 7:
		data_C98--;
		// fall through
	case 0:
	main_16A1:
		data_C99--;
		break;
	}
	if (data_C98 >= MAZE_WIDTH)
	{
		if (data_C98 > 240)
			data_C98 += MAZE_WIDTH;
		else
			data_C98 -= MAZE_WIDTH;
	}
	if (data_C99 >= MAZE_HEIGHT)
	{
		if (data_C99 > 240)
			data_C99 += MAZE_HEIGHT;
		else
			data_C99 -= MAZE_HEIGHT;
	}
	const WORD *data_B60 = data_34C;
	data_34C = data_1150;
	if (!main_CED(data_C99, data_C98))
		goto main_17C3;
	WORD data_B5E = data_C99 * MAZE_WIDTH + data_C98;
	if (memchr(data_1292, (BYTE&)maze[data_B5E], _countof(data_1292)))
		goto main_1899;
	if (arg)
		goto main_1786;
	if (memchr(data_129D, (BYTE&)maze[data_B5E], _countof(data_129D)))
	{
		data_34E++;
		goto main_17B4;
	}
	if (!wmemchr((wchar_t*)&data_1002[1], (wchar_t&)maze[data_B5E], _countof(data_1002)-1))
	{
		if ((BYTE&)maze[data_B5E] != 0xFF)
			goto main_17B4;
	}
	data_34E += 50;
	goto main_17B4;
main_1786:
	if (skillThing7)
		goto main_17B4;
	if (wmemchr((wchar_t*)&data_1002[1], (wchar_t&)maze[data_B5E], _countof(data_1002)-1))
		goto main_1899;
	if ((BYTE&)maze[data_B5E] == 0xFF)
		goto main_1899;
main_17B4:
	maze[data_B5E] = 0x0FB2;
	goto main_1899;
main_17C3:
	BYTE data_C9B = main_CB0();
	if (!data_C9B || data_B67 > 50)
		goto main_1899;
	data_B67++;
	BYTE *data_B62 = data_34A;
	data_34A = &data_350[data_C9B * 8];
	if (!data_C6D)
	{
		data_C6D = data_C9B;
		goto main_182D;
	}
	BYTE data_C9A = data_C6D;
	for (;;)
	{
		if (BYTE tmp = data_350[data_C9A * 8])
			data_C9A = tmp;
		else
			break;
	}
	data_350[data_C9A * 8] = data_C9B;
main_182D:
	data_34A[0] = 0;
	if (arg < 6)
	{
		(WORD&)data_34A[6] = FAKE_POINTER_data_1150;
		goto main_1854;
	}
	(WORD&)data_34A[6] = PointerToFakePointer(data_11B4[data_C97]);
main_1854:
	data_34A[2] = data_C98;
	data_34A[3] = data_C99;
	data_34A[4] = data_C97;
	data_34A[5] = 0;
	data_34A[1] = arg;
	data_B6C[data_C9B] = (BYTE)GetRandomMasked(7) + 1;
	data_34C = FakePointerToPointer((WORD&)data_34A[6]);
	PlotObjectToMaze();
	data_34A = data_B62;
main_1899:
	data_34C = data_B60;
}

BYTE main_18A3()
{
	BYTE data_C9C = data_B69 << skillThing1;
	if (data_C9C > 10)
		return 0;
	if (GetRandomMasked(0xFFFF >> (15 - data_C9C)))
		return 0;
	main_1613(6);
	SetSoundEffectState(0, 1);
	return 0xFF;
}

void main_1EC1()
{
	BYTE dl = data_C6E;
	while (dl)
	{
		BYTE *di = &data_350[dl * 8];
		WORD *bx_si = &maze[di[3] * MAZE_WIDTH + di[2]];
		if (*(BYTE*)bx_si == 0xB2)
			goto main_1F15;
		if (di[2] >= MAZE_WIDTH-1)
			goto main_1F06;
		if ((BYTE&)bx_si[1] == 0xB2)
			goto main_1F27;
		if (--di[5])
		{
			dl = di[0];
			continue;
		}
		goto main_1F84;
	main_1F06:
		if ((BYTE&)maze[di[3] * MAZE_WIDTH] == 0xB2)
			goto main_1F27;
		if (--di[5] == 0)
			goto main_1F8F;
		dl = di[0];
		continue;
	main_1F15:
		*bx_si = 0x920;
		if (di[2] < MAZE_WIDTH-1)
			bx_si++;
		else
			bx_si = &maze[di[3] * MAZE_WIDTH];
		goto main_1F33;
	main_1F27:
		;
	main_1F33:
		if (!(skillThing2 & 1))
			goto main_1FB9;
		if (*(BYTE*)bx_si != 1)
			goto main_1FB9;
		*bx_si = 0x502;
		di[2] = bx_si - &maze[di[3] * MAZE_WIDTH];
		BYTE *bx = &data_C6E;
		BYTE *si;
		do
		{
			si = bx;
			bx = &data_350[*bx * 8];
		}
		while (bx != di);

		{
			BYTE a = si[0] = di[0];
			{BYTE tmp = data_C71; data_C71 = dl; dl = tmp;}
			di[0] = dl;
			dl = a;
		}
		di[5] = 2;
		(WORD&)di[6] = FAKE_POINTER_data_10FE;
		data_B64--;
		data_346++;
		data_B68++;
		continue;
	main_1F84:
		*bx_si = 0x920;
		bx_si--;
		goto main_1F97;
	main_1F8F:
		*bx_si = 0x920;
		bx_si = &maze[di[3] * MAZE_WIDTH + MAZE_WIDTH-1];
	main_1F97:
		*bx_si = 0x920;
		if (GetRandomMasked(3) == 0)
			goto main_1FCF;
		{
			main_227E_retval result = main_227E(di);
			if (result.al)
				goto main_1FCF;
			(WORD&)di[2] = result.cx;
		}
		if (!(di[4] & 1))
			goto main_2021;
		di[5] = 8;
		goto main_2025;
	main_1FB9:
		{
			BYTE tmp = di[0];
			main_1E8F(&data_C6E, dl);
			dl = tmp;
		}
		data_B64--;
		data_346++;
		continue;
	main_1FCF:
		if (GetRandomMasked(3) == 0)
			di[4] = (BYTE)GetRandomMasked(7);
		else
		{
			WORD dummy;
			di[4] = main_2381(di, dummy);
		}
		{
			WORD cx = 8;
			do
			{
				main_227E_retval result = main_227E(di);
				if (!result.al)
					break;
				BYTE al = di[4];
				if (di[1] & 1)
					al = (al - 1) & 7;
				else
					al = (al + 1) & 7;
				di[4] = al;
			}
			while (--cx);
		}
		(WORD&)di[6] = PointerToFakePointer(data_1130[di[4]]);
	main_2021:
		di[5] = 6;
	main_2025:
		const WORD *sprite = FakePointerToPointer((WORD&)di[6]);
		maze[di[3] * MAZE_WIDTH + di[2]] = sprite[1];
		if (di[2] < MAZE_WIDTH-1)
			maze[di[3] * MAZE_WIDTH + di[2]+1] = sprite[2];
		else
			maze[di[3] * MAZE_WIDTH          ] = sprite[2];
		WORD cx;
		BYTE al = main_2381(di, cx);
		BYTE ah = di[4];
		if (al != ah)
			goto main_209E;
		if (!((BYTE*)&cx)[1] || !(BYTE&)cx)
			goto main_2083;
		if (abs((int)!(BYTE&)cx * MAZE_CELL_HEIGHT - (int)!((BYTE*)&cx)[1] * MAZE_CELL_WIDTH) >= MAZE_CELL_WIDTH)
			goto main_20F4;
		al = di[4];
	main_2083:
		{
			BYTE tmp = di[4];
			di[4] = al;
			data_34A = di;
			data_34C = FakePointerToPointer((WORD&)di[6]);
			al = main_18A3();
			di[4] = tmp;
		}
		goto main_20F9;
	main_209E:
		if (((BYTE*)&cx)[1] > 2 || al == 0 || al == 4)
			goto main_20C3;
		if (al > 4)
			goto main_20BA;
		if (!ah || ah > 3)
			goto main_20C3;
		al = 2;
		goto main_2083;
	main_20BA:
		if (ah >= 5)
		{
			al = 6;
			goto main_2083;
		}
	main_20C3:
		if ((BYTE&)cx > 2)
			goto main_20F4;
		al = (al + 1) & 7;
		if (al == 7 || al == 3)
			goto main_20F4;
		if (al > 3)
			goto main_20E5;
		ah = (ah + 1) & 7;
		if (ah > 2)
			goto main_20F4;
		al = 0;
		goto main_2083;
	main_20E5:
		ah = (ah + 2) & 7;
		if (ah >= 5)
		{
			al = 4;
			goto main_2083;
		}
	main_20F4:
		dl = di[0];
		continue;
	main_20F9:
		BYTE tmp = al & 1;
		al >>= 1;
		if (!tmp)
			goto main_20F4;
		if ((BYTE&)maze[di[3] * MAZE_WIDTH + di[2]] != 1)
			maze[di[3] * MAZE_WIDTH + di[2]] = 0x9FF;
		else
		{
			if (di[2] < MAZE_WIDTH-1)
				maze[di[3] * MAZE_WIDTH + di[2]+1] = 0x9FF;
			else
				maze[di[3] * MAZE_WIDTH          ] = 0x9FF;
		}
		goto main_20F4;
	}
}

void main_2124()
{
	BYTE dl = data_C71;
	while (dl)
	{
		BYTE *di = &data_350[dl * 8];
		WORD &bx_si = maze[di[3] * MAZE_WIDTH + di[2]];
		if ((BYTE&)bx_si != 0xB2)
		{
			if (--di[5])
			{
				dl = di[0];
				continue;
			}
		}
		else
		{
	main_2158:
			BYTE tmp = di[0];
			main_1E8F(&data_C71, dl);
			dl = tmp;
			data_B68--;
			data_348--;
			continue;
		}
		bx_si = 0x920;
		BYTE data_CD0 = dl;
		if (di[1] & 2)
			goto main_2228;
		WORD cx;
		BYTE al = main_2381(di, cx);
		if (data_B69 > 4)
			goto main_21C5;
		di[4] = al;
		{
			main_227E_retval ax = main_227E(di);
			if (!ax.al)
				goto main_21C5;
			if (di[4] & 1)
				goto main_21C5;
			if (al & 1)
			{
				if (ax.ah == 0x93 || ax.ah == 0x4F || ax.ah == 0x11 || ax.ah == 0x10) // player character sprite
				{
					if (GetRandomMasked(skillThing3) == 0)
					{
						dl = data_CD0;
						(BYTE&)bx_si = 0xB2;
						goto main_2158;
					}
				}
			}
		}
	main_21C5:
		if (((BYTE*)&cx)[1] < 1)
			goto main_21D8;
		if (((BYTE*)&cx)[1] > 1)
			goto main_21EC;
		BYTE tmp = al;
		al = 2;
		if (tmp >= 4)
			al = 6;
		goto main_220B;
	main_21D8:
		di[4] = ++al;
		{
			main_227E_retval ax = main_227E(di);
			if (!ax.al)
				goto main_225A;
		}
		al -= 2;
		goto main_220B;
	main_21EC:
		if ((BYTE&)cx == 1)
			al = (al + 1) & 4;
		else
		if ((BYTE&)cx > 1)
		{
			di[4] = al += 2;
			main_227E_retval ax = main_227E(di);
			if (!ax.al)
				goto main_225A;
			al = (al - 4) & 7;
		}
	main_220B:
		di[4] = al;
		{
			main_227E_retval ax = main_227E(di);
			if (!ax.al)
				goto main_225A;
		}
		if (data_B69 >= 0x14)
			di[1] |= 2;
		di[4] = (BYTE)GetRandomMasked(7);
	main_2228:
		cx = 8;
		do
		{
			main_227E_retval ax = main_227E(di);
			if (!ax.al)
				goto main_225A;
			di[1] &= ~2;
			al = di[4];
			if (di[1] & 1)
				al = (al - 1) & 7;
			else
				al = (al + 1) & 7;
			di[4] = al;
		}
		while (--cx);
		goto main_225D;
	main_225A:
		(WORD&)di[2] = cx;
	main_225D:
		maze[di[3] * MAZE_WIDTH + di[2]] = 0x502;
		di[5] = 3;
		dl = di[0];
	}
}

bool main_EB9()
{
	BYTE data_C89 = ((BYTE*)data_34C)[0] - 1;
	BYTE data_C8A = ((BYTE*)data_34C)[1] - 1;
	WORD data_B56 = data_34A[3] * MAZE_WIDTH;
	BYTE data_C8C = 0;
main_EE4:
	if (data_C8C > data_C89)
		return false;
	BYTE data_C8B = data_34A[2];
	BYTE data_C8D = 0;
main_EFC:
	if (data_C8D <= data_C8A)
	{
		if ((BYTE&)maze[data_B56 + data_C8B] == 0xB2)
			return true;
		data_C8B++;
		if (data_C8B >= MAZE_WIDTH)
			data_C8B = 0;
		if (++data_C8D)
			goto main_EFC;
	}
	data_B56 += MAZE_WIDTH;
	if (data_B56 >= _countof(maze))
		data_B56 -= _countof(maze);
	if (++data_C8C)
		goto main_EE4;
	return false;
}

void main_E2A()
{
	BYTE data_C84 = ((BYTE*)data_34C)[0] - 1;
	BYTE data_C85 = ((BYTE*)data_34C)[1] - 1;
	WORD data_B54 = data_34A[3] * MAZE_WIDTH;
	for (BYTE data_C87 = 0; data_C87 <= data_C84; data_C87++)
	{
		BYTE data_C86 = data_34A[2];
		for (BYTE data_C88 = 0; data_C88 <= data_C85; data_C88++)
		{
			maze[data_B54 + data_C86] = 0x920;
			if (++data_C86 >= MAZE_WIDTH)
				data_C86 = 0;
		}
		data_B54 += MAZE_WIDTH;
		if (data_B54 >= _countof(maze))
			data_B54 -= _countof(maze);
	}
}

void main_10C9()
{
	BYTE data_C8F = data_C70;
	while (data_C8F)
	{
		data_34A = &data_350[data_C8F * 8];
		if (++data_34A[5] >= 15)
			data_34A[5] = 0;
		data_34C = data_10A2[data_34A[5]];
		(WORD&)data_34A[6] = PointerToFakePointer(data_34C);
		if (main_EB9())
		{
			BYTE data_C90 = data_34A[0];
			main_1E8F(&data_C70, data_C8F);
			data_C8F = data_C90;
			data_B65--;
			continue;
		}
		main_E2A();
		PlotObjectToMaze();
		if (--data_34A[4])
			goto main_1251;
		BYTE *di = data_34A;
		if (frame >= 0xE00)
			data_34A[4] = 5;
		else
			data_34A[4] = (data_B69 >> (frame/0x100 + 1)) + 5;
		if (GetRandomMasked(0x1F >> (numGenerators + 1 - data_B65)))
			goto main_1251;
		data_34C = data_1112;
		BYTE data_C91 = data_34A[2] + 2;
		if (data_C91  > MAZE_WIDTH - 1)
			data_C91 -= MAZE_WIDTH - 1;
		BYTE data_C92 = data_34A[3];
		if (main_CED(data_C92, data_C91))
			goto main_1251;
		if (data_B64 + data_B68 >= maxSnipes)
		{
			BYTE data_C90 = main_CB0();
			if (!data_C90)
				goto main_1251;
			data_B64++;
			data_C8F = data_34A[0];
			data_34A = &data_350[data_C90 * 8];
			data_34A[0] = data_C6E;
			data_C6E = data_C90;
			data_34A[2] = data_C91;
			data_34A[3] = data_C92;
			data_34A[4] = 2;
			(WORD&)data_34A[6] = FAKE_POINTER_data_1112;
			PlotObjectToMaze();
			data_34A[1] = (BYTE)GetRandomMasked(1);
			data_34A[5] = 4;
			continue;
		}
	main_1251:
		data_C8F = data_34A[0];
	}
}

bool main_1A7B(BYTE arg)
{
	data_350[4] = arg << 1;
	main_227E_retval result = main_227E(data_34A);
	if (result.al)
		return data_CBF = true;
	data_1CA = data_350[2] = data_C77;
	data_1CC = data_350[3] = data_C78;
	return false;
}

bool main_198A()
{
	BYTE data_CBE = data_350[4];
	data_CBF = false;
	switch (data_CBE)
	{
	case 1:
		main_1A7B(1);
		if (!(data_11E8[data_34A[3]] & 1))
			goto main_1A5E;
		if (main_1A7B(1))
			goto main_1A5E;
		if (!main_1A7B(0))
			goto main_1A64;
		goto main_1A2B;
	case 3:
		if (main_1A7B(2))
			goto main_1A5A;
		if (data_11E8[data_34A[3]] & 1)
			main_1A7B(1);
		goto main_1A5A;
	case 4:
		main_1A7B(2);
		goto main_1A64;
	case 5:
		if (main_1A7B(2))
			goto main_1A2B;
		if (!(data_11E8[data_34A[3]] & 1))
			goto main_1A2B;
		main_1A7B(3);
		// fall through
	case 6:
	main_1A2B:
		main_1A7B(3);
		goto main_1A64;
	case 7:
		main_1A7B(3);
		if (!(data_11E8[data_34A[3]] & 1))
			goto main_1A5E;
		if (main_1A7B(3))
			goto main_1A5E;
		if (!main_1A7B(0))
			goto main_1A64;
		// fall through
	case 2:
	main_1A5A:
		main_1A7B(1);
		goto main_1A64;
	case 0:
	main_1A5E:
		main_1A7B(0);
	main_1A64:
		if (!data_CBF)
			PlotObjectToMaze();
		data_350[4] = data_CBE;
		return !data_CBF;
	default:
		__assume(0);
	}
}

bool main_1AB0() // returns true if the match has been lost
{
	data_34A = data_350;
	if (++data_C74 > 7)
	{
		data_C74 = 0;
		data_C75 ^= true;
	}
	if (data_C75)
		data_34C = data_10E2;
	else
		data_34C = data_10EC;
	if (data_C73)
	{
		keyboard_state = PollKeyboard();
		if (data_B66 >= numLives)
		{
			EraseBottomTwoLines();
			COORD pos;
			pos.X = 0;
			pos.Y = 25-2;
			SetConsoleCursorPosition(output, pos);
			SetConsoleTextAttribute(output, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED);
			SetConsoleMode(output, ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT);
			DWORD operationSize;
			WriteConsole(output, STRING_WITH_LEN("The SNIPES have triumphed!!!\r\n"), &operationSize, 0);
			return true;
		}
		if (!data_C72)
			goto main_1C03;
		return false;
	}
	else
	{
		if (--data_350[5] == 0)
		{
			keyboard_state = PollKeyboard();
			data_350[5] = 2;
			if (main_EB9())
				goto main_1BEE;
			goto main_1B3C;
		}
		if (main_EB9())
			goto main_1BEE;
		return false;
	}
main_1B3C:
	main_E2A();
	static const BYTE data_CAE[] = {0, 2, 6, 0, 4, 3, 5, 0, 0, 1, 7, 0, 0, 0, 0, 0};
	if (BYTE keyboardMove = keyboard_state & (KEYSTATE_MOVE_RIGHT | KEYSTATE_MOVE_LEFT | KEYSTATE_MOVE_DOWN | KEYSTATE_MOVE_UP))
	{
		data_350[4] = data_CAE[keyboardMove];
		if (!main_198A())
			goto main_1B85;
		if (!spacebar_state)
			goto main_1B8F;
		if (data_350[5] == 1)
		{
			main_E2A();
			if (!main_198A())
			{
				if (enableElectricWalls)
					goto main_1BEE;
				PlotObjectToMaze();
			}
		}
		data_350[5] = 1;
		goto main_1B8F;
	main_1B85:
		if (enableElectricWalls)
			goto main_1BEE;
	}
	PlotObjectToMaze();
main_1B8F:
	if (!spacebar_state && (keyboard_state & (KEYSTATE_FIRE_RIGHT | KEYSTATE_FIRE_LEFT | KEYSTATE_FIRE_DOWN | KEYSTATE_FIRE_UP)))
	{
		if (--data_350[1])
			return false;
		BYTE data_CC1 = data_350[4];
		data_350[4] = data_CAE[keyboard_state >> 4];
		main_1613(0);
		SetSoundEffectState(0, 0);
		data_350[4] = data_CC1;
		data_350[1] = data_350[5] == 1 ? data_2AA<<1 : data_2AA;
		return false;
	}
	data_350[1] = 1;
	return false;
main_1BEE:
	main_1E8F(data_350, 0);
	data_C73 = true;
	data_B66++;
	return false;
main_1C03:
	keyboard_state = PollKeyboard();
	data_C73 = false;
	main_F77();
	data_1CA = data_350[2];
	data_1CC = data_350[3];
	PlotObjectToMaze();
	return false;
}

void main_1D64()
{
	BYTE al = data_C6F;
main_1D6A:
	BYTE data_CCC = al;
	if (!al)
		return;
	BYTE *data_CC6 = data_34A = &data_350[al * 8];
	data_34C = FakePointerToPointer((WORD&)data_CC6[6]);
	main_E2A();
	BYTE data_CCE = (data_CC6[5] + 1) % 6;
	BYTE data_CCD = data_CC6[0];
	BYTE data_CCF = data_CCC == 0xFE ? 11 : 5;
	data_CC6[5]++;
	if (data_CC6[5] <= data_CCF)
		goto main_1DF4;
	main_1D04(&data_C6F, data_CCC);
	if (data_CCC != 0xFE)
		main_CCF(data_CCC);
	else
		data_C72 = 0;
	goto main_1E87;
main_1DF4:
	if (data_CC6[4] != 22)
		goto main_1E36;
	(WORD&)data_CC6[6] = PointerToFakePointer(data_12FE[data_CCE]);
	data_34C = data_12FE[data_CCE];
	if (data_CCC != 0xFE)
		goto main_1E32;
	if (data_CC6[5] <= 5)
		goto main_1E32;
	SetSoundEffectState(11 - data_CC6[5], 4);
	goto main_1E84;
main_1E32:
	al = 4;
	goto main_1E7C;
main_1E36:
	if (data_CC6[4] == 12)
	{
		(WORD&)data_CC6[6] = PointerToFakePointer(data_1352[data_CCE]);
		data_34C = data_1352[data_CCE];
		al = 3;
		goto main_1E7C;
	}
	if (data_CC6[4] != 11)
		goto main_1E84;
	(WORD&)data_CC6[6] = PointerToFakePointer(data_1392[data_CCE]);
	data_34C = data_1392[data_CCE];
	al = 2;
main_1E7C:
	SetSoundEffectState(data_CCE, al);
main_1E84:
	PlotObjectToMaze();
main_1E87:
	al = data_CCD;
	goto main_1D6A;
}

static const WORD sound1[] = {100, 100, 1400, 1800, 1600, 1200};
static const WORD sound2[] = {2200, 6600, 1800, 4400, 8400, 1100};
static const WORD sound3[] = {2000, 8000, 6500, 4000, 2500, 1000};

void UpdateSound()
{
	if (!sound_enabled || data_DF0 == 0xFF)
	{
		ClearSound();
		return;
	}
	switch (data_DF0)
	{
	case 0:
		if (!data_DF1)
			PlayTone(1900);
		else
			PlayTone(1400);
		break;
	case 1:
		PlayTone(1600);
		break;
	case 2:
		PlayTone(sound1[data_DF1]);
		break;
	case 3:
		PlayTone(sound2[data_DF1]);
		break;
	case 4:
		PlayTone(sound3[data_DF1]);
		break;
	}
	data_DF0 = 0xFF;
}

void DrawViewport()
{
	WORD data_29A = 2*(WINDOW_WIDTH * VIEWPORT_ROW);
	SHORT data_298 = data_1CC - VIEWPORT_HEIGHT / 2;
	if (data_298 < 0)
		data_298 += MAZE_HEIGHT;
	SHORT data_296 = data_1CA - WINDOW_WIDTH / 2;
	if (data_296 < 0)
		data_296 += MAZE_WIDTH;
	SHORT data_29E = 0;
	if (data_296 + WINDOW_WIDTH >= MAZE_WIDTH)
		data_29E = MAZE_WIDTH - data_296;
	else
		data_29E = WINDOW_WIDTH;
	for (Uint row = 0; row < VIEWPORT_HEIGHT; row++)
	{
		WORD data_29C = data_298 * MAZE_WIDTH;
		WriteTextMem(data_29E, data_29A, &maze[data_296 + data_29C]);
		if (data_29E != WINDOW_WIDTH)
			WriteTextMem(WINDOW_WIDTH - data_29E, data_29A + 2*data_29E, &maze[data_29C]);
		data_29A += WINDOW_WIDTH * 2;
		if (++data_298 == MAZE_HEIGHT)
			data_298 = 0;
	}
}

int main(int argc, char* argv[])
{
	input  = GetStdHandle(STD_INPUT_HANDLE);
	output = GetStdHandle(STD_OUTPUT_HANDLE);

	SetConsoleCP      (437);
	SetConsoleOutputCP(437);
	
	CONSOLE_CURSOR_INFO cursorInfo;
	GetConsoleCursorInfo(output, &cursorInfo);

	COORD windowSize;
	windowSize.X = WINDOW_WIDTH;
	windowSize.Y = WINDOW_HEIGHT;
	SMALL_RECT window;
	window.Left   = 0;
	window.Top    = 0;
	window.Right  = windowSize.X-1;
	window.Bottom = windowSize.Y-1;
	SetConsoleWindowInfo(output, TRUE, &window);
	SetConsoleScreenBufferSize(output, windowSize);

	//timeBeginPeriod(1);

	QueryPerformanceFrequency((LARGE_INTEGER*)&perf_freq);
	perf_freq *= 11 * 65535;

	{
		WAVEFORMATEX waveFormat;
		waveFormat.wFormatTag = WAVE_FORMAT_PCM;
		waveFormat.nChannels = 1;
		waveFormat.nSamplesPerSec = TONE_SAMPLE_RATE;
		waveFormat.nAvgBytesPerSec = TONE_SAMPLE_RATE * 2;
		waveFormat.nBlockAlign = 2;
		waveFormat.wBitsPerSample = 16;
		waveFormat.cbSize = 0;
		MMRESULT result = waveOutOpen(&waveOutput, WAVE_MAPPER, &waveFormat, (DWORD_PTR)WaveOutProc, NULL, CALLBACK_FUNCTION);
		if (result != MMSYSERR_NOERROR)
		{
			fprintf(stderr, "Error opening wave output\n");
			//timeEndPeriod(1);
			return -1;
		}
		for (Uint i=0; i<WAVE_BUFFER_COUNT; i++)
		{
			waveHeader[i].lpData = (LPSTR)&toneBuf[WAVE_BUFFER_LENGTH * i];
			waveHeader[i].dwBufferLength = sizeof(SHORT)*WAVE_BUFFER_LENGTH;
			waveHeader[i].dwBytesRecorded = 0;
			waveHeader[i].dwUser = i;
			waveHeader[i].dwFlags = 0;
			waveHeader[i].dwLoops = 0;
			waveHeader[i].lpNext = 0;
			waveHeader[i].reserved = 0;
			waveOutPrepareHeader(waveOutput, &waveHeader[i], sizeof(waveHeader[i]));
		}
	}

	DWORD operationSize; // dummy in most circumstances
	COORD pos;

	pos.X = 0;
	pos.Y = 0;
	FillConsoleOutputAttribute(output, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED, windowSize.X*windowSize.Y, pos, &operationSize);
	SetConsoleTextAttribute(output, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED);
	WriteConsole(output, STRING_WITH_LEN("Enter skill level (A-Z)(1-9): "), &operationSize, 0);
	ReadSkillLevel();

	WORD tick_count = GetTickCountWord();
	random_seed_lo = (BYTE)tick_count;
	if (!random_seed_lo)
		random_seed_lo = 444;
	random_seed_hi = tick_count >> 8;
	if (!random_seed_hi)
		random_seed_hi = 555;

	for (;;)
	{
		enableElectricWalls = skillLevelLetter >= 'M'-'A';
		skillThing1           = skillThing1Table  [skillLevelLetter];
		skillThing2           = skillThing2Table  [skillLevelLetter];
		skillThing3           = skillThing3Table  [skillLevelLetter];
		maxSnipes             = maxSnipesTable    [skillLevelNumber-1];
		numGenerators         = numGeneratorsTable[skillLevelNumber-1];
		numLives              = numLivesTable     [skillLevelNumber-1];
		skillThing7           = skillLevelLetter < 'W'-'A';
		data_2AA              = 2;
		enableRubberBullets   = rubberBulletTable [skillLevelLetter];

		SetConsoleMode(output, 0);
		cursorInfo.bVisible = FALSE;
		SetConsoleCursorInfo(output, &cursorInfo);

		frame = 0;
		outputHUD();
		CreateMaze();
		CreateGenerators();
		SetSoundEffectState(0, 0xFF);

		/*FILE *f = fopen("mazey.bin", "wb");
		for (Uint i=0; i<sizeof(maze)/2; i++)
			fwrite(&maze[i], 1, 1, f);
		fclose(f);*/

		/*StartTone(2711);
		for (;;)
			Sleep(1000);*/

		for (;;)
		{
			if (forfeit_match)
			{
				EraseBottomTwoLines();
				break;
			}
			if (updateHUD())
				break;

			for (;;)
			{
				Sleep(1);
				WORD tick_count2 = GetTickCountWord();
				if (tick_count2 != tick_count)
				{
					tick_count = tick_count2;
					break;
				}
			}

			main_125C();
			main_2124();
			main_1EC1();
			main_10C9();

			if (main_1AB0())
				break;

			main_1D64();
			UpdateSound();
			DrawViewport();
		}

		ClearSound();
		forfeit_match = false;

		cursorInfo.bVisible = TRUE;
		SetConsoleCursorInfo(output, &cursorInfo);
		COORD pos;
		pos.X = 0;
		pos.Y = 25-1;
		SetConsoleCursorPosition(output, pos);
		SetConsoleTextAttribute(output, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED);
		SetConsoleMode(output, ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT);
		for (;;)
		{
			SetConsoleMode(input, ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
			WriteConsole(output, STRING_WITH_LEN("Play another game? (Y or N) "), &operationSize, 0);
			char playAgain;
			ReadConsole_wrapper(&playAgain, 1);
			if (playAgain == 'Y' || playAgain == 'y')
				goto do_play_again;
			if (playAgain == 'N' || playAgain == 'n')
				goto do_not_play_again;
		}
	do_play_again:
		WriteConsole(output, STRING_WITH_LEN("Enter new skill level (A-Z)(1-9): "), &operationSize, 0);
		ReadSkillLevel();
	}
do_not_play_again:

	for (Uint i=0; i<WAVE_BUFFER_COUNT; i++)
		waveOutUnprepareHeader(waveOutput, &waveHeader[i], sizeof(waveHeader[i]));
	waveOutClose(waveOutput);

	//timeEndPeriod(1);

	return 0;
}
