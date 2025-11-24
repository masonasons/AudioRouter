#pragma once

#include <vector>
#include <functional>
#include <string>

// Forward declaration for RNNoise state
typedef struct DenoiseState DenoiseState;

class NoiseSuppress
{
public:
    NoiseSuppress();
    ~NoiseSuppress();

    bool Initialize();  // Returns true if successful, false if failed
    void Process(float* audioData, unsigned int frameCount, unsigned int channels);

    // Diagnostic function to get processing stats
    unsigned int GetProcessedFrameCount() const { return m_totalFramesProcessed; }

    // Set callback for diagnostic messages
    void SetDiagnosticCallback(std::function<void(const std::wstring&)> callback) { m_diagnosticCallback = callback; }

private:
    DenoiseState* m_state;
    bool m_isInitialized;

    // Audio format tracking
    unsigned int m_inputSampleRate;
    unsigned int m_inputChannels;

    // Processing buffers
    std::vector<float> m_frameBuffer;         // Accumulation buffer for 480-sample frames
    std::vector<float> m_monoBuffer;          // Mono conversion buffer
    std::vector<float> m_processedBuffer;     // Processed output buffer
    std::vector<float> m_resampleBuffer;      // Resampling buffer (if needed)
    std::vector<float> m_outputBuffer;        // Output buffer for processed frames

    // Frame accumulation state
    unsigned int m_accumulatedSamples;        // How many samples currently in frame buffer
    unsigned int m_outputBufferReadPos;       // Read position in output buffer
    unsigned int m_outputBufferAvailable;     // Available samples in output buffer

    // Diagnostic counters
    unsigned int m_totalFramesProcessed;

    // Diagnostic callback
    std::function<void(const std::wstring&)> m_diagnosticCallback;
};
