#ifndef OV_PROCESSING_H
#define OV_PROCESSING_H

void ov_processing_init(void);
void ov_processing_add_to_queue(const char *wav_file);
void ov_processing_set_channel(int channel); // 0 for both, 1 for left, 2 for right
void ov_processing_cleanup(void);

#endif // OV_PROCESSING_H
