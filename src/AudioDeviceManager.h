#pragma once

#include <windows.h>
#include <mmdeviceapi.h>
#include <string>
#include <vector>

struct AudioDevice
{
    std::wstring id;
    std::wstring name;
};

class AudioDeviceManager
{
public:
    AudioDeviceManager();
    ~AudioDeviceManager();

    std::vector<AudioDevice> GetInputDevices();
    std::vector<AudioDevice> GetOutputDevices();

private:
    IMMDeviceEnumerator* m_pEnumerator;

    std::vector<AudioDevice> EnumerateDevices(EDataFlow dataFlow);
};
