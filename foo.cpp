#include <iostream>
#include <vector>
#include <cstring>
#include <portaudio.h>
#include <ttsapi.h>

// Function to check PortAudio errors
void handlePaError(PaError err) {
    if (err != paNoError) {
        std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
        exit(1);
    }
}

// Callback function for PortAudio
static int paCallback(const void* inputBuffer, void* outputBuffer,
                      unsigned long framesPerBuffer,
                      const PaStreamCallbackTimeInfo* timeInfo,
                      PaStreamCallbackFlags statusFlags,
                      void* userData) {
    (void)inputBuffer; // Unused parameter
    (void)timeInfo;    // Unused parameter
    (void)statusFlags; // Unused parameter

    float* out = static_cast<float*>(outputBuffer);
    const std::vector<short>* audioData = static_cast<const std::vector<short>*>(userData);
    static size_t position = 0;

    for (unsigned int i = 0; i < framesPerBuffer; i++) {
        if (position < audioData->size()) {
            // Left channel (index 0) gets the audio, right channel (index 1) is silent
            out[i * 2] = (*audioData)[position] / 32768.0f;
            out[i * 2 + 1] = 0.0f;
            position++;
        } else {
            // If we've reached the end of the audio data, output silence
            out[i * 2] = 0.0f;
            out[i * 2 + 1] = 0.0f;
        }
    }

    return paContinue;
}

int main() {
    LPTTS_HANDLE_T phTTS = nullptr;
    MMRESULT result;

    std::cout << "Initializing DECtalk..." << std::endl;
    // Initialize DECtalk
    result = TextToSpeechStartup(&phTTS, WAVE_MAPPER, 0, nullptr, 0);
    if (result != MMSYSERR_NOERROR) {
        std::cerr << "Failed to initialize DECtalk. Error code: " << result << std::endl;
        return 1;
    }
    std::cout << "DECtalk initialized successfully." << std::endl;

    std::cout << "Setting up speech-to-memory mode..." << std::endl;
    // Set up speech-to-memory mode
    result = TextToSpeechOpenInMemory(phTTS, WAVE_FORMAT_1M16);
    if (result != MMSYSERR_NOERROR) {
        std::cerr << "Failed to open speech-to-memory mode. Error code: " << result << std::endl;
        TextToSpeechShutdown(phTTS);
        return 1;
    }
    std::cout << "Speech-to-memory mode set up successfully." << std::endl;

    // Prepare the text to be spoken
    const char* text = "Hello, this is a test of DECtalk and PortAudio.";

    std::cout << "Speaking text to memory..." << std::endl;
    // Speak the text (to memory)
    result = TextToSpeechSpeak(phTTS, const_cast<char*>(text), TTS_FORCE);
    if (result != MMSYSERR_NOERROR) {
        std::cerr << "Failed to speak text. Error code: " << result << std::endl;
        TextToSpeechCloseInMemory(phTTS);
        TextToSpeechShutdown(phTTS);
        return 1;
    }
    std::cout << "Text spoken to memory successfully." << std::endl;

    std::cout << "Retrieving audio data from memory..." << std::endl;
    // Get the audio data from memory
    std::vector<short> audioData;
    TTS_BUFFER_T ttsBuffer;
    LPTTS_BUFFER_T pTtsBuffer = &ttsBuffer;
    while (true) {
        result = TextToSpeechReturnBuffer(phTTS, &pTtsBuffer);
        if (result != MMSYSERR_NOERROR) {
            std::cerr << "Error retrieving buffer. Error code: " << result << std::endl;
            break;
        }
        if (pTtsBuffer->dwBufferLength == 0) {
            std::cout << "Reached end of audio data." << std::endl;
            break;
        }
        short* shortData = reinterpret_cast<short*>(pTtsBuffer->lpData);
        audioData.insert(audioData.end(), 
                         shortData,
                         shortData + pTtsBuffer->dwBufferLength / sizeof(short));
    }
    std::cout << "Retrieved " << audioData.size() << " audio samples." << std::endl;

    std::cout << "Cleaning up DECtalk..." << std::endl;
    // Clean up DECtalk
    TextToSpeechCloseInMemory(phTTS);
    TextToSpeechShutdown(phTTS);
    std::cout << "DECtalk cleanup complete." << std::endl;

    if (audioData.empty()) {
        std::cerr << "No audio data generated. Exiting." << std::endl;
        return 1;
    }

    std::cout << "Initializing PortAudio..." << std::endl;
    // Initialize PortAudio
    handlePaError(Pa_Initialize());

    std::cout << "Opening PortAudio stream..." << std::endl;
    // Open PortAudio stream
    PaStream* stream;
    handlePaError(Pa_OpenDefaultStream(&stream,
                                       0,          // No input channels
                                       2,          // Stereo output
                                       paFloat32,  // 32-bit floating point output
                                       44100,      // Sample rate
                                       paFramesPerBufferUnspecified,
                                       paCallback,
                                       &audioData));

    std::cout << "Starting PortAudio stream..." << std::endl;
    // Start the stream
    handlePaError(Pa_StartStream(stream));

    std::cout << "Playing audio... Press Enter to stop." << std::endl;
    std::cin.get();

    std::cout << "Stopping PortAudio stream..." << std::endl;
    // Stop and close the stream
    handlePaError(Pa_StopStream(stream));
    handlePaError(Pa_CloseStream(stream));

    std::cout << "Terminating PortAudio..." << std::endl;
    // Terminate PortAudio
    handlePaError(Pa_Terminate());

    std::cout << "Program completed successfully." << std::endl;
    return 0;
}
