#ifndef OV_COMMANDS_H
#define OV_COMMANDS_H

#include <stdbool.h>

int ov_commands_init(void);
void ov_commands_cleanup(void);

void ov_cmd_say(const char* text);
void ov_cmd_tts_say(const char* text);
void ov_cmd_sync(void);
void ov_cmd_stop(void);
void ov_cmd_letter(const char* text);
void ov_cmd_tone(float pitch, int duration);
void ov_cmd_silence(int duration);
void ov_cmd_rate(int rate);
void ov_cmd_split_caps(bool flag);
void ov_cmd_punctuations(const char* mode);

#endif // OV_COMMANDS_H
