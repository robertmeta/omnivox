#ifndef OV_AUDIO_H
#define OV_AUDIO_H

#include <stdint.h>

void ov_audio_init(void);
void ov_audio_cleanup(void);

// Generate a sine wave tone
// frequency: in Hz
// duration: in seconds
// sample_rate: samples per second
// buffer: pointer to where the generated audio data should be stored
// buffer_size: pointer to store the size of the generated buffer
void ov_audio_generate_tone(float frequency, float duration, int sample_rate, float **buffer, uint32_t *buffer_size);

#endif // OV_AUDIO_H
