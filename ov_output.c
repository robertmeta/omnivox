#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ov_output.h"

#define SAMPLE_RATE  (44100)
#define FRAMES_PER_BUFFER (512)

static PaStream *stream = NULL;

static int paCallback(const void *inputBuffer, void *outputBuffer,
                      unsigned long framesPerBuffer,
                      const PaStreamCallbackTimeInfo* timeInfo,
                      PaStreamCallbackFlags statusFlags,
                      void *userData)
{
    paTestData *data = (paTestData*)userData;
    float *out = (float*)outputBuffer;
    unsigned long i;
    (void) inputBuffer; /* Prevent unused variable warning. */

    for (i=0; i<framesPerBuffer; i++)
    {
        if (data->frameIndex < data->maxFrameIndex)
        {
            *out++ = data->buffer[data->frameIndex++];  /* left */
            *out++ = data->buffer[data->frameIndex++];  /* right */
        }
        else
        {
            *out++ = 0;  /* left */
            *out++ = 0;  /* right */
        }
    }

    return paContinue;
}

int ov_output_init(void)
{
    PaError err;

    err = Pa_Initialize();
    if (err != paNoError) {
        fprintf(stderr, "PortAudio error: %s\n", Pa_GetErrorText(err));
        return 1;
    }

    return 0;
}

int ov_output_play_buffer(const float *buffer, unsigned long frames)
{
    PaError err;
    paTestData data;

    data.buffer = buffer;
    data.frameIndex = 0;
    data.maxFrameIndex = frames * 2; // Stereo, so 2 samples per frame

    err = Pa_OpenDefaultStream(&stream,
                               0,          /* no input channels */
                               2,          /* stereo output */
                               paFloat32,  /* 32 bit floating point output */
                               SAMPLE_RATE,
                               FRAMES_PER_BUFFER,
                               paCallback,
                               &data);
    if (err != paNoError) {
        fprintf(stderr, "PortAudio error: %s\n", Pa_GetErrorText(err));
        return 1;
    }

    err = Pa_StartStream(stream);
    if (err != paNoError) {
        fprintf(stderr, "PortAudio error: %s\n", Pa_GetErrorText(err));
        return 1;
    }

    // Wait for stream to finish
    while (Pa_IsStreamActive(stream) == 1) {
        Pa_Sleep(100);
    }

    err = Pa_CloseStream(stream);
    if (err != paNoError) {
        fprintf(stderr, "PortAudio error: %s\n", Pa_GetErrorText(err));
        return 1;
    }

    return 0;
}

void ov_output_cleanup(void)
{
    PaError err = Pa_Terminate();
    if (err != paNoError) {
        fprintf(stderr, "PortAudio error: %s\n", Pa_GetErrorText(err));
    }
}
