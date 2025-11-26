#include "SpeexProcessor.h"

#ifdef HAVE_SPEEX
#include <speex/speex_preprocess.h>
#endif

#include <cstring>
#include <algorithm>
#include <cmath>
#include <sstream>

#ifndef HAVE_SPEEX
// Stub implementation when Speex is not available
SpeexProcessor::SpeexProcessor(const SpeexConfig& config)
    : m_state(nullptr), m_config(config), m_isInitialized(false), m_sampleRate(0), m_channels(0), m_frameSize(0), m_totalFramesProcessed(0) {}
SpeexProcessor::~SpeexProcessor() {}
bool SpeexProcessor::Initialize(unsigned int, unsigned int) {
    if (m_diagnosticCallback) m_diagnosticCallback(L"Speex not available (not compiled in)");
    return false;
}
void SpeexProcessor::Process(float*, unsigned int, unsigned int) {}
void SpeexProcessor::UpdateConfig(const SpeexConfig& config) { m_config = config; }
#else

SpeexProcessor::SpeexProcessor(const SpeexConfig& config)
    : m_state(nullptr)
    , m_config(config)
    , m_isInitialized(false)
    , m_sampleRate(0)
    , m_channels(0)
    , m_frameSize(0)
    , m_accumulatedSamples(0)
    , m_outputBufferReadPos(0)
    , m_outputBufferAvailable(0)
    , m_totalFramesProcessed(0)
{
}

SpeexProcessor::~SpeexProcessor()
{
    if (m_state)
    {
        speex_preprocess_state_destroy(m_state);
    }
}

bool SpeexProcessor::Initialize(unsigned int sampleRate, unsigned int channels)
{
    if (m_isInitialized)
    {
        // If already initialized with same parameters, just return success
        if (m_sampleRate == sampleRate && m_channels == channels)
            return (m_state != nullptr);

        // Different parameters - need to reinitialize
        if (m_state)
        {
            speex_preprocess_state_destroy(m_state);
            m_state = nullptr;
        }
    }

    m_sampleRate = sampleRate;
    m_channels = channels;

    // Calculate frame size based on sample rate
    // Speex preprocess works best with 10-30ms frames
    // Using 20ms as a good balance
    m_frameSize = (sampleRate * 20) / 1000;  // 20ms worth of samples

    if (m_diagnosticCallback)
    {
        std::wostringstream msg;
        msg << L"Speex frame size: " << m_frameSize << L" samples (20ms at " << sampleRate << L" Hz)";
        m_diagnosticCallback(msg.str());
    }

    // Create Speex preprocessor state
    m_state = speex_preprocess_state_init(m_frameSize, sampleRate);

    if (!m_state)
    {
        if (m_diagnosticCallback)
        {
            m_diagnosticCallback(L"ERROR: speex_preprocess_state_init() returned NULL!");
        }
        return false;
    }

    // Apply configuration
    ApplyConfig();

    // Pre-allocate buffers
    m_frameBuffer.resize(m_frameSize);
    m_monoBuffer.resize(m_frameSize * 2);  // Extra space
    m_outputBuffer.resize(m_frameSize * 2);

    // Reset accumulation state
    m_accumulatedSamples = 0;
    m_outputBufferReadPos = 0;
    m_outputBufferAvailable = 0;
    m_totalFramesProcessed = 0;

    m_isInitialized = true;

    if (m_diagnosticCallback)
    {
        std::wostringstream msg;
        msg << L"Speex preprocessor initialized (suppression=" << m_config.noiseSuppressionLevel << L" dB)";
        m_diagnosticCallback(msg.str());
    }

    return true;
}

void SpeexProcessor::ApplyConfig()
{
    if (!m_state)
        return;

    // Enable noise suppression
    int denoise = 1;
    speex_preprocess_ctl(m_state, SPEEX_PREPROCESS_SET_DENOISE, &denoise);

    // Set noise suppression level (in dB, negative value)
    int noiseSuppress = m_config.noiseSuppressionLevel;
    speex_preprocess_ctl(m_state, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &noiseSuppress);

    // Configure VAD if enabled
    int vad = m_config.enableVAD ? 1 : 0;
    speex_preprocess_ctl(m_state, SPEEX_PREPROCESS_SET_VAD, &vad);

    // Configure AGC if enabled
    int agc = m_config.enableAGC ? 1 : 0;
    speex_preprocess_ctl(m_state, SPEEX_PREPROCESS_SET_AGC, &agc);

    if (m_config.enableAGC)
    {
        float agcLevel = (float)m_config.agcLevel;
        speex_preprocess_ctl(m_state, SPEEX_PREPROCESS_SET_AGC_LEVEL, &agcLevel);
    }

    // Configure dereverb if enabled
    int dereverb = m_config.enableDereverb ? 1 : 0;
    speex_preprocess_ctl(m_state, SPEEX_PREPROCESS_SET_DEREVERB, &dereverb);

    if (m_diagnosticCallback)
    {
        std::wostringstream msg;
        msg << L"Speex config: suppress=" << m_config.noiseSuppressionLevel
            << L"dB, VAD=" << (m_config.enableVAD ? L"on" : L"off")
            << L", AGC=" << (m_config.enableAGC ? L"on" : L"off")
            << L", Dereverb=" << (m_config.enableDereverb ? L"on" : L"off");
        m_diagnosticCallback(msg.str());
    }
}

void SpeexProcessor::UpdateConfig(const SpeexConfig& config)
{
    m_config = config;
    if (m_isInitialized && m_state)
    {
        ApplyConfig();
    }
}

void SpeexProcessor::Process(float* audioData, unsigned int frameCount, unsigned int channels)
{
    if (!m_isInitialized || !m_state || !audioData || frameCount == 0 || channels == 0)
        return;

    // Ensure mono buffer is large enough
    if (frameCount > m_monoBuffer.size())
    {
        m_monoBuffer.resize(frameCount);
    }

    // Step 1: Convert input to mono
    if (channels == 1)
    {
        // Already mono, copy to working buffer
        std::memcpy(m_monoBuffer.data(), audioData, frameCount * sizeof(float));
    }
    else if (channels == 2)
    {
        // Convert stereo to mono by averaging channels
        for (unsigned int i = 0; i < frameCount; i++)
        {
            m_monoBuffer[i] = (audioData[i * 2] + audioData[i * 2 + 1]) * 0.5f;
        }
    }
    else
    {
        // Unsupported channel count - take first channel only
        for (unsigned int i = 0; i < frameCount; i++)
        {
            m_monoBuffer[i] = audioData[i * channels];
        }
    }

    // Step 2: Process using frame accumulation
    unsigned int inputPos = 0;
    unsigned int outputPos = 0;

    while (outputPos < frameCount)
    {
        // First, try to output any previously processed samples
        if (m_outputBufferAvailable > 0)
        {
            unsigned int samplesToOutput = std::min(m_outputBufferAvailable, frameCount - outputPos);

            // Copy from output buffer
            for (unsigned int i = 0; i < samplesToOutput; i++)
            {
                // Convert mono to multi-channel
                float sample = m_outputBuffer[m_outputBufferReadPos + i];

                if (channels == 1)
                {
                    audioData[outputPos] = sample;
                }
                else if (channels == 2)
                {
                    audioData[outputPos * 2] = sample;
                    audioData[outputPos * 2 + 1] = sample;
                }
                else
                {
                    for (unsigned int ch = 0; ch < channels; ch++)
                    {
                        audioData[outputPos * channels + ch] = sample;
                    }
                }
                outputPos++;
            }

            m_outputBufferReadPos += samplesToOutput;
            m_outputBufferAvailable -= samplesToOutput;

            if (m_outputBufferAvailable == 0)
            {
                m_outputBufferReadPos = 0;
            }
        }
        else
        {
            // No processed samples available, need to process more input
            if (inputPos >= frameCount)
            {
                break;
            }

            // Accumulate samples into frame buffer until we have enough
            unsigned int samplesToAccumulate = std::min(
                m_frameSize - m_accumulatedSamples,
                frameCount - inputPos
            );

            // Convert to int16 and accumulate
            for (unsigned int i = 0; i < samplesToAccumulate; i++)
            {
                float sample = m_monoBuffer[inputPos + i] * 32768.0f;
                if (sample > 32767.0f) sample = 32767.0f;
                if (sample < -32768.0f) sample = -32768.0f;
                m_frameBuffer[m_accumulatedSamples + i] = (short)sample;
            }

            m_accumulatedSamples += samplesToAccumulate;
            inputPos += samplesToAccumulate;

            // If we have a full frame, process it
            if (m_accumulatedSamples == m_frameSize)
            {
                // DIAGNOSTIC: Check input samples before processing
                static bool firstFrame = true;
                if (firstFrame && m_diagnosticCallback)
                {
                    float inputMax = 0;
                    for (unsigned int i = 0; i < m_frameSize; i++)
                    {
                        inputMax = std::max(inputMax, std::abs((float)m_frameBuffer[i]));
                    }

                    std::wostringstream msg;
                    msg << L"Speex Input: max=" << inputMax
                        << L", first 3=[" << m_frameBuffer[0] << L", " << m_frameBuffer[1] << L", " << m_frameBuffer[2] << L"]";
                    m_diagnosticCallback(msg.str());
                }

                // Process the frame with Speex
                // speex_preprocess_run returns VAD result (1 = speech, 0 = noise)
                int vadResult = speex_preprocess_run(m_state, m_frameBuffer.data());

                // Convert back to float and store in output buffer
                if (m_outputBuffer.size() < m_frameSize)
                {
                    m_outputBuffer.resize(m_frameSize);
                }

                for (unsigned int i = 0; i < m_frameSize; i++)
                {
                    m_outputBuffer[i] = m_frameBuffer[i] / 32768.0f;
                }

                // DIAGNOSTIC: Check output
                if (firstFrame && m_diagnosticCallback)
                {
                    float outputMax = 0;
                    for (unsigned int i = 0; i < m_frameSize; i++)
                    {
                        outputMax = std::max(outputMax, std::abs(m_outputBuffer[i]));
                    }

                    std::wostringstream msg;
                    msg << L"Speex Output: max=" << outputMax
                        << L", VAD=" << vadResult
                        << L", first 3=[" << m_outputBuffer[0] << L", " << m_outputBuffer[1] << L", " << m_outputBuffer[2] << L"]";
                    m_diagnosticCallback(msg.str());
                    firstFrame = false;
                }

                m_totalFramesProcessed++;
                m_outputBufferReadPos = 0;
                m_outputBufferAvailable = m_frameSize;

                // Reset accumulation
                m_accumulatedSamples = 0;
            }
        }
    }
}

#endif // HAVE_SPEEX
