#pragma once

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <string>
#include <vector>
#include <functional>
#include "NoiseSuppress.h"

class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine();

    bool Start(const std::wstring& inputDeviceId, const std::wstring& outputDeviceId, bool enableNoiseSuppression);
    void Stop();
    bool IsRunning() const { return m_isRunning; }

    // Set callback for status updates
    void SetStatusCallback(std::function<void(const std::wstring&)> callback) { m_statusCallback = callback; }

private:
    bool InitializeDevice(const std::wstring& deviceId, bool isInput, IMMDevice** ppDevice, IAudioClient** ppAudioClient);
    static DWORD WINAPI AudioThreadProc(LPVOID lpParameter);
    void AudioThread();

    IMMDevice* m_pInputDevice;
    IMMDevice* m_pOutputDevice;
    IAudioClient* m_pInputClient;
    IAudioClient* m_pOutputClient;
    IAudioCaptureClient* m_pCaptureClient;
    IAudioRenderClient* m_pRenderClient;

    NoiseSuppress* m_noiseSuppressor;
    bool m_enableNoiseSuppression;

    HANDLE m_hThread;
    HANDLE m_hStopEvent;
    bool m_isRunning;

    WAVEFORMATEX* m_pInputFormat;
    WAVEFORMATEX* m_pOutputFormat;
    UINT32 m_inputBufferFrameCount;
    UINT32 m_outputBufferFrameCount;
    HANDLE m_hInputEvent;
    HANDLE m_hOutputEvent;

    // Audio format tracking for conversion
    bool m_inputIsFloatFormat;
    bool m_outputIsFloatFormat;
    std::vector<float> m_conversionBuffer;
    std::vector<float> m_resampleBuffer;

    // Status callback for reporting diagnostics to GUI
    std::function<void(const std::wstring&)> m_statusCallback;

    // Helper to report status
    void ReportStatus(const std::wstring& status);
};
