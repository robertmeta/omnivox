#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>
#include "ov_input.h"

#define MAX_INPUT_LENGTH 1024

static uv_tcp_t server;
static uv_pipe_t stdin_pipe;

static void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    buf->base = malloc(suggested_size);
    buf->len = suggested_size;
}

static void on_read(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf) {
    if (nread > 0) {
        buf->base[nread] = '\0';  // Null-terminate the input
        printf("Received input: %s", buf->base);
        // TODO: Process the input (implement command processing here)
    }
    if (nread < 0) {
        if (nread != UV_EOF) {
            fprintf(stderr, "Read error: %s\n", uv_strerror(nread));
        }
        uv_close((uv_handle_t*)client, NULL);
    }
    free(buf->base);
}

static void on_new_connection(uv_stream_t *server, int status) {
    if (status < 0) {
        fprintf(stderr, "New connection error: %s\n", uv_strerror(status));
        return;
    }

    uv_tcp_t *client = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
    uv_tcp_init(uv_default_loop(), client);

    if (uv_accept(server, (uv_stream_t*)client) == 0) {
        uv_read_start((uv_stream_t*)client, alloc_buffer, on_read);
    } else {
        uv_close((uv_handle_t*)client, NULL);
    }
}

void ov_input_init(uv_loop_t *loop) {
    // Nothing to initialize for now
}

void ov_input_start_server(uv_loop_t *loop, int port) {
    struct sockaddr_in addr;
    uv_ip4_addr("0.0.0.0", port, &addr);

    uv_tcp_init(loop, &server);
    uv_tcp_bind(&server, (const struct sockaddr*)&addr, 0);

    int r = uv_listen((uv_stream_t*)&server, 128, on_new_connection);
    if (r) {
        fprintf(stderr, "Listen error: %s\n", uv_strerror(r));
        exit(1);
    }
    printf("Listening on port %d\n", port);
}

void ov_input_start_stdin(uv_loop_t *loop) {
    uv_pipe_init(loop, &stdin_pipe, 0);
    uv_pipe_open(&stdin_pipe, 0);
    uv_read_start((uv_stream_t*)&stdin_pipe, alloc_buffer, on_read);
}

void ov_input_cleanup(void) {
    // Cleanup resources if needed
}
