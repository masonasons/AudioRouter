#include "NoiseSuppress.h"
#include "RNNoiseProcessor.h"
#include "SpeexProcessor.h"
#include <sstream>

NoiseSuppress::NoiseSuppress()
    : m_isInitialized(false)
{
}

NoiseSuppress::~NoiseSuppress()
{
    // unique_ptr handles cleanup automatically
}

bool NoiseSuppress::Initialize(const NoiseReductionConfig& config, unsigned int sampleRate, unsigned int channels)
{
    m_config = config;

    // If noise reduction is off, no processor needed
    if (config.type == NoiseReductionType::Off)
    {
        m_processor.reset();
        m_isInitialized = true;
        if (m_diagnosticCallback)
        {
            m_diagnosticCallback(L"Noise reduction disabled");
        }
        return true;
    }

    // Create appropriate processor based on type
    switch (config.type)
    {
        case NoiseReductionType::RNNoise:
        {
            if (m_diagnosticCallback)
            {
                m_diagnosticCallback(L"Initializing RNNoise processor...");
            }
            m_processor = std::make_unique<RNNoiseProcessor>(config.rnnoise);
            break;
        }

        case NoiseReductionType::Speex:
        {
            if (m_diagnosticCallback)
            {
                m_diagnosticCallback(L"Initializing Speex processor...");
            }
            m_processor = std::make_unique<SpeexProcessor>(config.speex);
            break;
        }

        default:
            if (m_diagnosticCallback)
            {
                m_diagnosticCallback(L"ERROR: Unknown noise reduction type!");
            }
            return false;
    }

    // Set diagnostic callback on processor
    if (m_processor && m_diagnosticCallback)
    {
        m_processor->SetDiagnosticCallback(m_diagnosticCallback);
    }

    // Check sample rate requirements
    unsigned int requiredRate = m_processor->GetRequiredSampleRate();
    if (requiredRate > 0 && sampleRate != requiredRate)
    {
        if (m_diagnosticCallback)
        {
            std::wostringstream msg;
            msg << L"WARNING: " << m_processor->GetName() << L" requires " << requiredRate
                << L" Hz, but input is " << sampleRate << L" Hz";
            m_diagnosticCallback(msg.str());
        }
    }

    // Initialize the processor
    if (!m_processor->Initialize(sampleRate, channels))
    {
        if (m_diagnosticCallback)
        {
            std::wostringstream msg;
            msg << L"ERROR: Failed to initialize " << m_processor->GetName();
            m_diagnosticCallback(msg.str());
        }
        return false;
    }

    m_isInitialized = true;

    if (m_diagnosticCallback)
    {
        std::wostringstream msg;
        msg << m_processor->GetName() << L" initialized successfully";
        m_diagnosticCallback(msg.str());
    }

    return true;
}

void NoiseSuppress::Process(float* audioData, unsigned int frameCount, unsigned int channels)
{
    if (!m_isInitialized || !m_processor || m_config.type == NoiseReductionType::Off)
        return;

    m_processor->Process(audioData, frameCount, channels);
}

void NoiseSuppress::SetDiagnosticCallback(std::function<void(const std::wstring&)> callback)
{
    m_diagnosticCallback = callback;

    // Also set on processor if already created
    if (m_processor)
    {
        m_processor->SetDiagnosticCallback(callback);
    }
}
