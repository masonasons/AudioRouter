#include "AudioEngine.h"
#include <avrt.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "avrt.lib")

AudioEngine::AudioEngine()
    : m_pInputDevice(nullptr)
    , m_pOutputDevice(nullptr)
    , m_pInputClient(nullptr)
    , m_pOutputClient(nullptr)
    , m_pCaptureClient(nullptr)
    , m_pRenderClient(nullptr)
    , m_noiseSuppressor(nullptr)
    , m_enableNoiseSuppression(false)
    , m_hThread(NULL)
    , m_hStopEvent(NULL)
    , m_isRunning(false)
    , m_pInputFormat(nullptr)
    , m_pOutputFormat(nullptr)
    , m_inputBufferFrameCount(0)
    , m_outputBufferFrameCount(0)
    , m_hInputEvent(NULL)
    , m_hOutputEvent(NULL)
    , m_inputIsFloatFormat(false)
    , m_outputIsFloatFormat(false)
{
    m_noiseSuppressor = new NoiseSuppress();
}

AudioEngine::~AudioEngine()
{
    Stop();
    delete m_noiseSuppressor;
}

bool AudioEngine::Start(const std::wstring& inputDeviceId, const std::wstring& outputDeviceId, bool enableNoiseSuppression)
{
    if (m_isRunning)
        return false;

    m_enableNoiseSuppression = enableNoiseSuppression;

    // Initialize devices
    ReportStatus(L"Initializing input device...");
    if (!InitializeDevice(inputDeviceId, true, &m_pInputDevice, &m_pInputClient))
    {
        ReportStatus(L"ERROR: Failed to initialize input device");
        return false;
    }
    ReportStatus(L"Input device initialized successfully");

    ReportStatus(L"Initializing output device...");
    if (!InitializeDevice(outputDeviceId, false, &m_pOutputDevice, &m_pOutputClient))
    {
        ReportStatus(L"ERROR: Failed to initialize output device");
        Stop();
        return false;
    }
    ReportStatus(L"Output device initialized successfully");

    // Get capture client
    ReportStatus(L"Getting capture client...");
    HRESULT hr = m_pInputClient->GetService(__uuidof(IAudioCaptureClient), (void**)&m_pCaptureClient);
    if (FAILED(hr))
    {
        std::wostringstream msg;
        msg << L"ERROR: Failed to get capture client (HRESULT: 0x" << std::hex << hr << L")";
        ReportStatus(msg.str());
        Stop();
        return false;
    }

    // Get render client
    ReportStatus(L"Getting render client...");
    hr = m_pOutputClient->GetService(__uuidof(IAudioRenderClient), (void**)&m_pRenderClient);
    if (FAILED(hr))
    {
        std::wostringstream msg;
        msg << L"ERROR: Failed to get render client (HRESULT: 0x" << std::hex << hr << L")";
        ReportStatus(msg.str());
        Stop();
        return false;
    }

    // Report audio format diagnostics
    std::wostringstream formatInfo;
    formatInfo << L"Input Format: ";
    formatInfo << (m_inputIsFloatFormat ? L"Float32" : L"PCM16");
    formatInfo << L" | " << m_pInputFormat->nSamplesPerSec << L" Hz";
    formatInfo << L" | " << m_pInputFormat->nChannels << L" ch";
    formatInfo << L" | " << m_pInputFormat->wBitsPerSample << L" bit";
    ReportStatus(formatInfo.str());

    std::wostringstream outputFormatInfo;
    outputFormatInfo << L"Output Format: ";
    outputFormatInfo << (m_outputIsFloatFormat ? L"Float32" : L"PCM16");
    outputFormatInfo << L" | " << m_pOutputFormat->nSamplesPerSec << L" Hz";
    outputFormatInfo << L" | " << m_pOutputFormat->nChannels << L" ch";
    outputFormatInfo << L" | " << m_pOutputFormat->wBitsPerSample << L" bit";
    ReportStatus(outputFormatInfo.str());

    // Check for format mismatches that need conversion
    if (m_pInputFormat->nSamplesPerSec != m_pOutputFormat->nSamplesPerSec)
    {
        std::wostringstream warning;
        warning << L"WARNING: Sample rate mismatch! Input=" << m_pInputFormat->nSamplesPerSec
                << L"Hz, Output=" << m_pOutputFormat->nSamplesPerSec << L"Hz";
        ReportStatus(warning.str());
        ReportStatus(L"Sample rate conversion will be applied (may affect quality)");
    }

    if (m_pInputFormat->nChannels != m_pOutputFormat->nChannels)
    {
        std::wostringstream warning;
        warning << L"WARNING: Channel count mismatch! Input=" << m_pInputFormat->nChannels
                << L"ch, Output=" << m_pOutputFormat->nChannels << L"ch";
        ReportStatus(warning.str());
    }

    // Initialize noise suppression if enabled
    if (m_enableNoiseSuppression)
    {
        // Set up diagnostic callback for NoiseSuppress
        m_noiseSuppressor->SetDiagnosticCallback([this](const std::wstring& msg) {
            ReportStatus(msg);
        });

        // Check if sample rate is compatible with RNNoise (requires 48kHz)
        if (m_pInputFormat->nSamplesPerSec != 48000)
        {
            std::wostringstream warning;
            warning << L"WARNING: RNNoise requires 48kHz, but input audio is "
                    << m_pInputFormat->nSamplesPerSec << L" Hz. Noise suppression may not work correctly!";
            ReportStatus(warning.str());
        }

        if (!m_noiseSuppressor->Initialize())
        {
            ReportStatus(L"ERROR: Failed to initialize RNNoise! Noise suppression will not work.");
            // Continue anyway - audio routing will still work
        }
        else
        {
            ReportStatus(L"RNNoise initialized successfully");
        }
    }
    else
    {
        ReportStatus(L"Noise suppression disabled");
    }

    // Create stop event
    m_hStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    // Pre-fill output buffer with silence to prevent initial underruns
    UINT32 bufferFrameCount = 0;
    m_pOutputClient->GetBufferSize(&bufferFrameCount);
    BYTE* pRenderData = nullptr;
    hr = m_pRenderClient->GetBuffer(bufferFrameCount, &pRenderData);
    if (SUCCEEDED(hr))
    {
        // Fill with silence
        memset(pRenderData, 0, bufferFrameCount * m_pOutputFormat->nBlockAlign);
        m_pRenderClient->ReleaseBuffer(bufferFrameCount, 0);
    }

    // Start audio clients
    m_pInputClient->Start();
    m_pOutputClient->Start();

    // Create audio thread
    m_isRunning = true;
    m_hThread = CreateThread(NULL, 0, AudioThreadProc, this, 0, NULL);

    return true;
}

void AudioEngine::Stop()
{
    if (!m_isRunning)
        return;

    m_isRunning = false;

    // Signal stop event
    if (m_hStopEvent)
    {
        SetEvent(m_hStopEvent);
        WaitForSingleObject(m_hThread, INFINITE);
        CloseHandle(m_hThread);
        CloseHandle(m_hStopEvent);
        m_hThread = NULL;
        m_hStopEvent = NULL;
    }

    // Close event handles
    if (m_hInputEvent)
    {
        CloseHandle(m_hInputEvent);
        m_hInputEvent = NULL;
    }

    if (m_hOutputEvent)
    {
        CloseHandle(m_hOutputEvent);
        m_hOutputEvent = NULL;
    }

    // Stop audio clients
    if (m_pInputClient)
    {
        m_pInputClient->Stop();
        m_pInputClient->Release();
        m_pInputClient = nullptr;
    }

    if (m_pOutputClient)
    {
        m_pOutputClient->Stop();
        m_pOutputClient->Release();
        m_pOutputClient = nullptr;
    }

    // Release clients
    if (m_pCaptureClient)
    {
        m_pCaptureClient->Release();
        m_pCaptureClient = nullptr;
    }

    if (m_pRenderClient)
    {
        m_pRenderClient->Release();
        m_pRenderClient = nullptr;
    }

    // Release devices
    if (m_pInputDevice)
    {
        m_pInputDevice->Release();
        m_pInputDevice = nullptr;
    }

    if (m_pOutputDevice)
    {
        m_pOutputDevice->Release();
        m_pOutputDevice = nullptr;
    }

    // Free wave formats
    if (m_pInputFormat)
    {
        CoTaskMemFree(m_pInputFormat);
        m_pInputFormat = nullptr;
    }

    if (m_pOutputFormat)
    {
        CoTaskMemFree(m_pOutputFormat);
        m_pOutputFormat = nullptr;
    }
}

bool AudioEngine::InitializeDevice(const std::wstring& deviceId, bool isInput, IMMDevice** ppDevice, IAudioClient** ppAudioClient)
{
    // Create device enumerator
    IMMDeviceEnumerator* pEnumerator = nullptr;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        NULL,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        (void**)&pEnumerator
    );

    if (FAILED(hr))
    {
        ReportStatus(L"ERROR: Failed to create device enumerator");
        return false;
    }

    // Get device
    hr = pEnumerator->GetDevice(deviceId.c_str(), ppDevice);
    pEnumerator->Release();

    if (FAILED(hr))
    {
        std::wostringstream msg;
        msg << L"ERROR: Failed to get device (HRESULT: 0x" << std::hex << hr << L")";
        ReportStatus(msg.str());
        return false;
    }

    // Activate audio client
    hr = (*ppDevice)->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)ppAudioClient);
    if (FAILED(hr))
    {
        std::wostringstream msg;
        msg << L"ERROR: Failed to activate audio client (HRESULT: 0x" << std::hex << hr << L")";
        ReportStatus(msg.str());
        return false;
    }

    // Get mix format - use whatever WASAPI provides
    WAVEFORMATEX* pWaveFormat = nullptr;
    hr = (*ppAudioClient)->GetMixFormat(&pWaveFormat);
    if (FAILED(hr))
    {
        std::wostringstream msg;
        msg << L"ERROR: Failed to get mix format (HRESULT: 0x" << std::hex << hr << L")";
        ReportStatus(msg.str());
        return false;
    }

    // Store format for this specific device
    WAVEFORMATEX** ppStoredFormat = isInput ? &m_pInputFormat : &m_pOutputFormat;
    bool* pIsFloatFormat = isInput ? &m_inputIsFloatFormat : &m_outputIsFloatFormat;

    *ppStoredFormat = pWaveFormat;

    // Detect if format is float or PCM for this device
    if (pWaveFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
    {
        *pIsFloatFormat = true;
    }
    else if (pWaveFormat->wFormatTag == WAVE_FORMAT_PCM)
    {
        *pIsFloatFormat = false;
    }
    else if (pWaveFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        // Check the SubFormat GUID for float vs PCM
        WAVEFORMATEXTENSIBLE* pWaveFormatEx = (WAVEFORMATEXTENSIBLE*)pWaveFormat;
        if (pWaveFormatEx->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
        {
            *pIsFloatFormat = true;
        }
        else
        {
            *pIsFloatFormat = false;
        }
    }
    else
    {
        *pIsFloatFormat = false; // Default to PCM
    }

    // Create event for event-driven mode
    HANDLE* pEvent = isInput ? &m_hInputEvent : &m_hOutputEvent;
    *pEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!*pEvent)
    {
        ReportStatus(L"ERROR: Failed to create event handle");
        return false;
    }

    // Initialize audio client with event-driven mode and smaller buffer (10ms for low latency)
    REFERENCE_TIME hnsRequestedDuration = 100000; // 10ms for low latency
    DWORD streamFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;

    hr = (*ppAudioClient)->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        streamFlags,
        hnsRequestedDuration,
        0,
        pWaveFormat,  // Use THIS device's native format
        NULL
    );

    if (FAILED(hr))
    {
        std::wostringstream msg;
        msg << L"ERROR: Failed to initialize audio client (HRESULT: 0x" << std::hex << hr << L")";
        ReportStatus(msg.str());

        // Common error codes
        if (hr == AUDCLNT_E_UNSUPPORTED_FORMAT)
            ReportStatus(L"  Reason: Unsupported format");
        else if (hr == AUDCLNT_E_ALREADY_INITIALIZED)
            ReportStatus(L"  Reason: Already initialized");
        else if (hr == E_INVALIDARG)
            ReportStatus(L"  Reason: Invalid argument");

        CloseHandle(*pEvent);
        *pEvent = NULL;
        return false;
    }

    // Set event handle for event-driven mode
    hr = (*ppAudioClient)->SetEventHandle(*pEvent);
    if (FAILED(hr))
    {
        std::wostringstream msg;
        msg << L"ERROR: Failed to set event handle (HRESULT: 0x" << std::hex << hr << L")";
        ReportStatus(msg.str());
        CloseHandle(*pEvent);
        *pEvent = NULL;
        return false;
    }

    // Get buffer size
    UINT32* pBufferFrameCount = isInput ? &m_inputBufferFrameCount : &m_outputBufferFrameCount;
    (*ppAudioClient)->GetBufferSize(pBufferFrameCount);

    return true;
}

DWORD WINAPI AudioEngine::AudioThreadProc(LPVOID lpParameter)
{
    AudioEngine* pThis = (AudioEngine*)lpParameter;
    pThis->AudioThread();
    return 0;
}

void AudioEngine::AudioThread()
{
    // Set thread priority
    DWORD taskIndex = 0;
    HANDLE hTask = AvSetMmThreadCharacteristics(L"Pro Audio", &taskIndex);

    // Prepare event array for waiting
    HANDLE waitArray[2] = { m_hStopEvent, m_hInputEvent };
    const DWORD numEvents = 2;

    while (m_isRunning)
    {
        // Wait for input data or stop event (event-driven, efficient)
        DWORD waitResult = WaitForMultipleObjects(numEvents, waitArray, FALSE, 1000);

        if (waitResult == WAIT_OBJECT_0) // Stop event
            break;

        if (waitResult != WAIT_OBJECT_0 + 1) // Not input event
            continue;

        // Get captured data
        BYTE* pData = nullptr;
        UINT32 numFramesAvailable = 0;
        DWORD flags = 0;

        HRESULT hr = m_pCaptureClient->GetBuffer(&pData, &numFramesAvailable, &flags, NULL, NULL);

        if (SUCCEEDED(hr) && numFramesAvailable > 0)
        {
            // Calculate how many output frames we'll produce
            double sampleRateRatio = (double)m_pOutputFormat->nSamplesPerSec / (double)m_pInputFormat->nSamplesPerSec;
            UINT32 numOutputFrames = (UINT32)(numFramesAvailable * sampleRateRatio);

            // Check how much space is available in output buffer
            BYTE* pRenderData = nullptr;
            UINT32 numFramesPadding = 0;
            m_pOutputClient->GetCurrentPadding(&numFramesPadding);

            UINT32 numFramesAvailableInOutput = m_outputBufferFrameCount - numFramesPadding;

            // Only write if there's enough space to avoid buffer overflow
            if (numFramesAvailableInOutput >= numOutputFrames)
            {
                hr = m_pRenderClient->GetBuffer(numOutputFrames, &pRenderData);
                if (SUCCEEDED(hr))
                {
                    // Process audio data
                    if (flags & AUDCLNT_BUFFERFLAGS_SILENT || !pData)
                    {
                        // Fill with silence
                        memset(pRenderData, 0, numOutputFrames * m_pOutputFormat->nBlockAlign);
                    }
                    else
                    {
                        // Step 1: Convert input to normalized float (interleaved)
                        unsigned int inputSamples = numFramesAvailable * m_pInputFormat->nChannels;
                        if (m_conversionBuffer.size() < inputSamples)
                        {
                            m_conversionBuffer.resize(inputSamples);
                        }

                        if (m_inputIsFloatFormat)
                        {
                            std::memcpy(m_conversionBuffer.data(), pData, inputSamples * sizeof(float));
                        }
                        else
                        {
                            int16_t* pInputSamples = (int16_t*)pData;
                            for (unsigned int i = 0; i < inputSamples; i++)
                            {
                                m_conversionBuffer[i] = pInputSamples[i] / 32768.0f;
                            }
                        }

                        // Step 2: Apply noise suppression (if enabled, works on input format)
                        if (m_enableNoiseSuppression)
                        {
                            static bool firstProcess = true;
                            if (firstProcess)
                            {
                                ReportStatus(L"Applying noise suppression...");
                                firstProcess = false;
                            }
                            m_noiseSuppressor->Process(m_conversionBuffer.data(), numFramesAvailable, m_pInputFormat->nChannels);
                        }

                        // Step 3: Convert channels if needed
                        unsigned int outputFrames = numFramesAvailable;
                        float* pProcessedAudio = m_conversionBuffer.data();

                        if (m_pInputFormat->nChannels != m_pOutputFormat->nChannels)
                        {
                            // Need channel conversion - use resample buffer as temp
                            unsigned int convertedSamples = numFramesAvailable * m_pOutputFormat->nChannels;
                            if (m_resampleBuffer.size() < convertedSamples)
                            {
                                m_resampleBuffer.resize(convertedSamples);
                            }

                            if (m_pInputFormat->nChannels == 1 && m_pOutputFormat->nChannels == 2)
                            {
                                // Mono to stereo: duplicate
                                for (unsigned int i = 0; i < numFramesAvailable; i++)
                                {
                                    m_resampleBuffer[i * 2] = m_conversionBuffer[i];
                                    m_resampleBuffer[i * 2 + 1] = m_conversionBuffer[i];
                                }
                            }
                            else if (m_pInputFormat->nChannels == 2 && m_pOutputFormat->nChannels == 1)
                            {
                                // Stereo to mono: average
                                for (unsigned int i = 0; i < numFramesAvailable; i++)
                                {
                                    m_resampleBuffer[i] = (m_conversionBuffer[i * 2] + m_conversionBuffer[i * 2 + 1]) * 0.5f;
                                }
                            }
                            pProcessedAudio = m_resampleBuffer.data();
                        }

                        // Step 4: Convert sample rate if needed
                        if (m_pInputFormat->nSamplesPerSec != m_pOutputFormat->nSamplesPerSec)
                        {
                            // Calculate output frame count based on sample rate ratio
                            double ratio = (double)m_pOutputFormat->nSamplesPerSec / (double)m_pInputFormat->nSamplesPerSec;
                            outputFrames = (unsigned int)(numFramesAvailable * ratio);

                            // Simple linear interpolation resampling
                            unsigned int tempSize = outputFrames * m_pOutputFormat->nChannels;
                            if (m_resampleBuffer.size() < tempSize * 2) // Extra space
                            {
                                m_resampleBuffer.resize(tempSize * 2);
                            }

                            for (unsigned int i = 0; i < outputFrames; i++)
                            {
                                double srcPos = i / ratio;
                                unsigned int srcIndex = (unsigned int)srcPos;
                                double frac = srcPos - srcIndex;

                                if (srcIndex + 1 < numFramesAvailable)
                                {
                                    for (unsigned int ch = 0; ch < m_pOutputFormat->nChannels; ch++)
                                    {
                                        float sample1 = pProcessedAudio[srcIndex * m_pOutputFormat->nChannels + ch];
                                        float sample2 = pProcessedAudio[(srcIndex + 1) * m_pOutputFormat->nChannels + ch];
                                        m_resampleBuffer[i * m_pOutputFormat->nChannels + ch] =
                                            sample1 + (sample2 - sample1) * (float)frac;
                                    }
                                }
                                else
                                {
                                    for (unsigned int ch = 0; ch < m_pOutputFormat->nChannels; ch++)
                                    {
                                        m_resampleBuffer[i * m_pOutputFormat->nChannels + ch] =
                                            pProcessedAudio[srcIndex * m_pOutputFormat->nChannels + ch];
                                    }
                                }
                            }
                            pProcessedAudio = m_resampleBuffer.data();
                        }

                        // Step 5: Convert to output format
                        unsigned int outputSamples = outputFrames * m_pOutputFormat->nChannels;
                        if (m_outputIsFloatFormat)
                        {
                            std::memcpy(pRenderData, pProcessedAudio, outputSamples * sizeof(float));
                        }
                        else
                        {
                            int16_t* pOutputSamples = (int16_t*)pRenderData;
                            for (unsigned int i = 0; i < outputSamples; i++)
                            {
                                float sample = pProcessedAudio[i] * 32768.0f;
                                if (sample > 32767.0f) sample = 32767.0f;
                                if (sample < -32768.0f) sample = -32768.0f;
                                pOutputSamples[i] = (int16_t)sample;
                            }
                        }
                    }

                    m_pRenderClient->ReleaseBuffer(numOutputFrames, 0);
                }
            }
            else
            {
                // Output buffer is full, we need to drop frames to avoid accumulating latency
                // This should rarely happen with proper buffer sizing
            }

            m_pCaptureClient->ReleaseBuffer(numFramesAvailable);
        }
        else if (hr == AUDCLNT_S_BUFFER_EMPTY)
        {
            // No data available yet, continue waiting
            continue;
        }
    }

    // Restore thread characteristics
    if (hTask)
        AvRevertMmThreadCharacteristics(hTask);
}

void AudioEngine::ReportStatus(const std::wstring& status)
{
    if (m_statusCallback)
    {
        m_statusCallback(status);
    }
}
