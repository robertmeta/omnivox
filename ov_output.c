#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include "ov_output.h"

#define SAMPLE_RATE  (44100)
#define FRAMES_PER_BUFFER (512)
#define RING_BUFFER_SIZE (SAMPLE_RATE * 10)  // 10 seconds of audio

static PaStream *stream = NULL;
static float ringBuffer[RING_BUFFER_SIZE];
static atomic_size_t writeIndex = 0;
static atomic_size_t readIndex = 0;
static bool isRunning = false;
static pthread_t audioThread;

static size_t ringBufferWrite(const float *data, size_t count) {
    size_t i;
    for (i = 0; i < count; ++i) {
        size_t next = (writeIndex + 1) % RING_BUFFER_SIZE;
        if (next == readIndex) break;  // Buffer full
        ringBuffer[writeIndex] = data[i];
        writeIndex = next;
    }
    return i;
}

static int paCallback(const void *inputBuffer, void *outputBuffer,
                      unsigned long framesPerBuffer,
                      const PaStreamCallbackTimeInfo* timeInfo,
                      PaStreamCallbackFlags statusFlags,
                      void *userData) {
    float *out = (float*)outputBuffer;
    (void) inputBuffer; (void) timeInfo; (void) statusFlags; (void) userData;

    for (unsigned long i = 0; i < framesPerBuffer * 2; i++) {
        if (readIndex != writeIndex) {
            *out++ = ringBuffer[readIndex];
            readIndex = (readIndex + 1) % RING_BUFFER_SIZE;
        } else {
            *out++ = 0;  // Output silence if buffer is empty
        }
    }

    return paContinue;
}

static void* audioThreadFunc(void *arg) {
    (void)arg;
    PaError err;

    err = Pa_Initialize();
    if (err != paNoError) goto error;

    err = Pa_OpenDefaultStream(&stream, 0, 2, paFloat32, SAMPLE_RATE,
                               FRAMES_PER_BUFFER, paCallback, NULL);
    if (err != paNoError) goto error;

    err = Pa_StartStream(stream);
    if (err != paNoError) goto error;

    while (isRunning) {
        Pa_Sleep(100);  // Sleep to reduce CPU usage
    }

    err = Pa_StopStream(stream);
    if (err != paNoError) goto error;

    err = Pa_CloseStream(stream);
    if (err != paNoError) goto error;

    Pa_Terminate();
    return NULL;

error:
    fprintf(stderr, "PortAudio error: %s\n", Pa_GetErrorText(err));
    return NULL;
}

int ov_output_init(void) {
    isRunning = true;
    if (pthread_create(&audioThread, NULL, audioThreadFunc, NULL) != 0) {
        fprintf(stderr, "Failed to create audio thread\n");
        return 1;
    }
    return 0;
}

int ov_output_play_buffer(const float *buffer, unsigned long frames) {
    size_t written = ringBufferWrite(buffer, frames * 2);  // * 2 for stereo
    if (written < frames * 2) {
        fprintf(stderr, "Warning: Buffer overflow, some audio data was discarded\n");
    }
    return 0;
}

void ov_output_cleanup(void) {
    isRunning = false;
    pthread_join(audioThread, NULL);
}
