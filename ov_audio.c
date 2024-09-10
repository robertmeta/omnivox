#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "ov_audio.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void ov_audio_init(void) {
    // Initialize any audio resources if needed
}

void ov_audio_cleanup(void) {
    // Clean up any audio resources if needed
}

void ov_audio_generate_tone(float frequency, float duration, int sample_rate, float **buffer, uint32_t *buffer_size) {
    uint32_t num_samples = (uint32_t)(duration * sample_rate);
    *buffer_size = num_samples * sizeof(float) * 2; // 2 channels (stereo)
    *buffer = (float*)malloc(*buffer_size);

    if (*buffer == NULL) {
        fprintf(stderr, "Failed to allocate memory for audio buffer\n");
        return;
    }

    for (uint32_t i = 0; i < num_samples; i++) {
        float t = (float)i / sample_rate;
        float sample = sinf(2 * M_PI * frequency * t);
        
        // Left channel
        (*buffer)[i*2] = sample;
        // Right channel
        (*buffer)[i*2 + 1] = sample;
    }
}
