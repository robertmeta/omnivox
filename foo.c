#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ttsapi.h>
#include <portaudio.h>
#include <uv.h>

#define SAMPLE_RATE 11025
#define FRAMES_PER_BUFFER 256
#define NUM_CHANNELS 1
#define BUFFER_SIZE 32768  // Adjust this size as needed

typedef struct {
    unsigned char *audioData;
    DWORD dataSize;
    DWORD currentIndex;
    int isPlaying;
} AudioContext;

static int paCallback(const void *inputBuffer, void *outputBuffer,
                      unsigned long framesPerBuffer,
                      const PaStreamCallbackTimeInfo* timeInfo,
                      PaStreamCallbackFlags statusFlags,
                      void *userData)
{
    AudioContext *context = (AudioContext*)userData;
    float *out = (float*)outputBuffer;
    unsigned int i;

    (void) inputBuffer; // Prevent unused variable warning

    for (i = 0; i < framesPerBuffer; i++) {
        if (context->currentIndex >= context->dataSize) {
            *out++ = 0.0f;
            if (i == 0) {
                context->isPlaying = 0;
                printf("Audio playback completed\n");
                return paComplete;
            }
        } else {
            *out++ = ((float)context->audioData[context->currentIndex++] - 128.0f) / 128.0f;
        }
    }

    printf("Processed %lu frames, current index: %lu\n", framesPerBuffer, context->currentIndex);
    return paContinue;
}

void on_timer(uv_timer_t* handle) {
    PaStream *stream = (PaStream*)handle->data;
    PaError err = Pa_StartStream(stream);
    if (err != paNoError) {
        printf("Failed to start PortAudio stream: %s\n", Pa_GetErrorText(err));
    } else {
        printf("Started PortAudio stream\n");
    }
}

void check_audio_status(uv_timer_t* handle) {
    AudioContext *context = (AudioContext*)handle->data;
    if (!context->isPlaying) {
        printf("Audio playback seems to have stopped\n");
        uv_timer_stop(handle);
        uv_stop(uv_default_loop());
    }
}

int main() {
    LPTTS_HANDLE_T phTTS;
    MMRESULT result;
    AudioContext context = {0};
    PaStream *stream;
    PaError err;
    uv_loop_t *loop;
    uv_timer_t start_timer, check_timer;
    TTS_BUFFER_T ttsBuffer;

    // Initialize DECtalk
    result = TextToSpeechStartup(&phTTS, WAVE_MAPPER, 0, NULL, 0);
    if (result != MMSYSERR_NOERROR) {
        printf("Failed to initialize DECtalk: %d\n", result);
        return 1;
    }
    printf("DECtalk initialized successfully\n");

    // Set up in-memory mode
    result = TextToSpeechOpenInMemory(phTTS, WAVE_FORMAT_1M08);
    if (result != MMSYSERR_NOERROR) {
        printf("Failed to set up in-memory mode: %d\n", result);
        TextToSpeechShutdown(phTTS);
        return 1;
    }
    printf("In-memory mode set up successfully\n");

    // Prepare the buffer
    ttsBuffer.lpData = malloc(BUFFER_SIZE);
    ttsBuffer.dwMaximumBufferLength = BUFFER_SIZE;
    ttsBuffer.dwBufferLength = 0;

    // Add the buffer to DECtalk
    result = TextToSpeechAddBuffer(phTTS, &ttsBuffer);
    if (result != MMSYSERR_NOERROR) {
        printf("Failed to add buffer: %d\n", result);
        free(ttsBuffer.lpData);
        TextToSpeechShutdown(phTTS);
        return 1;
    }
    printf("Buffer added successfully\n");

    // Speak text
    result = TextToSpeechSpeak(phTTS, "Hello, world! This is a test of the DECtalk text-to-speech system.", TTS_FORCE);
    if (result != MMSYSERR_NOERROR) {
        printf("Failed to speak text: %d\n", result);
        free(ttsBuffer.lpData);
        TextToSpeechShutdown(phTTS);
        return 1;
    }
    printf("Text spoken successfully\n");

    // Wait for the buffer to be filled
    int timeout = 100; // 1 second timeout
    while (ttsBuffer.dwBufferLength == 0 && timeout > 0) {
        Pa_Sleep(10);  // Sleep for 10ms
        timeout--;
    }

    if (ttsBuffer.dwBufferLength == 0) {
        printf("Timeout waiting for buffer to be filled\n");
        free(ttsBuffer.lpData);
        TextToSpeechShutdown(phTTS);
        return 1;
    }

    printf("Buffer filled with %lu bytes of audio data\n", ttsBuffer.dwBufferLength);

    context.audioData = (unsigned char *)ttsBuffer.lpData;
    context.dataSize = ttsBuffer.dwBufferLength;
    context.currentIndex = 0;
    context.isPlaying = 1;

    // Initialize PortAudio
    err = Pa_Initialize();
    if (err != paNoError) {
        printf("Failed to initialize PortAudio: %s\n", Pa_GetErrorText(err));
        free(ttsBuffer.lpData);
        TextToSpeechShutdown(phTTS);
        return 1;
    }
    printf("PortAudio initialized successfully\n");

    // Open PortAudio stream
    err = Pa_OpenDefaultStream(&stream, 0, NUM_CHANNELS, paFloat32, SAMPLE_RATE,
                               FRAMES_PER_BUFFER, paCallback, &context);
    if (err != paNoError) {
        printf("Failed to open PortAudio stream: %s\n", Pa_GetErrorText(err));
        Pa_Terminate();
        free(ttsBuffer.lpData);
        TextToSpeechShutdown(phTTS);
        return 1;
    }
    printf("PortAudio stream opened successfully\n");

    // Initialize libuv
    loop = uv_default_loop();
    uv_timer_init(loop, &start_timer);
    uv_timer_init(loop, &check_timer);
    start_timer.data = stream;
    check_timer.data = &context;

    // Schedule audio playback after 2 seconds
    uv_timer_start(&start_timer, on_timer, 2000, 0);
    printf("Audio playback scheduled\n");

    // Check audio status every 100ms
    uv_timer_start(&check_timer, check_audio_status, 2100, 100);

    // Run the event loop
    printf("Starting event loop\n");
    uv_run(loop, UV_RUN_DEFAULT);
    printf("Event loop finished\n");

    // Clean up
    Pa_CloseStream(stream);
    Pa_Terminate();
    free(ttsBuffer.lpData);
    TextToSpeechShutdown(phTTS);

    return 0;
}
