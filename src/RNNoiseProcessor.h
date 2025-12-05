#pragma once

#include "NoiseReductionTypes.h"
#include <vector>

#ifdef HAVE_RNNOISE
// Forward declaration for RNNoise state
typedef struct DenoiseState DenoiseState;
#else
typedef void DenoiseState;
#endif

class RNNoiseProcessor : public INoiseProcessor
{
public:
    RNNoiseProcessor(const RNNoiseConfig& config = RNNoiseConfig());
    ~RNNoiseProcessor() override;

    // Update configuration
    void UpdateConfig(const RNNoiseConfig& config);

    // INoiseProcessor interface
    bool Initialize(unsigned int sampleRate, unsigned int channels) override;
    void Process(float* audioData, unsigned int frameCount, unsigned int channels) override;
    const wchar_t* GetName() const override { return L"RNNoise"; }
    unsigned int GetRequiredFrameSize() const override { return 480; }
    unsigned int GetRequiredSampleRate() const override { return 48000; }
    void SetDiagnosticCallback(std::function<void(const std::wstring&)> callback) override
    {
        m_diagnosticCallback = callback;
    }

    // Check if RNNoise is available at compile time
    static bool IsAvailable() {
#ifdef HAVE_RNNOISE
        return true;
#else
        return false;
#endif
    }

    // Diagnostic function to get processing stats
    unsigned int GetProcessedFrameCount() const { return m_totalFramesProcessed; }

private:
    DenoiseState* m_state;
    bool m_isInitialized;
    RNNoiseConfig m_config;

#ifdef HAVE_RNNOISE
    // Audio format tracking
    unsigned int m_inputSampleRate;
    unsigned int m_inputChannels;

    // Processing buffers
    std::vector<float> m_frameBuffer;         // Accumulation buffer for 480-sample frames
    std::vector<float> m_monoBuffer;          // Mono conversion buffer
    std::vector<float> m_processedBuffer;     // Processed output buffer
    std::vector<float> m_outputBuffer;        // Output buffer for processed frames

    // Frame accumulation state
    unsigned int m_accumulatedSamples;        // How many samples currently in frame buffer
    unsigned int m_outputBufferReadPos;       // Read position in output buffer
    unsigned int m_outputBufferAvailable;     // Available samples in output buffer

    // VAD state for grace period
    float m_lastVadProbability;               // Last VAD probability from RNNoise
    float m_vadGraceSamplesRemaining;         // Samples remaining in grace period
#endif

    // Diagnostic counters
    unsigned int m_totalFramesProcessed;

    // Diagnostic callback
    std::function<void(const std::wstring&)> m_diagnosticCallback;
};
