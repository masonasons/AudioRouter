#include "RNNoiseProcessor.h"

#ifdef HAVE_RNNOISE
#include "rnnoise.h"
#endif

#include <cstring>
#include <algorithm>
#include <cmath>
#include <sstream>

#ifndef HAVE_RNNOISE
// Stub implementation when RNNoise is not available
RNNoiseProcessor::RNNoiseProcessor(const RNNoiseConfig& config)
    : m_state(nullptr), m_isInitialized(false), m_config(config), m_totalFramesProcessed(0) {}
RNNoiseProcessor::~RNNoiseProcessor() {}
bool RNNoiseProcessor::Initialize(unsigned int, unsigned int) {
    if (m_diagnosticCallback) m_diagnosticCallback(L"RNNoise not available (not compiled in)");
    return false;
}
void RNNoiseProcessor::Process(float*, unsigned int, unsigned int) {}
void RNNoiseProcessor::UpdateConfig(const RNNoiseConfig& config) { m_config = config; }
#else

RNNoiseProcessor::RNNoiseProcessor(const RNNoiseConfig& config)
    : m_state(nullptr)
    , m_isInitialized(false)
    , m_config(config)
    , m_inputSampleRate(0)
    , m_inputChannels(0)
    , m_accumulatedSamples(0)
    , m_outputBufferReadPos(0)
    , m_outputBufferAvailable(0)
    , m_lastVadProbability(0.0f)
    , m_vadGraceSamplesRemaining(0.0f)
    , m_totalFramesProcessed(0)
{
}

void RNNoiseProcessor::UpdateConfig(const RNNoiseConfig& config)
{
    m_config = config;
}

RNNoiseProcessor::~RNNoiseProcessor()
{
    if (m_state)
    {
        rnnoise_destroy(m_state);
    }
}

bool RNNoiseProcessor::Initialize(unsigned int sampleRate, unsigned int channels)
{
    if (m_isInitialized)
        return (m_state != nullptr);  // Return success if already initialized with valid state

    m_inputSampleRate = sampleRate;
    m_inputChannels = channels;

    // DIAGNOSTIC: Check RNNoise frame size
    int frameSize = rnnoise_get_frame_size();
    if (m_diagnosticCallback)
    {
        std::wostringstream msg;
        msg << L"RNNoise frame size: " << frameSize;
        m_diagnosticCallback(msg.str());
    }

    // Create RNNoise state (NULL = use default model)
    m_state = rnnoise_create(nullptr);

    m_isInitialized = true;  // Mark as initialized (attempted) to prevent repeated attempts

    if (!m_state)
    {
        // Failed to create RNNoise state - this is a critical error
        if (m_diagnosticCallback)
        {
            m_diagnosticCallback(L"ERROR: rnnoise_create() returned NULL!");
        }
        return false;
    }

    // DIAGNOSTIC: State pointer
    if (m_diagnosticCallback)
    {
        std::wostringstream msg;
        msg << L"RNNoise state created at: 0x" << std::hex << (uintptr_t)m_state;
        m_diagnosticCallback(msg.str());
    }

    // Pre-allocate buffers
    // RNNoise processes 480-sample frames at 48kHz
    m_frameBuffer.resize(480);              // Exactly one RNNoise frame
    m_monoBuffer.resize(4800);              // Buffer for mono conversion
    m_processedBuffer.resize(480);          // Single processed frame
    m_outputBuffer.resize(4800);            // Output buffer to hold processed samples

    // Reset accumulation state
    m_accumulatedSamples = 0;
    m_outputBufferReadPos = 0;
    m_outputBufferAvailable = 0;
    m_totalFramesProcessed = 0;

    return true;  // Success
}

void RNNoiseProcessor::Process(float* audioData, unsigned int frameCount, unsigned int channels)
{
    if (!m_isInitialized || !m_state || !audioData || frameCount == 0 || channels == 0)
        return;

    const unsigned int RNNOISE_FRAME_SIZE = 480;

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
    // This ensures RNNoise always gets exactly 480 samples to process
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
                m_outputBufferReadPos = 0; // Reset read position
            }
        }
        else
        {
            // No processed samples available, need to process more input
            if (inputPos >= frameCount)
            {
                // No more input samples - shouldn't happen, but handle gracefully
                break;
            }

            // Accumulate samples into frame buffer until we have 480
            unsigned int samplesToAccumulate = std::min(
                RNNOISE_FRAME_SIZE - m_accumulatedSamples,
                frameCount - inputPos
            );

            // Copy to accumulation buffer
            std::memcpy(
                &m_frameBuffer[m_accumulatedSamples],
                &m_monoBuffer[inputPos],
                samplesToAccumulate * sizeof(float)
            );

            m_accumulatedSamples += samplesToAccumulate;
            inputPos += samplesToAccumulate;

            // If we have a full frame, process it
            if (m_accumulatedSamples == RNNOISE_FRAME_SIZE)
            {
                // DIAGNOSTIC: Check input samples before processing
                static bool firstFrame = true;
                if (firstFrame && m_diagnosticCallback)
                {
                    float inputSum = 0;
                    float inputMax = 0;
                    for (unsigned int i = 0; i < RNNOISE_FRAME_SIZE; i++)
                    {
                        inputSum += std::abs(m_frameBuffer[i]);
                        inputMax = std::max(inputMax, std::abs(m_frameBuffer[i]));
                    }

                    std::wostringstream msg;
                    msg << L"RNNoise Input (normalized): avg=" << (inputSum / RNNOISE_FRAME_SIZE)
                        << L", max=" << inputMax
                        << L", first 3=[" << m_frameBuffer[0] << L", " << m_frameBuffer[1] << L", " << m_frameBuffer[2] << L"]";
                    m_diagnosticCallback(msg.str());
                }

                // RNNoise expects float samples in int16 range (-32768 to 32767), not normalized (-1.0 to 1.0)
                // Scale input from normalized float to int16 range
                for (unsigned int i = 0; i < RNNOISE_FRAME_SIZE; i++)
                {
                    m_frameBuffer[i] *= 32768.0f;
                }

                // Process the frame with RNNoise
                float vad_prob = rnnoise_process_frame(
                    m_state,
                    m_processedBuffer.data(),
                    m_frameBuffer.data()
                );

                // Scale output back from int16 range to normalized float
                for (unsigned int i = 0; i < RNNOISE_FRAME_SIZE; i++)
                {
                    m_processedBuffer[i] /= 32768.0f;
                }

                m_lastVadProbability = vad_prob;

                // Apply VAD gating if enabled (vadThreshold > 0)
                if (m_config.vadThreshold > 0.0f)
                {
                    bool isSpeech = (vad_prob >= m_config.vadThreshold);

                    if (isSpeech)
                    {
                        // Reset grace period when speech detected
                        m_vadGraceSamplesRemaining = (m_config.vadGracePeriodMs / 1000.0f) * m_inputSampleRate;
                    }
                    else if (m_vadGraceSamplesRemaining > 0)
                    {
                        // In grace period after speech
                        m_vadGraceSamplesRemaining -= RNNOISE_FRAME_SIZE;
                        isSpeech = true;  // Treat as speech during grace period
                    }

                    if (!isSpeech)
                    {
                        // Apply attenuation when not speech
                        for (unsigned int i = 0; i < RNNOISE_FRAME_SIZE; i++)
                        {
                            m_processedBuffer[i] *= m_config.attenuationFactor;
                        }
                    }
                }

                // DIAGNOSTIC: Check output and voice activity
                if (firstFrame && m_diagnosticCallback)
                {
                    float outputSum = 0;
                    float outputMax = 0;
                    for (unsigned int i = 0; i < RNNOISE_FRAME_SIZE; i++)
                    {
                        outputSum += std::abs(m_processedBuffer[i]);
                        outputMax = std::max(outputMax, std::abs(m_processedBuffer[i]));
                    }

                    std::wostringstream msg;
                    msg << L"RNNoise Output (normalized): avg=" << (outputSum / RNNOISE_FRAME_SIZE)
                        << L", max=" << outputMax
                        << L", VAD=" << vad_prob
                        << L", first 3=[" << m_processedBuffer[0] << L", " << m_processedBuffer[1] << L", " << m_processedBuffer[2] << L"]";
                    m_diagnosticCallback(msg.str());
                    firstFrame = false;
                }

                m_totalFramesProcessed++;

                // Move processed frame to output buffer
                std::memcpy(m_outputBuffer.data(), m_processedBuffer.data(), RNNOISE_FRAME_SIZE * sizeof(float));
                m_outputBufferReadPos = 0;
                m_outputBufferAvailable = RNNOISE_FRAME_SIZE;

                // Reset accumulation
                m_accumulatedSamples = 0;
            }
        }
    }
}

#endif // HAVE_RNNOISE
