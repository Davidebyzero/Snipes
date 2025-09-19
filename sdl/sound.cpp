#include "../config.h"
#include "../sound.h"
#include "../platform.h"

#include <math.h>
#include <stdio.h>
#ifdef __APPLE__
#define SDL_MAIN_HANDLED
#endif
#include <SDL_audio.h>

#define TONE_SAMPLE_RATE 48000
#define WAVE_BUFFER_LENGTH 512

double toneFreq;
int currentFreqnum = 0;
Uint tonePhase;

#ifdef STOP_WAVE_OUT_DURING_SILENCE
#error STOP_WAVE_OUT_DURING_SILENCE not supported in SDL build
#endif

void SDLCALL SoundCallback(void *, Uint8 *_stream, int _length)
{
    Sint16 *stream = (Sint16*) _stream;
    Uint length = _length / 2;

	if (currentFreqnum == 0)
		for (Uint i=0; i<length; i++)
			stream[i] = 0;
	else
		for (Uint i=0; i<length; i++)
		{
			stream[i] = fmod(tonePhase * toneFreq, 1.) < 0.5 ? 0 : 0x2000;
			tonePhase++;
		}
}

void PlayTone(Uint freqnum)
{
	bool soundAlreadyPlaying = currentFreqnum != -1;
	double prevPhase = fmod(tonePhase * toneFreq, 1.);
	toneFreq = (13125000. / (TONE_SAMPLE_RATE * 11)) / freqnum;
	if (soundAlreadyPlaying)
	{
		tonePhase = (Uint)(prevPhase / toneFreq);
		currentFreqnum = freqnum;
		return;
	}
	tonePhase = 0;
	currentFreqnum = freqnum;
}
void ClearSound()
{
	currentFreqnum = 0;
}

int OpenSound()
{
    SDL_AudioSpec desiredSpec;

    desiredSpec.freq = TONE_SAMPLE_RATE;
    desiredSpec.format = AUDIO_S16SYS;
    desiredSpec.channels = 1;
    desiredSpec.samples = WAVE_BUFFER_LENGTH;
    desiredSpec.callback = SoundCallback;

    SDL_AudioSpec obtainedSpec;

    if (SDL_OpenAudio(&desiredSpec, &obtainedSpec) != 0)
    {
		fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
		return 1;
    }

    // start playing audio
    SDL_PauseAudio(0);

    return 0;
}

void CloseSound()
{
    SDL_CloseAudio();
}
