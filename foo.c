#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ttsapi.h"
#include "portaudio.h"

#define NUM_BUFFERS 4  // Number of buffers for speech data
#define BUFFER_SIZE 1024 * 32 // Size of each buffer in bytes

// Callback function for PortAudio
static int audioCallback(const void *inputBuffer, void *outputBuffer,
                        unsigned long framesPerBuffer,
                        const PaStreamCallbackTimeInfo *timeInfo,
                        PaStreamCallbackFlags statusFlags,
                        void *userData) {
    LPTTS_BUFFER_T buffer = (LPTTS_BUFFER_T)userData;

    // Check if there's data available in the buffer
    if (buffer->dwBufferLength > 0) {
        // Copy data from the DECtalk buffer to the PortAudio output buffer
        memcpy(outputBuffer, buffer->lpData, buffer->dwBufferLength);

        // Update the buffer length to indicate data has been consumed
        buffer->dwBufferLength = 0;

        return paContinue; // Continue playback
    } else {
        return paComplete; // No more data, signal completion
    }
}

int main() {
    PaError err;
    PaStream *stream;
    LPTTS_HANDLE_T phTTS; // Use the correct type name
    LPTTS_BUFFER_T buffers[NUM_BUFFERS];
    int currentBuffer = 0;
    const char *text = "This is a long line of text that will be synthesized into a memory buffer and then played back using PortAudio.";

    // Initialize DECtalk
    if (TextToSpeechStartupEx(&phTTS, WAVE_MAPPER, 0, NULL, 0) != MMSYSERR_NOERROR) {
        fprintf(stderr, "Error initializing DECtalk.\n");
        return 1;
    }

    // Set up speech-to-memory mode
    if (TextToSpeechOpenInMemory(phTTS, WAVE_FORMAT_1M16) != MMSYSERR_NOERROR) {
        fprintf(stderr, "Error opening DECtalk in memory mode.\n");
        TextToSpeechShutdown(phTTS);
        return 1;
    }

    // Allocate and add memory buffers
    for (int i = 0; i < NUM_BUFFERS; i++) {
        buffers[i] = (LPTTS_BUFFER_T)malloc(sizeof(TTS_BUFFER_T));
        if (buffers[i] == NULL) {
            fprintf(stderr, "Error allocating memory for buffer.\n");
            TextToSpeechShutdown(phTTS);
            return 1;
        }
        buffers[i]->lpData = (LPSTR)malloc(BUFFER_SIZE);
        if (buffers[i]->lpData == NULL) {
            fprintf(stderr, "Error allocating memory for buffer data.\n");
            TextToSpeechShutdown(phTTS);
            return 1;
        }
        buffers[i]->dwMaximumBufferLength = BUFFER_SIZE;
        buffers[i]->lpPhonemeArray = NULL; // Not using phoneme data
        buffers[i]->lpIndexArray = NULL;    // Not using index marks
        if (TextToSpeechAddBuffer(phTTS, buffers[i]) != MMSYSERR_NOERROR) {
            fprintf(stderr, "Error adding buffer to DECtalk.\n");
            TextToSpeechShutdown(phTTS);
            return 1;
        }
    }

    // Create a non-const copy of the text
    char *textCopy = strdup(text); 
    if (textCopy == NULL) {
        fprintf(stderr, "Error duplicating text string.\n");
        TextToSpeechShutdown(phTTS);
        return 1;
    }

    // Synthesize the text using the text copy
    if (TextToSpeechSpeak(phTTS, textCopy, TTS_NORMAL) != MMSYSERR_NOERROR) {
        fprintf(stderr, "Error synthesizing speech.\n");
        free(textCopy); // Free the text copy
        TextToSpeechShutdown(phTTS);
        return 1;
    }

    // Initialize PortAudio
    err = Pa_Initialize();
    if (err != paNoError) {
        fprintf(stderr, "Error initializing PortAudio: %s\n", Pa_GetErrorText(err));
        free(textCopy); // Free the text copy
        TextToSpeechShutdown(phTTS);
        return 1;
    }

    // Open PortAudio stream
    err = Pa_OpenDefaultStream(&stream,
                               0, // No input channels
                               1, // Mono output
                               paInt16, // 16-bit integer format
                               11025,   // Sample rate (match DECtalk's output)
                               paFramesPerBufferUnspecified,
                               audioCallback,
                               &buffers[currentBuffer]); 
    if (err != paNoError) {
        fprintf(stderr, "Error opening PortAudio stream: %s\n", Pa_GetErrorText(err));
        Pa_Terminate();
        free(textCopy); // Free the text copy
        TextToSpeechShutdown(phTTS);
        return 1;
    }

    // Start PortAudio stream
    err = Pa_StartStream(stream);
    if (err != paNoError) {
        fprintf(stderr, "Error starting PortAudio stream: %s\n", Pa_GetErrorText(err));
        Pa_Terminate();
        free(textCopy); // Free the text copy
        TextToSpeechShutdown(phTTS);
        return 1;
    }

    // Wait for playback to finish (buffers to be consumed)
    while (Pa_IsStreamActive(stream)) {
        // You might want to add a small delay here to avoid busy-waiting
        Pa_Sleep(10); 

        // Check if the current buffer is empty and there's another buffer available
        if (buffers[currentBuffer]->dwBufferLength == 0 && 
            buffers[(currentBuffer + 1) % NUM_BUFFERS]->dwBufferLength > 0) {
            currentBuffer = (currentBuffer + 1) % NUM_BUFFERS; // Switch to the next buffer
        }
    }

    // Clean up
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    free(textCopy); // Free the text copy
    TextToSpeechShutdown(phTTS);

    // Free allocated memory for buffers
    for (int i = 0; i < NUM_BUFFERS; i++) {
        free(buffers[i]->lpData);
        free(buffers[i]);
    }

    return 0;
}
