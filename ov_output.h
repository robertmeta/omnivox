#ifndef OV_OUTPUT_H
#define OV_OUTPUT_H

#include <portaudio.h>

typedef struct {
    const float *buffer;
    unsigned long frameIndex;
    unsigned long maxFrameIndex;
} paTestData;

int ov_output_init(void);
int ov_output_play_buffer(const float *buffer, unsigned long frames);
void ov_output_cleanup(void);

#endif // OV_OUTPUT_H
