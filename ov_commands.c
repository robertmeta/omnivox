#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "ov_commands.h"
#include "ov_audio.h"
#include "ov_output.h"
#include "ttsapi.h"

#define SAMPLE_RATE 44100
#define MAX_TTS_BUFFER_SIZE 1048576  // 1MB buffer for TTS output

static LPTTS_HANDLE_T ttsHandle = NULL;
static TTS_BUFFER_T ttsBuffer;

// Callback function for TTS
static void CALLBACK ttsCallback(LONG lParam1, LONG lParam2, DWORD dwInstance, UINT uiMsg)
{
    printf("Debug: TTS Callback - lParam1: %ld, lParam2: %ld, uiMsg: %u\n", lParam1, lParam2, uiMsg);
}

int ov_commands_init(void)
{
    printf("Debug: Initializing TTS engine\n");
    MMRESULT result = TextToSpeechStartupEx(&ttsHandle, 0, 0, ttsCallback, 0);
    if (result != MMSYSERR_NOERROR) {
        fprintf(stderr, "Failed to initialize TTS engine. Error code: %d\n", result);
        return 1;
    }
    if (ttsHandle == NULL) {
        fprintf(stderr, "TTS handle is NULL after initialization\n");
        return 1;
    }
    printf("Debug: TTS engine initialized successfully\n");

    // Initialize TTS buffer
    memset(&ttsBuffer, 0, sizeof(TTS_BUFFER_T));
    ttsBuffer.lpData = malloc(MAX_TTS_BUFFER_SIZE);
    ttsBuffer.dwMaximumBufferLength = MAX_TTS_BUFFER_SIZE;

    // Open TTS output to memory
    result = TextToSpeechOpenInMemory(ttsHandle, WAVE_FORMAT_1M16);
    if (result != MMSYSERR_NOERROR) {
        fprintf(stderr, "Failed to open TTS output to memory. Error code: %d\n", result);
        return 1;
    }

    printf("Debug: TTS output opened to memory\n");

    // Test TTS engine
    result = TextToSpeechSpeak(ttsHandle, "Test", TTS_FORCE);
    if (result != MMSYSERR_NOERROR) {
        fprintf(stderr, "Failed to speak test phrase. Error code: %d\n", result);
        return 1;
    }
    printf("Debug: TTS engine test successful\n");

    return 0;
}

void ov_commands_cleanup(void)
{
    if (ttsHandle) {
        TextToSpeechCloseInMemory(ttsHandle);
        TextToSpeechShutdown(ttsHandle);
        ttsHandle = NULL;
    }
    if (ttsBuffer.lpData) {
        free(ttsBuffer.lpData);
    }
}

void ov_cmd_say(const char* text)
{
    printf("Debug: Entering ov_cmd_say with text: %s\n", text);

    if (!ttsHandle) {
        fprintf(stderr, "TTS engine not initialized\n");
        return;
    }

    // Reset buffer
    ttsBuffer.dwBufferLength = 0;

    // Speak the text
    MMRESULT result = TextToSpeechSpeak(ttsHandle, (char*)text, TTS_FORCE);
    if (result != MMSYSERR_NOERROR) {
        fprintf(stderr, "Failed to speak text. Error code: %d\n", result);
        return;
    }

    printf("Debug: Text spoken\n");

    // Wait for speech to complete
    result = TextToSpeechSync(ttsHandle);
    if (result != MMSYSERR_NOERROR) {
        fprintf(stderr, "Failed to sync TTS. Error code: %d\n", result);
        return;
    }

    printf("Debug: TTS synced\n");

    // Get the TTS output
    LPTTS_BUFFER_T pTtsBuffer = &ttsBuffer;
    result = TextToSpeechReturnBuffer(ttsHandle, &pTtsBuffer);
    if (result != MMSYSERR_NOERROR) {
        fprintf(stderr, "Failed to get TTS output. Error code: %d\n", result);
        return;
    }

    printf("Debug: TTS output retrieved. Buffer length: %lu\n", ttsBuffer.dwBufferLength);

    if (ttsBuffer.dwBufferLength == 0) {
        fprintf(stderr, "TTS output buffer is empty\n");
        return;
    }

    // Play the generated audio
    printf("Debug: Attempting to play buffer of length %lu\n", ttsBuffer.dwBufferLength);
    ov_output_play_buffer((float*)ttsBuffer.lpData, ttsBuffer.dwBufferLength / sizeof(float));

    printf("Debug: Audio playback initiated\n");
}

void ov_cmd_tts_say(const char* text)
{
    ov_cmd_say(text);  // For now, this is the same as ov_cmd_say
}

void ov_cmd_sync(void)
{
    if (ttsHandle) {
        TextToSpeechSync(ttsHandle);
    }
}

void ov_cmd_stop(void)
{
    if (ttsHandle) {
        TextToSpeechReset(ttsHandle, TRUE);
    }
}

void ov_cmd_letter(const char* text)
{
    if (ttsHandle && text[0]) {
        TextToSpeechTyping(ttsHandle, text[0]);
    }
}

void ov_cmd_tone(float pitch, int duration)
{
    float *buffer;
    uint32_t buffer_size;
    ov_audio_generate_tone(pitch, duration / 1000.0f, SAMPLE_RATE, &buffer, &buffer_size);
    if (buffer) {
        printf("Playing tone: %.2f Hz for %d ms\n", pitch, duration);
        ov_output_play_buffer(buffer, buffer_size / (2 * sizeof(float))); // Divide by 2 for stereo
        free(buffer);
    }
}

void ov_cmd_silence(int duration)
{
    // Implement silence by playing a buffer of zeros
    uint32_t buffer_size = (duration * SAMPLE_RATE) / 1000 * 2;  // *2 for stereo
    float *buffer = calloc(buffer_size, sizeof(float));
    if (buffer) {
        ov_output_play_buffer(buffer, buffer_size / 2);
        free(buffer);
    }
}

void ov_cmd_rate(int rate)
{
    if (ttsHandle) {
        TextToSpeechSetRate(ttsHandle, rate);
    }
}

void ov_cmd_split_caps(bool flag)
{
    // This functionality might not be directly supported by the Dectalk API
    // You may need to implement it in your text preprocessing
    printf("Split caps set to: %s\n", flag ? "true" : "false");
}

void ov_cmd_punctuations(const char* mode)
{
    // This functionality might not be directly supported by the Dectalk API
    // You may need to implement it in your text preprocessing
    printf("Punctuation mode set to: %s\n", mode);
}
