#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "ov_output.h"
#include <portaudio.h>

#define SAMPLE_RATE  (44100)
#define FRAMES_PER_BUFFER (512)
#define MAX_BUFFER_SIZE (SAMPLE_RATE * 5)  // 5 seconds of audio

static PaStream *stream = NULL;
static float audioBuffer[MAX_BUFFER_SIZE];
static unsigned int writeIndex = 0;
static unsigned int readIndex = 0;
static pthread_mutex_t bufferMutex = PTHREAD_MUTEX_INITIALIZER;
static volatile int isRunning = 0;

static int paCallback(const void *inputBuffer, void *outputBuffer,
                      unsigned long framesPerBuffer,
                      const PaStreamCallbackTimeInfo* timeInfo,
                      PaStreamCallbackFlags statusFlags,
                      void *userData)
{
    float *out = (float*)outputBuffer;
    (void) inputBuffer; /* Prevent unused variable warnings. */
    (void) timeInfo;
    (void) statusFlags;
    (void) userData;

    pthread_mutex_lock(&bufferMutex);
    for (unsigned long i = 0; i < framesPerBuffer * 2; i++) {
        if (readIndex != writeIndex) {
            *out++ = audioBuffer[readIndex];
            readIndex = (readIndex + 1) % MAX_BUFFER_SIZE;
        } else {
            *out++ = 0;  // Output silence if buffer is empty
        }
    }
    pthread_mutex_unlock(&bufferMutex);

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

    err = Pa_OpenDefaultStream(&stream,
                               0,          /* no input channels */
                               2,          /* stereo output */
                               paFloat32,  /* 32 bit floating point output */
                               SAMPLE_RATE,
                               FRAMES_PER_BUFFER,
                               paCallback,
                               NULL);
    if (err != paNoError) {
        fprintf(stderr, "PortAudio error: %s\n", Pa_GetErrorText(err));
        return 1;
    }

    err = Pa_StartStream(stream);
    if (err != paNoError) {
        fprintf(stderr, "PortAudio error: %s\n", Pa_GetErrorText(err));
        return 1;
    }

    isRunning = 1;

    return 0;
}

int ov_output_play_buffer(const float *buffer, unsigned long frames)
{
    printf("Debug: ov_output_play_buffer called with %lu frames\n", frames);

    pthread_mutex_lock(&bufferMutex);
    for (unsigned long i = 0; i < frames * 2; i++) {  // * 2 for stereo
        audioBuffer[writeIndex] = buffer[i];
        writeIndex = (writeIndex + 1) % MAX_BUFFER_SIZE;
        if (writeIndex == readIndex) {
            // Buffer overflow, move read index
            readIndex = (readIndex + 1) % MAX_BUFFER_SIZE;
        }
    }
    pthread_mutex_unlock(&bufferMutex);

    printf("Debug: Audio data added to circular buffer\n");

    return 0;
}

void ov_output_cleanup(void)
{
    isRunning = 0;

    if (stream) {
        Pa_StopStream(stream);
        Pa_CloseStream(stream);
        stream = NULL;
    }
    Pa_Terminate();
}
