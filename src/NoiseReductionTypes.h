#pragma once

#include <string>
#include <functional>

// Noise reduction algorithm types
enum class NoiseReductionType
{
    Off = 0,
    RNNoise = 1,
    Speex = 2
};

// Configuration for Speex noise suppression
struct SpeexConfig
{
    int noiseSuppressionLevel = -25;  // dB, range: -1 to -50 (typical: -15 to -35)
    bool enableVAD = false;           // Voice Activity Detection
    bool enableAGC = false;           // Automatic Gain Control
    bool enableDereverb = false;      // Dereverberation
    int agcLevel = 8000;              // AGC target level (1-32767)

    SpeexConfig() = default;
    SpeexConfig(int suppressionLevel) : noiseSuppressionLevel(suppressionLevel) {}
};

// Configuration for RNNoise
struct RNNoiseConfig
{
    float vadThreshold = 0.0f;            // VAD threshold (0.0-1.0). Below this, audio is attenuated. 0 = disabled
    float vadGracePeriodMs = 200.0f;      // Grace period after speech ends before attenuation kicks in
    float attenuationFactor = 0.0f;       // How much to attenuate when VAD below threshold (0.0 = mute, 1.0 = pass through)

    RNNoiseConfig() = default;
};

// Combined noise reduction configuration
struct NoiseReductionConfig
{
    NoiseReductionType type = NoiseReductionType::Off;
    SpeexConfig speex;
    RNNoiseConfig rnnoise;

    NoiseReductionConfig() = default;
    NoiseReductionConfig(NoiseReductionType t) : type(t) {}

    bool isEnabled() const { return type != NoiseReductionType::Off; }

    static const wchar_t* getTypeName(NoiseReductionType type)
    {
        switch (type)
        {
            case NoiseReductionType::Off: return L"Off";
            case NoiseReductionType::RNNoise: return L"RNNoise";
            case NoiseReductionType::Speex: return L"Speex";
            default: return L"Unknown";
        }
    }
};

// Abstract interface for noise processors
class INoiseProcessor
{
public:
    virtual ~INoiseProcessor() = default;

    // Initialize the processor. Returns true on success.
    virtual bool Initialize(unsigned int sampleRate, unsigned int channels) = 0;

    // Process audio data in-place. Audio is in normalized float format (-1.0 to 1.0).
    // audioData: interleaved audio samples
    // frameCount: number of frames (not samples)
    // channels: number of channels in the audio data
    virtual void Process(float* audioData, unsigned int frameCount, unsigned int channels) = 0;

    // Get the name of this processor for display purposes
    virtual const wchar_t* GetName() const = 0;

    // Get the required frame size for this processor (0 = any size)
    virtual unsigned int GetRequiredFrameSize() const { return 0; }

    // Get the required sample rate (0 = any rate)
    virtual unsigned int GetRequiredSampleRate() const { return 0; }

    // Set callback for diagnostic messages
    virtual void SetDiagnosticCallback(std::function<void(const std::wstring&)> callback) = 0;
};
