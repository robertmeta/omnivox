#ifndef OV_INPUT_H
#define OV_INPUT_H

#include <uv.h>

void ov_input_init(uv_loop_t *loop);
void ov_input_start_server(uv_loop_t *loop, int port);
void ov_input_start_stdin(uv_loop_t *loop);
void ov_input_cleanup(void);

#endif // OV_INPUT_H
