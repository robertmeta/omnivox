#include <uv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dtk/ttsapi.h>
#include <sndfile.h>
#include <portaudio.h>
#include <stdint.h>

#define DEFAULT_PORT 22222
#define DEFAULT_BACKLOG 128
#define MAX_LINE_LENGTH 1024
#define MAX_AUDIO_QUEUE 5

typedef struct {
    uv_tcp_t handle;
    char buffer[MAX_LINE_LENGTH];
    size_t buffer_len;
} client_t;

typedef struct {
    float *data;
    sf_count_t frames;
    int samplerate;
    int channels;
    int is_processed;
    int is_playing;
} audio_item_t;

typedef struct {
    BYTE *buffer;
    DWORD buffer_size;
    DWORD current_position;
} virtual_file_t;

LPTTS_HANDLE_T tts_handle;
uv_loop_t *loop;
uv_mutex_t audio_queue_mutex;
uv_cond_t audio_queue_cond;
audio_item_t audio_queue[MAX_AUDIO_QUEUE];
int audio_queue_size = 0;
int current_audio_index = 0;
PaStream *audio_stream;

void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    (void)handle;
    buf->base = malloc(suggested_size);
    buf->len = suggested_size;
}

void process_wav_in_memory(float *input_data, sf_count_t input_frames, int input_samplerate, float **output_data, sf_count_t *output_frames) {
    (void)input_samplerate;
    printf("Processing WAV in memory\n");

    *output_frames = input_frames;
    *output_data = malloc((size_t)(*output_frames) * 2 * sizeof(float));  // Allocate stereo output

    for (sf_count_t i = 0; i < input_frames; i++) {
        (*output_data)[i*2] = 0;           // Left channel (silent)
        (*output_data)[i*2+1] = input_data[i]; // Right channel
    }

    printf("WAV processing complete. Total frames processed: %lld\n", (long long)*output_frames);
}

int audio_callback(const void *inputBuffer, void *outputBuffer,
                   unsigned long framesPerBuffer,
                   const PaStreamCallbackTimeInfo* timeInfo,
                   PaStreamCallbackFlags statusFlags,
                   void *userData) {
    (void)inputBuffer;
    (void)timeInfo;
    (void)statusFlags;
    (void)userData;

    float *out = (float*)outputBuffer;
    static sf_count_t current_frame = 0;

    uv_mutex_lock(&audio_queue_mutex);
    if (audio_queue_size > 0 && !audio_queue[current_audio_index].is_playing && audio_queue[current_audio_index].is_processed) {
        audio_queue[current_audio_index].is_playing = 1;
        current_frame = 0;
    }
    uv_mutex_unlock(&audio_queue_mutex);

    if (audio_queue_size > 0 && audio_queue[current_audio_index].is_playing) {
        audio_item_t *current_item = &audio_queue[current_audio_index];
        sf_count_t frames_to_play = (sf_count_t)framesPerBuffer;
        if (current_frame + frames_to_play > current_item->frames) {
            frames_to_play = current_item->frames - current_frame;
        }

        memcpy(out, current_item->data + current_frame * current_item->channels, (size_t)(frames_to_play * current_item->channels) * sizeof(float));
        current_frame += frames_to_play;

        // If we didn't read enough frames, fill the rest with silence
        if (frames_to_play < (sf_count_t)framesPerBuffer) {
            for (sf_count_t i = frames_to_play * current_item->channels; i < (sf_count_t)(framesPerBuffer * (unsigned long)current_item->channels); i++) {
                out[i] = 0.0f;
            }
        }

        printf("Frames played: %lld / %lld, Total played: %lld\n", 
               (long long)frames_to_play, (long long)framesPerBuffer, (long long)current_frame);

        if (current_frame >= current_item->frames) {
            printf("End of audio reached\n");

            uv_mutex_lock(&audio_queue_mutex);
            free(current_item->data);
            memmove(&audio_queue[0], &audio_queue[1], sizeof(audio_item_t) * (MAX_AUDIO_QUEUE - 1));
            audio_queue_size--;
            if (current_audio_index > 0) current_audio_index--;
            uv_mutex_unlock(&audio_queue_mutex);

            uv_cond_signal(&audio_queue_cond);
        }
    } else {
        // No audio to play, output silence
        for (unsigned long i = 0; i < framesPerBuffer * 2; i++) {
            out[i] = 0.0f;
        }
    }

    return paContinue;
}

sf_count_t vio_get_filelen(void *user_data) {
    virtual_file_t *vf = (virtual_file_t *)user_data;
    return vf->buffer_size;
}

sf_count_t vio_seek(sf_count_t offset, int whence, void *user_data) {
    virtual_file_t *vf = (virtual_file_t *)user_data;
    switch (whence) {
        case SEEK_SET:
            if (offset <= UINT32_MAX) {
                vf->current_position = (DWORD)offset;
            } else {
                // Handle error
                return -1;
            }
            break;
        case SEEK_CUR:
            if (vf->current_position + offset <= UINT32_MAX) {
                vf->current_position += (DWORD)offset;
            } else {
                // Handle error
                return -1;
            }
            break;
        case SEEK_END:
            if (vf->buffer_size + offset <= UINT32_MAX) {
                vf->current_position = (DWORD)(vf->buffer_size + offset);
            } else {
                // Handle error
                return -1;
            }
            break;
    }
    return vf->current_position;
}

sf_count_t vio_read(void *ptr, sf_count_t count, void *user_data) {
    virtual_file_t *vf = (virtual_file_t *)user_data;
    sf_count_t to_read = (vf->current_position + count > vf->buffer_size) ? 
                         (vf->buffer_size - vf->current_position) : count;
    memcpy(ptr, vf->buffer + vf->current_position, (size_t)to_read);
    vf->current_position += (DWORD)to_read;
    return to_read;
}

sf_count_t vio_tell(void *user_data) {
    virtual_file_t *vf = (virtual_file_t *)user_data;
    return vf->current_position;
}

// Forward declaration of GetSpeechData
MMRESULT GetSpeechData(LPTTS_HANDLE_T handle, BYTE **buffer, DWORD *buffer_size);

// Add these functions before process_input
static sf_count_t mem_get_filelen(void *user_data) {
    return ((TTS_BUFFER_T*)user_data)->dwBufferLength;
}

static sf_count_t mem_seek(sf_count_t offset, int whence, void *user_data) {
    TTS_BUFFER_T *buf = (TTS_BUFFER_T*)user_data;
    switch(whence) {
        case SEEK_SET: buf->dwReserved = (DWORD)offset; break;
        case SEEK_CUR: buf->dwReserved += (DWORD)offset; break;
        case SEEK_END: buf->dwReserved = buf->dwBufferLength + (DWORD)offset; break;
    }
    return buf->dwReserved;
}

static sf_count_t mem_read(void *ptr, sf_count_t count, void *user_data) {
    TTS_BUFFER_T *buf = (TTS_BUFFER_T*)user_data;
    sf_count_t to_read = (buf->dwReserved + count > buf->dwBufferLength) ?
                         (buf->dwBufferLength - buf->dwReserved) : count;
    memcpy(ptr, buf->lpData + buf->dwReserved, (size_t)to_read);
    buf->dwReserved += (DWORD)to_read;
    return to_read;
}

static sf_count_t mem_tell(void *user_data) {
    return ((TTS_BUFFER_T*)user_data)->dwReserved;
}

void process_input(char* input) {
    char* newline = strchr(input, '\n');
    if (newline) *newline = '\0';

    printf("Processing input: %s\n", input);

    uv_mutex_lock(&audio_queue_mutex);
    if (audio_queue_size >= MAX_AUDIO_QUEUE) {
        printf("Audio queue full, skipping input\n");
        uv_mutex_unlock(&audio_queue_mutex);
        return;
    }

    MMRESULT result;

    // Open in-memory output
    result = TextToSpeechOpenInMemory(tts_handle, WAVE_FORMAT_1M16);
    if (result != MMSYSERR_NOERROR) {
        fprintf(stderr, "Error in TextToSpeechOpenInMemory: %d\n", result);
        uv_mutex_unlock(&audio_queue_mutex);
        return;
    }

    // Speak the text
    result = TextToSpeechSpeak(tts_handle, input, TTS_FORCE);
    if (result != MMSYSERR_NOERROR) {
        fprintf(stderr, "Error in TextToSpeechSpeak: %d\n", result);
        TextToSpeechCloseInMemory(tts_handle);
        uv_mutex_unlock(&audio_queue_mutex);
        return;
    }

    // Synchronize to ensure speech synthesis is complete
    result = TextToSpeechSync(tts_handle);
    if (result != MMSYSERR_NOERROR) {
        fprintf(stderr, "Error in TextToSpeechSync: %d\n", result);
        TextToSpeechCloseInMemory(tts_handle);
        uv_mutex_unlock(&audio_queue_mutex);
        return;
    }

    // Retrieve the speech data
    LPTTS_BUFFER_T ptts_buffer = NULL;
    result = TextToSpeechReturnBuffer(tts_handle, &ptts_buffer);
    if (result != MMSYSERR_NOERROR || ptts_buffer == NULL) {
        fprintf(stderr, "Error in TextToSpeechReturnBuffer: %d\n", result);
        TextToSpeechCloseInMemory(tts_handle);
        uv_mutex_unlock(&audio_queue_mutex);
        return;
    }

    // Close in-memory output
    result = TextToSpeechCloseInMemory(tts_handle);
    if (result != MMSYSERR_NOERROR) {
        fprintf(stderr, "Error in TextToSpeechCloseInMemory: %d\n", result);
        free(ptts_buffer->lpData);
        free(ptts_buffer);
        uv_mutex_unlock(&audio_queue_mutex);
        return;
    }

    printf("Generated speech in memory, size: %d bytes\n", ptts_buffer->dwBufferLength);

    // Process the speech data
    SF_INFO sfinfo = {0};
    sfinfo.samplerate = 11025;  // Assuming 11025 Hz sample rate, adjust if needed
    sfinfo.channels = 1;        // Mono output
    sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

    SF_VIRTUAL_IO virtual_io = {
        .get_filelen = mem_get_filelen,
        .seek = mem_seek,
        .read = mem_read,
        .write = NULL,
        .tell = mem_tell
    };

    ptts_buffer->dwReserved = 0;  // Use dwReserved as current position

    SNDFILE *infile = sf_open_virtual(&virtual_io, SFM_READ, &sfinfo, ptts_buffer);
    if (!infile) {
        fprintf(stderr, "Error opening virtual file: %s\n", sf_strerror(NULL));
        free(ptts_buffer->lpData);
        free(ptts_buffer);
        uv_mutex_unlock(&audio_queue_mutex);
        return;
    }

    float *input_data = malloc((size_t)sfinfo.frames * sizeof(float));
    sf_readf_float(infile, input_data, sfinfo.frames);
    sf_close(infile);

    float *processed_data;
    sf_count_t processed_frames;
    process_wav_in_memory(input_data, sfinfo.frames, sfinfo.samplerate, &processed_data, &processed_frames);

    free(input_data);
    free(ptts_buffer->lpData);
    free(ptts_buffer);

    audio_queue[audio_queue_size].data = processed_data;
    audio_queue[audio_queue_size].frames = processed_frames;
    audio_queue[audio_queue_size].samplerate = sfinfo.samplerate;
    audio_queue[audio_queue_size].channels = 2;  // We're converting to stereo
    audio_queue[audio_queue_size].is_processed = 1;
    audio_queue[audio_queue_size].is_playing = 0;
    audio_queue_size++;
    uv_mutex_unlock(&audio_queue_mutex);

    uv_cond_signal(&audio_queue_cond);
}void on_read(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf) {
    client_t* c = (client_t*)client->data;

    if (nread < 0) {
        if (nread != UV_EOF)
            fprintf(stderr, "Read error %s\n", uv_strerror((int)nread));
        uv_close((uv_handle_t*) client, NULL);
        return;
    }

    if (nread > 0) {
        for (ssize_t i = 0; i < nread; i++) {
            if (buf->base[i] == '\n' || c->buffer_len == MAX_LINE_LENGTH - 1) {
                c->buffer[c->buffer_len] = '\0';
                process_input(c->buffer);
                c->buffer_len = 0;
            } else {
                c->buffer[c->buffer_len++] = buf->base[i];
            }
        }
    }

    if (buf->base)
        free(buf->base);
}

void on_new_connection(uv_stream_t *server, int status) {
    if (status < 0) {
        fprintf(stderr, "New connection error %s\n", uv_strerror(status));
        return;
    }

    client_t *client = (client_t*)malloc(sizeof(client_t));
    uv_tcp_init(loop, &client->handle);
    client->buffer_len = 0;
    client->handle.data = client;

    if (uv_accept(server, (uv_stream_t*)&client->handle) == 0) {
        uv_read_start((uv_stream_t*)&client->handle, alloc_buffer, on_read);
    }
    else {
        uv_close((uv_handle_t*)&client->handle, NULL);
    }
}

void stdin_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
    if (nread < 0) {
        if (nread == UV_EOF) {
            // end of file
            uv_close((uv_handle_t*)stream, NULL);
        }
    } else if (nread > 0) {
        buf->base[nread] = '\0';  // Null-terminate the input
        char *cmd = strtok(buf->base, " ");
        if (cmd && strcmp(cmd, "ttssay") == 0) {
            char *text = strtok(NULL, "\n");
            if (text) {
                process_input(text);
            } else {
                printf("Usage: ttssay <text to speak>\n");
            }
        } else {
            printf("Unknown command. Use 'ttssay <text>' to speak.\n");
        }
    }

    if (buf->base)
        free(buf->base);
}

void check_portaudio_stream(uv_timer_t* handle) {
    (void)handle;
    if (Pa_IsStreamActive(audio_stream)) {
      //printf("PortAudio stream is active\n");
    } else {
      //printf("PortAudio stream is not active\n");
    }
}

void tts_callback(LONG lParam1, LONG lParam2, DWORD dwParam3, UINT uiParam4) {
    (void)lParam1;
    (void)lParam2;
    (void)dwParam3;
    (void)uiParam4;
    // Handle TTS callbacks here
}

int main(void) {
    MMRESULT result = TextToSpeechStartup(&tts_handle, 0, 0, tts_callback, 0);
    if (result != MMSYSERR_NOERROR) {
        fprintf(stderr, "Failed to initialize TTS\n");
        return 1;
    }

    PaError err;
    err = Pa_Initialize();
    if (err != paNoError) {
        fprintf(stderr, "PortAudio error: %s\n", Pa_GetErrorText(err));
        return 1;
    }

    // Print audio device info
    PaDeviceIndex numDevices = Pa_GetDeviceCount();
    printf("Number of audio devices: %d\n", numDevices);
    PaDeviceIndex defaultOutput = Pa_GetDefaultOutputDevice();
    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(defaultOutput);
    printf("Default output device: %s\n", deviceInfo->name);

    // Open PortAudio stream
    PaStreamParameters outputParameters;
    outputParameters.device = defaultOutput;
    outputParameters.channelCount = 2;  // Stereo output
    outputParameters.sampleFormat = paFloat32;
    outputParameters.suggestedLatency = deviceInfo->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = NULL;

    // TODO: remove hardcoded rate
    err = Pa_OpenStream(&audio_stream,
                        NULL,  // No input
                        &outputParameters,
                        11025,  // Sample rate
                        256,    // Frames per buffer
                        paClipOff,
                        audio_callback,
                        NULL);
    if (err != paNoError) {
        fprintf(stderr, "PortAudio error: %s\n", Pa_GetErrorText(err));
        return 1;
    }

    err = Pa_StartStream(audio_stream);
    if (err != paNoError) {
        fprintf(stderr, "PortAudio error: %s\n", Pa_GetErrorText(err));
        return 1;
    }

    printf("PortAudio stream started\n");

    loop = uv_default_loop();

    uv_mutex_init(&audio_queue_mutex);
    uv_cond_init(&audio_queue_cond);

    uv_tcp_t server;
    uv_tcp_init(loop, &server);

    struct sockaddr_in addr;
    uv_ip4_addr("0.0.0.0", DEFAULT_PORT, &addr);

    uv_tcp_bind(&server, (const struct sockaddr*)&addr, 0);
    int r = uv_listen((uv_stream_t*)&server, DEFAULT_BACKLOG, on_new_connection);
    if (r) {
        fprintf(stderr, "Listen error %s\n", uv_strerror(r));
        return 1;
    }

    // Set up STDIN handling
    uv_pipe_t stdin_pipe;
    uv_pipe_init(loop, &stdin_pipe, 0);
    uv_pipe_open(&stdin_pipe, 0);
    uv_read_start((uv_stream_t*)&stdin_pipe, alloc_buffer, stdin_read);

    // Set up PortAudio stream check timer
    uv_timer_t check_audio_timer;
    uv_timer_init(loop, &check_audio_timer);
    uv_timer_start(&check_audio_timer, check_portaudio_stream, 0, 5000); // Check every 5 seconds

    printf("Server listening on port %d\n", DEFAULT_PORT);
    printf("Use 'ttssay <text>' to speak text\n");

    int run_result = uv_run(loop, UV_RUN_DEFAULT);

    // Cleanup
    Pa_StopStream(audio_stream);
    Pa_CloseStream(audio_stream);
    Pa_Terminate();

    return run_result;
}
