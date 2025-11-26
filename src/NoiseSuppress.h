#pragma once

#include "NoiseReductionTypes.h"
#include <memory>

class NoiseSuppress
{
public:
    NoiseSuppress();
    ~NoiseSuppress();

    // Initialize with specific noise reduction type and config
    bool Initialize(const NoiseReductionConfig& config, unsigned int sampleRate, unsigned int channels);

    // Process audio data in-place
    void Process(float* audioData, unsigned int frameCount, unsigned int channels);

    // Get current noise reduction type
    NoiseReductionType GetType() const { return m_config.type; }

    // Get current configuration
    const NoiseReductionConfig& GetConfig() const { return m_config; }

    // Check if initialized
    bool IsInitialized() const { return m_isInitialized; }

    // Set callback for diagnostic messages
    void SetDiagnosticCallback(std::function<void(const std::wstring&)> callback);

    // Get the underlying processor (for advanced configuration)
    INoiseProcessor* GetProcessor() { return m_processor.get(); }

private:
    std::unique_ptr<INoiseProcessor> m_processor;
    NoiseReductionConfig m_config;
    bool m_isInitialized;
    std::function<void(const std::wstring&)> m_diagnosticCallback;
};
