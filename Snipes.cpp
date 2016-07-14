#include <Windows.h>
#include <MMSystem.h>
#include <stdio.h>
#include <math.h>
#pragma comment(lib,"winmm.lib")

typedef unsigned char Uchar;
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

#define MAZE_WIDTH  (7*17)
#define MAZE_HEIGHT (6*20)

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
			if (record.Event.KeyEvent.wVirtualKeyCode == VK_F2)
				shooting_sound_enabled ^= true;
		}
	}
	Uint state = 0;
	forfeit_match = GetKeyState(VK_ESCAPE) < 0;
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

WORD random_seed_lo, random_seed_hi;

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

static Uchar skillThing1Table  ['Z'-'A'+1] = {2, 3, 4, 3, 4, 4, 3, 4, 3, 4, 4, 5, 3, 4, 3, 4, 3, 4, 3, 4, 4, 5, 4, 4, 5, 5};
static bool  skillThing2Table  ['Z'-'A'+1] = {0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1};
static bool  rubberBulletTable ['Z'-'A'+1] = {0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
static Uchar skillThing3Table  ['Z'-'A'+1] = {0x7F, 0x7F, 0x7F, 0x3F, 0x3F, 0x1F, 0x7F, 0x7F, 0x3F, 0x3F, 0x1F, 0x1F, 0x7F, 0x7F, 0x3F, 0x3F, 0x7F, 0x7F, 0x3F, 0x3F, 0x1F, 0x1F, 0x3F, 0x1F, 0x1F, 0x0F};
static Uchar maxSnipesTable    ['9'-'1'+1] = { 10,  20,  30,  40,  60,  80, 100, 120, 150};
static Uchar numGeneratorsTable['9'-'1'+1] = {  3,   3,   4,   4,   5,   5,   6,   8,  10};
static Uchar numLivesTable     ['9'-'1'+1] = {  5,   5,   5,   5,   5,   4,   4,   3,   2};

bool enableElectricWalls, skillThing2, skillThing7, enableRubberBullets;
Uchar skillThing1, skillThing3, maxSnipes, numGenerators, numLives;

Uchar data_1D0;

int main(int argc, char* argv[])
{
	input  = GetStdHandle(STD_INPUT_HANDLE);
	output = GetStdHandle(STD_OUTPUT_HANDLE);

	SetConsoleCP      (437);
	SetConsoleOutputCP(437);
	
	COORD windowSize;
	windowSize.X = 40;
	windowSize.Y = 25;
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

	char skillLevel[4] = {0};
	DWORD skillLevelLength = 0;
	ReadConsole(input, skillLevel, _countof(skillLevel), &skillLevelLength, NULL);

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
	//printf("Skill: %c%c\n", skillLevelLetter + 'A', skillLevelNumber + '0');

	enableElectricWalls = skillLevelLetter >= 'M'-'A';
	skillThing1           = skillThing1Table  [skillLevelLetter];
	skillThing2           = skillThing2Table  [skillLevelLetter];
	skillThing3           = skillThing3Table  [skillLevelLetter];
	maxSnipes             = maxSnipesTable    [skillLevelNumber];
	numGenerators         = numGeneratorsTable[skillLevelNumber];
	numLives              = numLivesTable     [skillLevelNumber];
	skillThing7           = skillLevelLetter < 'W'-'A';
	enableRubberBullets   = rubberBulletTable [skillLevelLetter];

	data_1D0 = 0;

	/*StartTone(2711);
	for (;;)
		Sleep(1000);*/

	/*char string[] = "Hello, \x01 world!\n";
	COORD pos;
	pos.X = 10;
	pos.Y = 10;
	SetConsoleCursorPosition(output, pos);
	SetConsoleTextAttribute(output, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED | BACKGROUND_BLUE);
	WriteConsole(output, string, strlength(string), &operationSize, 0);*/

	/*COORD moveto;
	moveto.X = 2;
	moveto.Y = 2;
	CHAR_INFO backgroundFill;
	backgroundFill.Char.AsciiChar = ' ';
	backgroundFill.Attributes = FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED | BACKGROUND_BLUE;
	ScrollConsoleScreenBuffer(output, &window, NULL, moveto, &backgroundFill);*/

	WORD tick_count = GetTickCountWord();
	random_seed_lo = (BYTE)tick_count;
	if (!random_seed_lo)
		random_seed_lo = 444;
	random_seed_hi = tick_count >> 8;
	if (!random_seed_hi)
		random_seed_hi = 555;

	//Uint time = 0;
	for (;;)
	{
		Uint result = PollKeyboard();
		Sleep(1);
	}

	for (Uint i=0; i<WAVE_BUFFER_COUNT; i++)
		waveOutUnprepareHeader(waveOutput, &waveHeader[i], sizeof(waveHeader[i]));
	waveOutClose(waveOutput);

	//timeEndPeriod(1);

	return 0;
}
