#include <Windows.h>
#include <MMSystem.h>
#include <stdio.h>
#include <math.h>
#include "config.h"
#include "sound.h"
#pragma comment(lib,"winmm.lib")

#define TONE_SAMPLE_RATE 48000
#define WAVE_BUFFER_LENGTH 200
#define WAVE_BUFFER_COUNT 11
HWAVEOUT waveOutput;
WAVEHDR waveHeader[WAVE_BUFFER_COUNT];
double toneFreq;
Uint currentFreqnum = -1;
Uint tonePhase;
SHORT toneBuf[WAVE_BUFFER_LENGTH * WAVE_BUFFER_COUNT];
void CALLBACK WaveOutProc(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
	if (uMsg != WOM_DONE)
		return;
	if (currentFreqnum == -1)
		return;
	WAVEHDR *currentWaveHeader = (WAVEHDR*)dwParam1;
	if (currentFreqnum == 0)
		for (Uint i=0; i<WAVE_BUFFER_LENGTH; i++)
			((SHORT*)currentWaveHeader->lpData)[i] = 0;
	else
		for (Uint i=0; i<WAVE_BUFFER_LENGTH; i++)
		{
			((SHORT*)currentWaveHeader->lpData)[i] = fmod(tonePhase * toneFreq, 1.) < 0.5 ? 0 : 0x2000;
			tonePhase++;
		}
	waveOutWrite(hwo, currentWaveHeader, sizeof(SHORT)*WAVE_BUFFER_LENGTH);
}
void PlayTone(Uint freqnum)
{
	BOOL soundAlreadyPlaying = currentFreqnum != -1;
	double prevPhase = fmod(tonePhase * toneFreq, 1.);
	toneFreq = (13125000. / (TONE_SAMPLE_RATE * 11)) / freqnum;
	if (soundAlreadyPlaying)
	{
		tonePhase = (Uint)(prevPhase / toneFreq);
		currentFreqnum = freqnum;
		return;
	}
	tonePhase = 0;
	currentFreqnum = -1;
	waveOutReset(waveOutput);
	currentFreqnum = freqnum;
	for (Uint i=0; i<WAVE_BUFFER_COUNT; i++)
		WaveOutProc(waveOutput, WOM_DONE, 0, (DWORD_PTR)&waveHeader[i], 0);
}
void ClearSound()
{
	currentFreqnum = -1;
}

int OpenSound()
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
	return 0;
}

void CloseSound()
{
	for (Uint i=0; i<WAVE_BUFFER_COUNT; i++)
		waveOutUnprepareHeader(waveOutput, &waveHeader[i], sizeof(waveHeader[i]));
	waveOutClose(waveOutput);
}
