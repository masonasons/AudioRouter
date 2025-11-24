#include "AudioDeviceManager.h"
#include <functiondiscoverykeys_devpkey.h>

AudioDeviceManager::AudioDeviceManager()
    : m_pEnumerator(nullptr)
{
    // Create device enumerator
    CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        NULL,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        (void**)&m_pEnumerator
    );
}

AudioDeviceManager::~AudioDeviceManager()
{
    if (m_pEnumerator)
    {
        m_pEnumerator->Release();
    }
}

std::vector<AudioDevice> AudioDeviceManager::GetInputDevices()
{
    return EnumerateDevices(eCapture);
}

std::vector<AudioDevice> AudioDeviceManager::GetOutputDevices()
{
    return EnumerateDevices(eRender);
}

std::vector<AudioDevice> AudioDeviceManager::EnumerateDevices(EDataFlow dataFlow)
{
    std::vector<AudioDevice> devices;

    if (!m_pEnumerator)
        return devices;

    IMMDeviceCollection* pCollection = nullptr;
    HRESULT hr = m_pEnumerator->EnumAudioEndpoints(dataFlow, DEVICE_STATE_ACTIVE, &pCollection);

    if (FAILED(hr))
        return devices;

    UINT count = 0;
    pCollection->GetCount(&count);

    for (UINT i = 0; i < count; i++)
    {
        IMMDevice* pDevice = nullptr;
        hr = pCollection->Item(i, &pDevice);

        if (SUCCEEDED(hr))
        {
            AudioDevice device;

            // Get device ID
            LPWSTR pwszID = nullptr;
            hr = pDevice->GetId(&pwszID);
            if (SUCCEEDED(hr))
            {
                device.id = pwszID;
                CoTaskMemFree(pwszID);
            }

            // Get device friendly name
            IPropertyStore* pProps = nullptr;
            hr = pDevice->OpenPropertyStore(STGM_READ, &pProps);
            if (SUCCEEDED(hr))
            {
                PROPVARIANT varName;
                PropVariantInit(&varName);

                hr = pProps->GetValue(PKEY_Device_FriendlyName, &varName);
                if (SUCCEEDED(hr))
                {
                    device.name = varName.pwszVal;
                    PropVariantClear(&varName);
                }

                pProps->Release();
            }

            devices.push_back(device);
            pDevice->Release();
        }
    }

    pCollection->Release();
    return devices;
}
