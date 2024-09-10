#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

#include "ov_input.h"
#include "ov_audio.h"
#include "ov_processing.h"
#include "ov_output.h"
#include "ov_commands.h"

#define MAX_INPUT_LENGTH 1024
#define DEFAULT_PORT 22222
#define SAMPLE_RATE 44100

// Function prototypes
void process_command(const char* command);
void on_read(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf);
void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);

// Function to process commands
void process_command(const char* command) {
    char cmd[32];
    char args[MAX_INPUT_LENGTH];

    if (sscanf(command, "%31s %[^\n]", cmd, args) < 1) {
        printf("Invalid command format\n");
        return;
    }

    if (strcmp(cmd, "s") == 0 || strcmp(cmd, "say") == 0 || strcmp(cmd, "tts_say") == 0) {
        ov_cmd_tts_say(args);
    } else if (strcmp(cmd, "sync") == 0) {
        ov_cmd_sync();
    } else if (strcmp(cmd, "st") == 0 || strcmp(cmd, "stop") == 0) {
        ov_cmd_stop();
    } else if (strcmp(cmd, "l") == 0 || strcmp(cmd, "letter") == 0) {
        ov_cmd_letter(args);
    } else if (strcmp(cmd, "t") == 0 || strcmp(cmd, "tone") == 0) {
        float pitch;
        int duration;
        if (sscanf(args, "%f %d", &pitch, &duration) == 2) {
            ov_cmd_tone(pitch, duration);
        } else {
            printf("Invalid tone command format. Use: tone <pitch> <duration>\n");
        }
    } else if (strcmp(cmd, "sh") == 0 || strcmp(cmd, "silence") == 0) {
        int duration;
        if (sscanf(args, "%d", &duration) == 1) {
            ov_cmd_silence(duration);
        } else {
            printf("Invalid silence command format. Use: silence <duration>\n");
        }
    } else if (strcmp(cmd, "r") == 0 || strcmp(cmd, "rate") == 0) {
        int rate;
        if (sscanf(args, "%d", &rate) == 1) {
            ov_cmd_rate(rate);
        } else {
            printf("Invalid rate command format. Use: rate <value>\n");
        }
    } else if (strcmp(cmd, "split_caps") == 0) {
        bool flag = (strcmp(args, "1") == 0 || strcmp(args, "true") == 0);
        ov_cmd_split_caps(flag);
    } else if (strcmp(cmd, "punctuations") == 0) {
        ov_cmd_punctuations(args);
    } else {
        printf("Unknown command: %s\n", cmd);
    }
}

// Callback for input reading
void on_read(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf) {
    if (nread > 0) {
        buf->base[nread] = '\0';  // Null-terminate the input
        process_command(buf->base);
    }
    if (nread < 0) {
        if (nread != UV_EOF) {
            fprintf(stderr, "Read error: %s\n", uv_strerror(nread));
        }
        uv_close((uv_handle_t*)client, NULL);
    }
    free(buf->base);
}

// Allocation callback for libuv
void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    (void)handle;  // Unused parameter
    buf->base = (char*)malloc(suggested_size);
    buf->len = suggested_size;
}

int main(void) {
    uv_loop_t *loop;
    uv_pipe_t stdin_pipe;

    loop = uv_default_loop();

    ov_input_init(loop);
    ov_audio_init();

    int ret = ov_output_init();
    if (ret != 0) {
        fprintf(stderr, "Failed to initialize audio output\n");
        return 1;
    }

    ret = ov_commands_init();
    if (ret != 0) {
        fprintf(stderr, "Failed to initialize TTS commands\n");
        return 1;
    }

    const char *listen_env = getenv("OMNIVOX_LISTEN");
    if (listen_env && strlen(listen_env) > 0) {
        const char *port_env = getenv("OMNIVOX_PORT");
        int port = port_env ? atoi(port_env) : DEFAULT_PORT;
        ov_input_start_server(loop, port);
    }

    uv_pipe_init(loop, &stdin_pipe, 0);
    uv_pipe_open(&stdin_pipe, 0);
    uv_read_start((uv_stream_t*)&stdin_pipe, alloc_buffer, on_read);

    printf("Omnivox initialized. Enter commands:\n");

    uv_run(loop, UV_RUN_DEFAULT);

    ov_input_cleanup();
    ov_audio_cleanup();
    ov_output_cleanup();
    ov_commands_cleanup();
    
    return 0;
}
