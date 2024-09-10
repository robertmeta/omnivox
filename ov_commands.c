#include <stdio.h>
#include <stdlib.h>  // Added for free()
#include <stdbool.h>
#include "ov_commands.h"
#include "ov_audio.h"
#include "ov_output.h"

#define SAMPLE_RATE 44100

void ov_cmd_say(const char* text) {
    printf("Saying: %s\n", text);
    // TODO: Implement actual TTS functionality
}

void ov_cmd_tts_say(const char* text) {
    ov_cmd_say(text);  // For now, this is the same as ov_cmd_say
}

void ov_cmd_sync(void) {
    printf("Syncing speech output\n");
    // TODO: Implement sync functionality
}

void ov_cmd_stop(void) {
    printf("Stopping speech output\n");
    // TODO: Implement stop functionality
}

void ov_cmd_letter(const char* text) {
    printf("Speaking letter: %s\n", text);
    // TODO: Implement letter speaking
}

void ov_cmd_tone(float pitch, int duration) {
    float *buffer;
    uint32_t buffer_size;
    ov_audio_generate_tone(pitch, duration / 1000.0f, SAMPLE_RATE, &buffer, &buffer_size);
    if (buffer) {
        printf("Playing tone: %.2f Hz for %d ms\n", pitch, duration);
        ov_output_play_buffer(buffer, buffer_size / (2 * sizeof(float))); // Divide by 2 for stereo
        free(buffer);
    }
}

void ov_cmd_silence(int duration) {
    printf("Inserting silence for %d ms\n", duration);
    // TODO: Implement silence insertion
}

void ov_cmd_rate(int rate) {
    printf("Setting speech rate to %d\n", rate);
    // TODO: Implement rate change
}

void ov_cmd_split_caps(bool flag) {
    printf("Setting split caps to %s\n", flag ? "true" : "false");
    // TODO: Implement split caps functionality
}

void ov_cmd_punctuations(const char* mode) {
    printf("Setting punctuation mode to %s\n", mode);
    // TODO: Implement punctuation mode change
}
