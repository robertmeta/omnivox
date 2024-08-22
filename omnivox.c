#include <uv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dtk/ttsapi.h>
#include <sndfile.h>
#include <portaudio.h>

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
    char *filename;
    char *processed_filename;
    int is_processed;
    int is_playing;
} audio_item_t;

LPTTS_HANDLE_T tts_handle;
uv_loop_t *loop;
uv_mutex_t audio_queue_mutex;
uv_cond_t audio_queue_cond;
audio_item_t audio_queue[MAX_AUDIO_QUEUE];
int audio_queue_size = 0;
int current_audio_index = 0;
PaStream *audio_stream;

void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    buf->base = malloc(suggested_size);
    buf->len = suggested_size;
}

void process_wav(const char* input_file, const char* output_file) {
    printf("Processing WAV file: %s -> %s\n", input_file, output_file);

    SF_INFO sfinfo;
    SNDFILE* infile = sf_open(input_file, SFM_READ, &sfinfo);
    if (!infile) {
        fprintf(stderr, "Error opening input file: %s\n", sf_strerror(NULL));
        return;
    }
    printf("Input file opened successfully. Channels: %d, Samplerate: %d\n", sfinfo.channels, sfinfo.samplerate);

    sfinfo.channels = 2; // Set to stereo
    SNDFILE* outfile = sf_open(output_file, SFM_WRITE, &sfinfo);
    if (!outfile) {
        fprintf(stderr, "Error opening output file: %s\n", sf_strerror(NULL));
        sf_close(infile);
        return;
    }
    printf("Output file opened successfully\n");

    float buffer[1024];
    float stereo_buffer[2048];
    sf_count_t read_count;
    sf_count_t total_frames_processed = 0;

    while ((read_count = sf_read_float(infile, buffer, 1024)) > 0) {
        for (int i = 0; i < read_count; i++) {
            stereo_buffer[i*2] = 0;           // Left channel (silent)
            stereo_buffer[i*2+1] = buffer[i]; // Right channel
        }
        sf_count_t frames_written = sf_writef_float(outfile, stereo_buffer, read_count);
        if (frames_written != read_count) {
            fprintf(stderr, "Error writing to output file\n");
            break;
        }
        total_frames_processed += read_count;
    }

    sf_close(infile);
    sf_close(outfile);

    printf("WAV processing complete. Total frames processed: %lld\n", (long long)total_frames_processed);
}

int audio_callback(const void *inputBuffer, void *outputBuffer,
                   unsigned long framesPerBuffer,
                   const PaStreamCallbackTimeInfo* timeInfo,
                   PaStreamCallbackFlags statusFlags,
                   void *userData) {
    float *out = (float*)outputBuffer;
    static SF_INFO sfinfo;
    static SNDFILE *file = NULL;
    static sf_count_t total_frames_read = 0;

    (void) inputBuffer; // Prevent unused variable warning

    uv_mutex_lock(&audio_queue_mutex);
    if (audio_queue_size > 0 && !audio_queue[current_audio_index].is_playing && audio_queue[current_audio_index].is_processed) {
        if (file) sf_close(file);
        file = sf_open(audio_queue[current_audio_index].processed_filename, SFM_READ, &sfinfo);
        if (!file) {
            fprintf(stderr, "Error opening file: %s\n", sf_strerror(NULL));
            uv_mutex_unlock(&audio_queue_mutex);
            return paComplete;
        }
        printf("Opened file: %s\n", audio_queue[current_audio_index].processed_filename);
        audio_queue[current_audio_index].is_playing = 1;
        total_frames_read = 0;
    }
    uv_mutex_unlock(&audio_queue_mutex);

    if (file) {
        sf_count_t frames_read = sf_readf_float(file, out, framesPerBuffer);
        total_frames_read += frames_read;

        // If we didn't read enough frames, fill the rest with silence
        if (frames_read < framesPerBuffer) {
            for (sf_count_t i = frames_read * sfinfo.channels; i < framesPerBuffer * sfinfo.channels; i++) {
                out[i] = 0.0f;
            }
        }

        printf("Frames read: %lld / %lld, Total read: %lld\n", 
               (long long)frames_read, (long long)framesPerBuffer, (long long)total_frames_read);

        if (frames_read == 0) {
            printf("End of file reached\n");
            sf_close(file);
            file = NULL;

            uv_mutex_lock(&audio_queue_mutex);
            free(audio_queue[current_audio_index].filename);
            free(audio_queue[current_audio_index].processed_filename);
            memmove(&audio_queue[0], &audio_queue[1], sizeof(audio_item_t) * (MAX_AUDIO_QUEUE - 1));
            audio_queue_size--;
            if (current_audio_index > 0) current_audio_index--;
            uv_mutex_unlock(&audio_queue_mutex);

            uv_cond_signal(&audio_queue_cond);
        }
    } else {
        // No file to play, output silence
        for (unsigned long i = 0; i < framesPerBuffer * 2; i++) {
            out[i] = 0.0f;
        }
    }

    return paContinue;
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

    char *filename = malloc(32);
    char *processed_filename = malloc(32);
    sprintf(filename, "output_%d.wav", audio_queue_size);
    sprintf(processed_filename, "processed_%d.wav", audio_queue_size);

    audio_queue[audio_queue_size].filename = filename;
    audio_queue[audio_queue_size].processed_filename = processed_filename;
    audio_queue[audio_queue_size].is_processed = 0;
    audio_queue[audio_queue_size].is_playing = 0;
    audio_queue_size++;
    uv_mutex_unlock(&audio_queue_mutex);

    TextToSpeechOpenWaveOutFile(tts_handle, filename, WAVE_FORMAT_1M16);
    TextToSpeechSpeak(tts_handle, input, TTS_FORCE);
    TextToSpeechSync(tts_handle);
    TextToSpeechCloseWaveOutFile(tts_handle);

    printf("Generated WAV file: %s\n", filename);

    process_wav(filename, processed_filename);

    uv_mutex_lock(&audio_queue_mutex);
    audio_queue[audio_queue_size - 1].is_processed = 1;
    uv_mutex_unlock(&audio_queue_mutex);

    uv_cond_signal(&audio_queue_cond);
}

void on_read(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf) {
    client_t* c = (client_t*)client->data;

    if (nread < 0) {
        if (nread != UV_EOF)
            fprintf(stderr, "Read error %s\n", uv_err_name(nread));
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
    if (Pa_IsStreamActive(audio_stream)) {
      //printf("PortAudio stream is active\n");
    } else {
      //printf("PortAudio stream is not active\n");
    }
}

void tts_callback(LONG lParam1, LONG lParam2, DWORD dwParam3, UINT uiParam4) {
    // Handle TTS callbacks here
}


int main() {
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
