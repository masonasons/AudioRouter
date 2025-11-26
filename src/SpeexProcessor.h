#pragma once

#include "NoiseReductionTypes.h"
#include <vector>

#ifdef HAVE_SPEEX
// Forward declaration for Speex preprocessor state
struct SpeexPreprocessState_;
typedef struct SpeexPreprocessState_ SpeexPreprocessState;
#else
typedef void SpeexPreprocessState;
#endif

class SpeexProcessor : public INoiseProcessor
{
public:
    SpeexProcessor(const SpeexConfig& config = SpeexConfig());
    ~SpeexProcessor() override;

    // INoiseProcessor interface
    bool Initialize(unsigned int sampleRate, unsigned int channels) override;
    void Process(float* audioData, unsigned int frameCount, unsigned int channels) override;
    const wchar_t* GetName() const override { return L"Speex"; }
    unsigned int GetRequiredFrameSize() const override { return m_frameSize; }
    unsigned int GetRequiredSampleRate() const override { return 0; } // Speex supports any rate
    void SetDiagnosticCallback(std::function<void(const std::wstring&)> callback) override
    {
        m_diagnosticCallback = callback;
    }

    // Check if Speex is available at compile time
    static bool IsAvailable() {
#ifdef HAVE_SPEEX
        return true;
#else
        return false;
#endif
    }

    // Update configuration (can be called at runtime)
    void UpdateConfig(const SpeexConfig& config);

    // Get current configuration
    const SpeexConfig& GetConfig() const { return m_config; }

private:
#ifdef HAVE_SPEEX
    void ApplyConfig();
#endif

    SpeexPreprocessState* m_state;
    SpeexConfig m_config;
    bool m_isInitialized;

    // Audio format
    unsigned int m_sampleRate;
    unsigned int m_channels;
    unsigned int m_frameSize;  // Frame size in samples (typically 10-30ms worth)

#ifdef HAVE_SPEEX
    // Processing buffers
    std::vector<short> m_frameBuffer;         // Buffer for Speex processing (int16)
    std::vector<float> m_monoBuffer;          // Mono conversion buffer
    std::vector<float> m_outputBuffer;        // Output buffer for processed frames

    // Frame accumulation state
    unsigned int m_accumulatedSamples;
    unsigned int m_outputBufferReadPos;
    unsigned int m_outputBufferAvailable;
#endif

    // Diagnostic counters
    unsigned int m_totalFramesProcessed;

    // Diagnostic callback
    std::function<void(const std::wstring&)> m_diagnosticCallback;
};
