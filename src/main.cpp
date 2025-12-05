#include <windows.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <vector>
#include <algorithm>
#include "AudioDeviceManager.h"
#include "AudioEngine.h"
#include "NoiseReductionTypes.h"

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")

// Control IDs
#define IDC_INPUT_COMBO      1001
#define IDC_OUTPUT_COMBO     1002
#define IDC_NOISE_COMBO      1003
#define IDC_START_BUTTON     1004
#define IDC_STATUS_TEXT      1005
#define IDC_SAVE_BUTTON      1006
#define IDC_DIAG_TEXT        1007
#define IDC_SPEEX_LEVEL_LABEL    1008
#define IDC_SPEEX_LEVEL_SLIDER   1009
#define IDC_SPEEX_LEVEL_VALUE    1010
#define IDC_SPEEX_VAD_CHECK      1011
#define IDC_SPEEX_AGC_CHECK      1012
#define IDC_SPEEX_DEREVERB_CHECK 1013

// RNNoise config controls
#define IDC_RNNOISE_VAD_LABEL     1014
#define IDC_RNNOISE_VAD_SLIDER    1015
#define IDC_RNNOISE_VAD_VALUE     1016
#define IDC_RNNOISE_GRACE_LABEL   1017
#define IDC_RNNOISE_GRACE_SLIDER  1018
#define IDC_RNNOISE_GRACE_VALUE   1019

// System tray and custom messages
#define WM_APPENDDIAG        (WM_USER + 2)
#define WM_TRAYICON          (WM_USER + 1)
#define ID_TRAY_RESTORE      2001
#define ID_TRAY_EXIT         2002
#define TRAY_ICON_ID         1

// Global variables
HWND g_hWnd = NULL;
HWND g_hInputCombo = NULL;
HWND g_hOutputCombo = NULL;
HWND g_hNoiseCombo = NULL;
HWND g_hStartButton = NULL;
HWND g_hStatusText = NULL;
HWND g_hDiagText = NULL;

// Speex configuration controls
HWND g_hSpeexLevelLabel = NULL;
HWND g_hSpeexLevelSlider = NULL;
HWND g_hSpeexLevelValue = NULL;
HWND g_hSpeexVadCheck = NULL;
HWND g_hSpeexAgcCheck = NULL;
HWND g_hSpeexDereverbCheck = NULL;

// RNNoise configuration controls
HWND g_hRnnoiseVadLabel = NULL;
HWND g_hRnnoiseVadSlider = NULL;
HWND g_hRnnoiseVadValue = NULL;
HWND g_hRnnoiseGraceLabel = NULL;
HWND g_hRnnoiseGraceSlider = NULL;
HWND g_hRnnoiseGraceValue = NULL;

AudioDeviceManager* g_deviceManager = nullptr;
AudioEngine* g_audioEngine = nullptr;
bool g_isRunning = false;

// Current noise reduction config (for Speex settings persistence)
NoiseReductionConfig g_noiseConfig;

NOTIFYICONDATA g_nid = {};
bool g_isInTray = false;

// Command line parameters
struct CommandLineParams
{
    std::wstring inputDevice;
    std::wstring outputDevice;
    NoiseReductionType noiseType = NoiseReductionType::Off;
    int speexLevel = -25;  // dB
    bool speexVad = false;
    bool speexAgc = false;
    bool speexDereverb = false;
    int rnnoiseVadThreshold = 0;  // 0-100 (0 = disabled)
    int rnnoiseGracePeriod = 200; // ms (0-1000)
    bool autoStart = false;
    bool autoHide = false;
};

// Forward declarations
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
void InitializeControls(HWND hWnd);
void PopulateDeviceLists();
void OnStartStop();
void UpdateStatus(const wchar_t* status);
void UpdateDiagnostics(const std::wstring& text);
void AppendDiagnostics(const std::wstring& text);
void AppendDiagnosticsImpl(const std::wstring& text);
void ParseCommandLine(CommandLineParams& params);
void ApplyCommandLineParams(const CommandLineParams& params);
void SaveSettingsToBatchFile();
void AddTrayIcon();
void RemoveTrayIcon();
void UpdateTrayTooltip();
void RestoreFromTray();
void MinimizeToTray();
void ShowTrayContextMenu();
void UpdateSpeexControlsVisibility();
void UpdateRnnoiseControlsVisibility();
void UpdateRnnoiseVadDisplay();
void UpdateRnnoiseGraceDisplay();
void UpdateSpeexLevelDisplay();
NoiseReductionConfig GetNoiseConfigFromUI();

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // Initialize COM
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    // Initialize common controls (for trackbar/slider)
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&icex);

    // Register window class
    WNDCLASSEX wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = L"AudioRouterClass";
    RegisterClassEx(&wcex);

    // Create main window (increased height for diagnostics and Speex config)
    g_hWnd = CreateWindowEx(
        0,
        L"AudioRouterClass",
        L"Audio Router",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 450, 520,
        NULL, NULL, hInstance, NULL
    );

    if (!g_hWnd)
    {
        CoUninitialize();
        return FALSE;
    }

    // Initialize controls
    InitializeControls(g_hWnd);

    // Create device manager and audio engine
    g_deviceManager = new AudioDeviceManager();
    g_audioEngine = new AudioEngine();

    // Populate device lists
    PopulateDeviceLists();

    // Parse and apply command line parameters
    CommandLineParams cmdParams;
    ParseCommandLine(cmdParams);
    ApplyCommandLineParams(cmdParams);

    // Show window or minimize to tray based on autohide flag
    if (cmdParams.autoHide)
    {
        // Don't show the window, minimize directly to tray
        MinimizeToTray();
    }
    else
    {
        // Show window normally
        ShowWindow(g_hWnd, nCmdShow);
        UpdateWindow(g_hWnd);
    }

    // Auto-start if requested
    if (cmdParams.autoStart)
    {
        OnStartStop();
    }

    // Message loop with dialog message handling for keyboard navigation
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        // Handle Ctrl+S before IsDialogMessage to ensure it works
        if (msg.message == WM_KEYDOWN && msg.wParam == 'S' && (GetKeyState(VK_CONTROL) & 0x8000))
        {
            OnStartStop();
            continue;
        }

        if (!IsDialogMessage(g_hWnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    // Cleanup
    if (g_isRunning)
    {
        g_audioEngine->Stop();
    }
    delete g_audioEngine;
    delete g_deviceManager;

    CoUninitialize();
    return (int)msg.wParam;
}

void InitializeControls(HWND hWnd)
{
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    int yPos = 10;

    // Input device label
    HWND hLabel = CreateWindow(L"STATIC", L"Input Device:",
        WS_VISIBLE | WS_CHILD,
        10, yPos, 120, 20, hWnd, NULL, NULL, NULL);
    SendMessage(hLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
    yPos += 25;

    // Input device combo box
    g_hInputCombo = CreateWindow(L"COMBOBOX", NULL,
        WS_VISIBLE | WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        10, yPos, 360, 200, hWnd, (HMENU)IDC_INPUT_COMBO, NULL, NULL);
    SendMessage(g_hInputCombo, WM_SETFONT, (WPARAM)hFont, TRUE);
    yPos += 35;

    // Output device label
    hLabel = CreateWindow(L"STATIC", L"Output Device:",
        WS_VISIBLE | WS_CHILD,
        10, yPos, 120, 20, hWnd, NULL, NULL, NULL);
    SendMessage(hLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
    yPos += 25;

    // Output device combo box
    g_hOutputCombo = CreateWindow(L"COMBOBOX", NULL,
        WS_VISIBLE | WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        10, yPos, 360, 200, hWnd, (HMENU)IDC_OUTPUT_COMBO, NULL, NULL);
    SendMessage(g_hOutputCombo, WM_SETFONT, (WPARAM)hFont, TRUE);
    yPos += 35;

    // Noise suppression label
    hLabel = CreateWindow(L"STATIC", L"Noise Reduction:",
        WS_VISIBLE | WS_CHILD,
        10, yPos, 120, 20, hWnd, NULL, NULL, NULL);
    SendMessage(hLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Noise suppression combo box (Off/RNNoise/Speex)
    g_hNoiseCombo = CreateWindow(L"COMBOBOX", NULL,
        WS_VISIBLE | WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        130, yPos - 3, 150, 100, hWnd, (HMENU)IDC_NOISE_COMBO, NULL, NULL);
    SendMessage(g_hNoiseCombo, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(g_hNoiseCombo, CB_ADDSTRING, 0, (LPARAM)L"Off");
    SendMessage(g_hNoiseCombo, CB_ADDSTRING, 0, (LPARAM)L"RNNoise");
    SendMessage(g_hNoiseCombo, CB_ADDSTRING, 0, (LPARAM)L"Speex");
    SendMessage(g_hNoiseCombo, CB_SETCURSEL, 0, 0);  // Default to Off
    yPos += 30;

    // Speex configuration controls (initially hidden)
    // Suppression level label
    g_hSpeexLevelLabel = CreateWindow(L"STATIC", L"  Suppression Level:",
        WS_CHILD,  // Not visible initially
        10, yPos, 130, 20, hWnd, (HMENU)IDC_SPEEX_LEVEL_LABEL, NULL, NULL);
    SendMessage(g_hSpeexLevelLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Suppression level slider (trackbar)
    g_hSpeexLevelSlider = CreateWindowEx(
        0, TRACKBAR_CLASS, NULL,
        WS_CHILD | WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS,
        140, yPos - 3, 180, 25, hWnd, (HMENU)IDC_SPEEX_LEVEL_SLIDER, NULL, NULL);
    SendMessage(g_hSpeexLevelSlider, TBM_SETRANGE, TRUE, MAKELPARAM(1, 50));  // Range: -1 to -50 dB (stored as positive)
    SendMessage(g_hSpeexLevelSlider, TBM_SETPOS, TRUE, 25);  // Default: -25 dB
    SendMessage(g_hSpeexLevelSlider, TBM_SETTICFREQ, 5, 0);

    // Level value display
    g_hSpeexLevelValue = CreateWindow(L"STATIC", L"-25 dB",
        WS_CHILD,  // Not visible initially
        325, yPos, 50, 20, hWnd, (HMENU)IDC_SPEEX_LEVEL_VALUE, NULL, NULL);
    SendMessage(g_hSpeexLevelValue, WM_SETFONT, (WPARAM)hFont, TRUE);
    yPos += 28;

    // Speex options checkboxes
    g_hSpeexVadCheck = CreateWindow(L"BUTTON", L"  VAD (Voice Activity Detection)",
        WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
        10, yPos, 220, 20, hWnd, (HMENU)IDC_SPEEX_VAD_CHECK, NULL, NULL);
    SendMessage(g_hSpeexVadCheck, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_hSpeexAgcCheck = CreateWindow(L"BUTTON", L"AGC",
        WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
        235, yPos, 55, 20, hWnd, (HMENU)IDC_SPEEX_AGC_CHECK, NULL, NULL);
    SendMessage(g_hSpeexAgcCheck, WM_SETFONT, (WPARAM)hFont, TRUE);

    g_hSpeexDereverbCheck = CreateWindow(L"BUTTON", L"Dereverb",
        WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
        295, yPos, 80, 20, hWnd, (HMENU)IDC_SPEEX_DEREVERB_CHECK, NULL, NULL);
    SendMessage(g_hSpeexDereverbCheck, WM_SETFONT, (WPARAM)hFont, TRUE);
    yPos += 30;

    // RNNoise configuration controls (initially hidden)
    // VAD threshold label
    g_hRnnoiseVadLabel = CreateWindow(L"STATIC", L"  VAD Threshold:",
        WS_CHILD,  // Not visible initially
        10, yPos, 120, 20, hWnd, (HMENU)IDC_RNNOISE_VAD_LABEL, NULL, NULL);
    SendMessage(g_hRnnoiseVadLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

    // VAD threshold slider (trackbar) - 0-100 represents 0.0-1.0
    g_hRnnoiseVadSlider = CreateWindowEx(
        0, TRACKBAR_CLASS, NULL,
        WS_CHILD | WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS,
        130, yPos - 3, 180, 25, hWnd, (HMENU)IDC_RNNOISE_VAD_SLIDER, NULL, NULL);
    SendMessage(g_hRnnoiseVadSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessage(g_hRnnoiseVadSlider, TBM_SETPOS, TRUE, 0);  // Default: 0 (disabled)
    SendMessage(g_hRnnoiseVadSlider, TBM_SETTICFREQ, 10, 0);

    // VAD value display
    g_hRnnoiseVadValue = CreateWindow(L"STATIC", L"Off",
        WS_CHILD,  // Not visible initially
        315, yPos, 60, 20, hWnd, (HMENU)IDC_RNNOISE_VAD_VALUE, NULL, NULL);
    SendMessage(g_hRnnoiseVadValue, WM_SETFONT, (WPARAM)hFont, TRUE);
    yPos += 25;

    // Grace period label
    g_hRnnoiseGraceLabel = CreateWindow(L"STATIC", L"  Grace Period:",
        WS_CHILD,  // Not visible initially
        10, yPos, 120, 20, hWnd, (HMENU)IDC_RNNOISE_GRACE_LABEL, NULL, NULL);
    SendMessage(g_hRnnoiseGraceLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Grace period slider (trackbar) - 0-1000 ms
    g_hRnnoiseGraceSlider = CreateWindowEx(
        0, TRACKBAR_CLASS, NULL,
        WS_CHILD | WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS,
        130, yPos - 3, 180, 25, hWnd, (HMENU)IDC_RNNOISE_GRACE_SLIDER, NULL, NULL);
    SendMessage(g_hRnnoiseGraceSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 1000));
    SendMessage(g_hRnnoiseGraceSlider, TBM_SETPOS, TRUE, 200);  // Default: 200ms
    SendMessage(g_hRnnoiseGraceSlider, TBM_SETTICFREQ, 100, 0);

    // Grace period value display
    g_hRnnoiseGraceValue = CreateWindow(L"STATIC", L"200 ms",
        WS_CHILD,  // Not visible initially
        315, yPos, 60, 20, hWnd, (HMENU)IDC_RNNOISE_GRACE_VALUE, NULL, NULL);
    SendMessage(g_hRnnoiseGraceValue, WM_SETFONT, (WPARAM)hFont, TRUE);
    yPos += 30;

    // Start/Stop button
    g_hStartButton = CreateWindow(L"BUTTON", L"Start",
        WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
        10, yPos, 100, 30, hWnd, (HMENU)IDC_START_BUTTON, NULL, NULL);
    SendMessage(g_hStartButton, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Save Settings button
    HWND hSaveButton = CreateWindow(L"BUTTON", L"Save Settings",
        WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
        120, yPos, 120, 30, hWnd, (HMENU)IDC_SAVE_BUTTON, NULL, NULL);
    SendMessage(hSaveButton, WM_SETFONT, (WPARAM)hFont, TRUE);
    yPos += 40;

    // Status text
    g_hStatusText = CreateWindow(L"STATIC", L"Status: Stopped",
        WS_VISIBLE | WS_CHILD,
        10, yPos, 420, 20, hWnd, (HMENU)IDC_STATUS_TEXT, NULL, NULL);
    SendMessage(g_hStatusText, WM_SETFONT, (WPARAM)hFont, TRUE);
    yPos += 25;

    // Diagnostic info text (multi-line, read-only edit control for better formatting)
    g_hDiagText = CreateWindowEx(
        WS_EX_CLIENTEDGE,
        L"EDIT", L"",
        WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
        10, yPos, 420, 120, hWnd, (HMENU)IDC_DIAG_TEXT, NULL, NULL);
    SendMessage(g_hDiagText, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Set initial focus to the first interactive control
    SetFocus(g_hInputCombo);
}

void PopulateDeviceLists()
{
    // Clear combo boxes
    SendMessage(g_hInputCombo, CB_RESETCONTENT, 0, 0);
    SendMessage(g_hOutputCombo, CB_RESETCONTENT, 0, 0);

    // Add default input device as first option
    AudioDevice defaultInput = g_deviceManager->GetDefaultInputDevice();
    int defaultInputIndex = SendMessage(g_hInputCombo, CB_ADDSTRING, 0, (LPARAM)defaultInput.name.c_str());
    SendMessage(g_hInputCombo, CB_SETITEMDATA, defaultInputIndex, (LPARAM)new std::wstring(defaultInput.id));

    // Get input devices
    std::vector<AudioDevice> inputDevices = g_deviceManager->GetInputDevices();
    for (const auto& device : inputDevices)
    {
        int index = SendMessage(g_hInputCombo, CB_ADDSTRING, 0, (LPARAM)device.name.c_str());
        SendMessage(g_hInputCombo, CB_SETITEMDATA, index, (LPARAM)new std::wstring(device.id));
    }

    // Select default (first item) by default
    SendMessage(g_hInputCombo, CB_SETCURSEL, 0, 0);

    // Add default output device as first option
    AudioDevice defaultOutput = g_deviceManager->GetDefaultOutputDevice();
    int defaultOutputIndex = SendMessage(g_hOutputCombo, CB_ADDSTRING, 0, (LPARAM)defaultOutput.name.c_str());
    SendMessage(g_hOutputCombo, CB_SETITEMDATA, defaultOutputIndex, (LPARAM)new std::wstring(defaultOutput.id));

    // Get output devices
    std::vector<AudioDevice> outputDevices = g_deviceManager->GetOutputDevices();
    for (const auto& device : outputDevices)
    {
        int index = SendMessage(g_hOutputCombo, CB_ADDSTRING, 0, (LPARAM)device.name.c_str());
        SendMessage(g_hOutputCombo, CB_SETITEMDATA, index, (LPARAM)new std::wstring(device.id));
    }

    // Select default (first item) by default
    SendMessage(g_hOutputCombo, CB_SETCURSEL, 0, 0);
}

void OnStartStop()
{
    if (!g_isRunning)
    {
        // Clear previous diagnostics
        UpdateDiagnostics(L"");

        // Get selected devices
        int inputIndex = SendMessage(g_hInputCombo, CB_GETCURSEL, 0, 0);
        int outputIndex = SendMessage(g_hOutputCombo, CB_GETCURSEL, 0, 0);

        if (inputIndex == CB_ERR || outputIndex == CB_ERR)
        {
            MessageBox(g_hWnd, L"Please select input and output devices", L"Error", MB_OK | MB_ICONERROR);
            return;
        }

        // Get device IDs
        std::wstring* inputId = (std::wstring*)SendMessage(g_hInputCombo, CB_GETITEMDATA, inputIndex, 0);
        std::wstring* outputId = (std::wstring*)SendMessage(g_hOutputCombo, CB_GETITEMDATA, outputIndex, 0);

        // Get noise reduction configuration from UI
        NoiseReductionConfig noiseConfig = GetNoiseConfigFromUI();

        // Set up status callback to display diagnostics
        g_audioEngine->SetStatusCallback([](const std::wstring& status) {
            AppendDiagnostics(status);
        });

        // Start audio engine
        if (g_audioEngine->Start(*inputId, *outputId, noiseConfig))
        {
            g_isRunning = true;
            SetWindowText(g_hStartButton, L"Stop");
            EnableWindow(g_hInputCombo, FALSE);
            EnableWindow(g_hOutputCombo, FALSE);
            EnableWindow(g_hNoiseCombo, FALSE);
            EnableWindow(g_hSpeexLevelSlider, FALSE);
            EnableWindow(g_hSpeexVadCheck, FALSE);
            EnableWindow(g_hSpeexAgcCheck, FALSE);
            EnableWindow(g_hSpeexDereverbCheck, FALSE);
            UpdateStatus(L"Status: Running");
            UpdateTrayTooltip();
        }
        else
        {
            MessageBox(g_hWnd, L"Failed to start audio routing", L"Error", MB_OK | MB_ICONERROR);
        }
    }
    else
    {
        // Stop audio engine
        g_audioEngine->Stop();
        g_isRunning = false;
        SetWindowText(g_hStartButton, L"Start");
        EnableWindow(g_hInputCombo, TRUE);
        EnableWindow(g_hOutputCombo, TRUE);
        EnableWindow(g_hNoiseCombo, TRUE);
        EnableWindow(g_hSpeexLevelSlider, TRUE);
        EnableWindow(g_hSpeexVadCheck, TRUE);
        EnableWindow(g_hSpeexAgcCheck, TRUE);
        EnableWindow(g_hSpeexDereverbCheck, TRUE);
        UpdateStatus(L"Status: Stopped");
        UpdateTrayTooltip();
    }
}

void UpdateStatus(const wchar_t* status)
{
    SetWindowText(g_hStatusText, status);
}

void UpdateDiagnostics(const std::wstring& text)
{
    SetWindowText(g_hDiagText, text.c_str());
}

void AppendDiagnosticsImpl(const std::wstring& text)
{
    // Get current text
    int len = GetWindowTextLength(g_hDiagText);
    std::vector<wchar_t> buffer(len + 1);
    GetWindowText(g_hDiagText, buffer.data(), len + 1);

    std::wstring current(buffer.data());
    if (!current.empty())
        current += L"\r\n";
    current += text;

    SetWindowText(g_hDiagText, current.c_str());

    // Scroll to bottom
    SendMessage(g_hDiagText, EM_SETSEL, current.length(), current.length());
    SendMessage(g_hDiagText, EM_SCROLLCARET, 0, 0);
}

void AppendDiagnostics(const std::wstring& text)
{
    // Thread-safe: post message to UI thread instead of directly manipulating controls
    // The text is allocated on the heap and will be freed by the message handler
    std::wstring* pText = new std::wstring(text);
    if (!PostMessage(g_hWnd, WM_APPENDDIAG, 0, (LPARAM)pText))
    {
        delete pText;
    }
}

void ParseCommandLine(CommandLineParams& params)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    if (!argv)
        return;

    for (int i = 1; i < argc; i++)
    {
        std::wstring arg = argv[i];

        if ((arg == L"--input" || arg == L"-i") && i + 1 < argc)
        {
            params.inputDevice = argv[++i];
        }
        else if ((arg == L"--output" || arg == L"-o") && i + 1 < argc)
        {
            params.outputDevice = argv[++i];
        }
        else if ((arg == L"--noise-type" || arg == L"-n") && i + 1 < argc)
        {
            std::wstring noiseArg = argv[++i];
            std::transform(noiseArg.begin(), noiseArg.end(), noiseArg.begin(), ::towlower);
            if (noiseArg == L"rnnoise" || noiseArg == L"1")
                params.noiseType = NoiseReductionType::RNNoise;
            else if (noiseArg == L"speex" || noiseArg == L"2")
                params.noiseType = NoiseReductionType::Speex;
            else
                params.noiseType = NoiseReductionType::Off;
        }
        else if (arg == L"--noise" || arg == L"--rnnoise")
        {
            // Legacy compatibility: --noise enables RNNoise
            params.noiseType = NoiseReductionType::RNNoise;
        }
        else if (arg == L"--speex")
        {
            params.noiseType = NoiseReductionType::Speex;
        }
        else if ((arg == L"--speex-level") && i + 1 < argc)
        {
            params.speexLevel = _wtoi(argv[++i]);
            // Ensure valid range
            if (params.speexLevel > -1) params.speexLevel = -1;
            if (params.speexLevel < -50) params.speexLevel = -50;
        }
        else if (arg == L"--speex-vad")
        {
            params.speexVad = true;
        }
        else if (arg == L"--speex-agc")
        {
            params.speexAgc = true;
        }
        else if (arg == L"--speex-dereverb")
        {
            params.speexDereverb = true;
        }
        else if ((arg == L"--rnnoise-vad") && i + 1 < argc)
        {
            params.rnnoiseVadThreshold = _wtoi(argv[++i]);
            // Clamp to valid range
            if (params.rnnoiseVadThreshold < 0) params.rnnoiseVadThreshold = 0;
            if (params.rnnoiseVadThreshold > 100) params.rnnoiseVadThreshold = 100;
        }
        else if ((arg == L"--rnnoise-grace") && i + 1 < argc)
        {
            params.rnnoiseGracePeriod = _wtoi(argv[++i]);
            // Clamp to valid range
            if (params.rnnoiseGracePeriod < 0) params.rnnoiseGracePeriod = 0;
            if (params.rnnoiseGracePeriod > 1000) params.rnnoiseGracePeriod = 1000;
        }
        else if (arg == L"--autostart" || arg == L"-a")
        {
            params.autoStart = true;
        }
        else if (arg == L"--autohide" || arg == L"-h")
        {
            params.autoHide = true;
        }
    }

    LocalFree(argv);
}

void ApplyCommandLineParams(const CommandLineParams& params)
{
    // Apply input device selection
    if (!params.inputDevice.empty())
    {
        int count = SendMessage(g_hInputCombo, CB_GETCOUNT, 0, 0);

        // Check if it's "Default" (case-insensitive) - select index 0
        std::wstring searchLower = params.inputDevice;
        std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::towlower);

        if (searchLower == L"default")
        {
            SendMessage(g_hInputCombo, CB_SETCURSEL, 0, 0);
        }
        // Check if it's a numeric index
        else if (_wtoi(params.inputDevice.c_str()) > 0 || params.inputDevice == L"0")
        {
            int numericIndex = _wtoi(params.inputDevice.c_str());
            if (numericIndex < count)
            {
                SendMessage(g_hInputCombo, CB_SETCURSEL, numericIndex, 0);
            }
        }
        else
        {
            // Search by device name (case-insensitive substring match)
            for (int i = 0; i < count; i++)
            {
                wchar_t deviceName[256] = {0};
                SendMessage(g_hInputCombo, CB_GETLBTEXT, i, (LPARAM)deviceName);

                // Convert both to lowercase for case-insensitive comparison
                std::wstring deviceNameLower = deviceName;
                std::transform(deviceNameLower.begin(), deviceNameLower.end(), deviceNameLower.begin(), ::towlower);

                if (deviceNameLower.find(searchLower) != std::wstring::npos)
                {
                    SendMessage(g_hInputCombo, CB_SETCURSEL, i, 0);
                    break;
                }
            }
        }
    }

    // Apply output device selection
    if (!params.outputDevice.empty())
    {
        int count = SendMessage(g_hOutputCombo, CB_GETCOUNT, 0, 0);

        // Check if it's "Default" (case-insensitive) - select index 0
        std::wstring searchLower = params.outputDevice;
        std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::towlower);

        if (searchLower == L"default")
        {
            SendMessage(g_hOutputCombo, CB_SETCURSEL, 0, 0);
        }
        // Check if it's a numeric index
        else if (_wtoi(params.outputDevice.c_str()) > 0 || params.outputDevice == L"0")
        {
            int numericIndex = _wtoi(params.outputDevice.c_str());
            if (numericIndex < count)
            {
                SendMessage(g_hOutputCombo, CB_SETCURSEL, numericIndex, 0);
            }
        }
        else
        {
            // Search by device name (case-insensitive substring match)
            for (int i = 0; i < count; i++)
            {
                wchar_t deviceName[256] = {0};
                SendMessage(g_hOutputCombo, CB_GETLBTEXT, i, (LPARAM)deviceName);

                // Convert both to lowercase for case-insensitive comparison
                std::wstring deviceNameLower = deviceName;
                std::transform(deviceNameLower.begin(), deviceNameLower.end(), deviceNameLower.begin(), ::towlower);

                if (deviceNameLower.find(searchLower) != std::wstring::npos)
                {
                    SendMessage(g_hOutputCombo, CB_SETCURSEL, i, 0);
                    break;
                }
            }
        }
    }

    // Apply noise reduction type
    int noiseIndex = static_cast<int>(params.noiseType);
    SendMessage(g_hNoiseCombo, CB_SETCURSEL, noiseIndex, 0);

    // Apply Speex settings
    // Convert level to slider position (positive value)
    int sliderPos = -params.speexLevel;
    if (sliderPos < 1) sliderPos = 1;
    if (sliderPos > 50) sliderPos = 50;
    SendMessage(g_hSpeexLevelSlider, TBM_SETPOS, TRUE, sliderPos);

    if (params.speexVad)
        SendMessage(g_hSpeexVadCheck, BM_SETCHECK, BST_CHECKED, 0);
    if (params.speexAgc)
        SendMessage(g_hSpeexAgcCheck, BM_SETCHECK, BST_CHECKED, 0);
    if (params.speexDereverb)
        SendMessage(g_hSpeexDereverbCheck, BM_SETCHECK, BST_CHECKED, 0);

    // Apply RNNoise settings
    SendMessage(g_hRnnoiseVadSlider, TBM_SETPOS, TRUE, params.rnnoiseVadThreshold);
    SendMessage(g_hRnnoiseGraceSlider, TBM_SETPOS, TRUE, params.rnnoiseGracePeriod);

    // Update controls visibility and displays
    UpdateSpeexControlsVisibility();
    UpdateSpeexLevelDisplay();
    UpdateRnnoiseControlsVisibility();
    UpdateRnnoiseVadDisplay();
    UpdateRnnoiseGraceDisplay();
}

void SaveSettingsToBatchFile()
{
    // Get current selections
    int inputIndex = SendMessage(g_hInputCombo, CB_GETCURSEL, 0, 0);
    int outputIndex = SendMessage(g_hOutputCombo, CB_GETCURSEL, 0, 0);
    int noiseIndex = SendMessage(g_hNoiseCombo, CB_GETCURSEL, 0, 0);
    NoiseReductionType noiseType = static_cast<NoiseReductionType>(noiseIndex);

    if (inputIndex == CB_ERR || outputIndex == CB_ERR)
    {
        MessageBox(g_hWnd, L"Please select input and output devices first", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    // Get device IDs and names
    std::wstring* inputId = (std::wstring*)SendMessage(g_hInputCombo, CB_GETITEMDATA, inputIndex, 0);
    std::wstring* outputId = (std::wstring*)SendMessage(g_hOutputCombo, CB_GETITEMDATA, outputIndex, 0);

    // For command line, use "Default" if ID is "DEFAULT", otherwise use device name
    std::wstring inputParam;
    std::wstring outputParam;

    if (*inputId == L"DEFAULT")
    {
        inputParam = L"Default";
    }
    else
    {
        wchar_t inputName[256] = {0};
        SendMessage(g_hInputCombo, CB_GETLBTEXT, inputIndex, (LPARAM)inputName);
        inputParam = inputName;
    }

    if (*outputId == L"DEFAULT")
    {
        outputParam = L"Default";
    }
    else
    {
        wchar_t outputName[256] = {0};
        SendMessage(g_hOutputCombo, CB_GETLBTEXT, outputIndex, (LPARAM)outputName);
        outputParam = outputName;
    }

    // Open file dialog
    OPENFILENAME ofn = {};
    wchar_t fileName[MAX_PATH] = L"AudioRouter.bat";

    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner = g_hWnd;
    ofn.lpstrFilter = L"Batch Files (*.bat)\0*.bat\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"bat";
    ofn.Flags = OFN_OVERWRITEPROMPT;

    if (GetSaveFileName(&ofn))
    {
        // Get executable path and extract directory
        wchar_t exePath[MAX_PATH];
        GetModuleFileName(NULL, exePath, MAX_PATH);

        // Get just the directory (remove filename)
        wchar_t exeDir[MAX_PATH];
        wcscpy_s(exeDir, MAX_PATH, exePath);
        PathRemoveFileSpec(exeDir);

        // Build command line with cd and start
        std::wstring cmdLine = L"@echo off\r\n";
        cmdLine += L"cd /d \"" + std::wstring(exeDir) + L"\"\r\n";
        cmdLine += L"start AudioRouter.exe";
        cmdLine += L" --input \"" + inputParam + L"\"";
        cmdLine += L" --output \"" + outputParam + L"\"";

        // Add noise reduction settings
        if (noiseType == NoiseReductionType::RNNoise)
        {
            cmdLine += L" --rnnoise";

            // Get RNNoise settings
            int vadThreshold = (int)SendMessage(g_hRnnoiseVadSlider, TBM_GETPOS, 0, 0);
            if (vadThreshold > 0)
            {
                cmdLine += L" --rnnoise-vad " + std::to_wstring(vadThreshold);
                int gracePeriod = (int)SendMessage(g_hRnnoiseGraceSlider, TBM_GETPOS, 0, 0);
                cmdLine += L" --rnnoise-grace " + std::to_wstring(gracePeriod);
            }
        }
        else if (noiseType == NoiseReductionType::Speex)
        {
            cmdLine += L" --speex";

            // Get Speex settings
            int level = -(int)SendMessage(g_hSpeexLevelSlider, TBM_GETPOS, 0, 0);
            cmdLine += L" --speex-level " + std::to_wstring(level);

            if (SendMessage(g_hSpeexVadCheck, BM_GETCHECK, 0, 0) == BST_CHECKED)
                cmdLine += L" --speex-vad";
            if (SendMessage(g_hSpeexAgcCheck, BM_GETCHECK, 0, 0) == BST_CHECKED)
                cmdLine += L" --speex-agc";
            if (SendMessage(g_hSpeexDereverbCheck, BM_GETCHECK, 0, 0) == BST_CHECKED)
                cmdLine += L" --speex-dereverb";
        }

        cmdLine += L" --autostart";
        cmdLine += L" --autohide";  // Launch to system tray
        cmdLine += L"\r\n";

        // Write to file
        HANDLE hFile = CreateFile(fileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            // Convert to ANSI for batch file
            int len = WideCharToMultiByte(CP_ACP, 0, cmdLine.c_str(), -1, NULL, 0, NULL, NULL);
            char* ansiStr = new char[len];
            WideCharToMultiByte(CP_ACP, 0, cmdLine.c_str(), -1, ansiStr, len, NULL, NULL);

            DWORD written;
            WriteFile(hFile, ansiStr, strlen(ansiStr), &written, NULL);
            CloseHandle(hFile);
            delete[] ansiStr;

            MessageBox(g_hWnd, L"Settings saved successfully!", L"Success", MB_OK | MB_ICONINFORMATION);
        }
        else
        {
            MessageBox(g_hWnd, L"Failed to save file", L"Error", MB_OK | MB_ICONERROR);
        }
    }
}

void AddTrayIcon()
{
    // Initialize NOTIFYICONDATA structure
    g_nid.cbSize = sizeof(NOTIFYICONDATA);
    g_nid.hWnd = g_hWnd;
    g_nid.uID = TRAY_ICON_ID;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"Audio Router");

    // Add the icon to the system tray
    Shell_NotifyIcon(NIM_ADD, &g_nid);
    g_isInTray = true;
}

void RemoveTrayIcon()
{
    if (g_isInTray)
    {
        Shell_NotifyIcon(NIM_DELETE, &g_nid);
        g_isInTray = false;
    }
}

void UpdateTrayTooltip()
{
    if (!g_isInTray)
        return;

    if (g_isRunning)
    {
        // Get current device names
        int inputIndex = SendMessage(g_hInputCombo, CB_GETCURSEL, 0, 0);
        int outputIndex = SendMessage(g_hOutputCombo, CB_GETCURSEL, 0, 0);

        if (inputIndex != CB_ERR && outputIndex != CB_ERR)
        {
            wchar_t inputName[64] = {0};
            wchar_t outputName[64] = {0};
            SendMessage(g_hInputCombo, CB_GETLBTEXT, inputIndex, (LPARAM)inputName);
            SendMessage(g_hOutputCombo, CB_GETLBTEXT, outputIndex, (LPARAM)outputName);

            // Truncate device names if too long (szTip is max 128 chars)
            inputName[40] = L'\0';
            outputName[40] = L'\0';

            // Build tooltip: "Audio Router\nInput → Output"
            wchar_t tooltip[128] = {0};
            _snwprintf_s(tooltip, _countof(tooltip), _TRUNCATE,
                L"Audio Router\n%s \u2192 %s", inputName, outputName);

            wcscpy_s(g_nid.szTip, tooltip);
        }
        else
        {
            wcscpy_s(g_nid.szTip, L"Audio Router - Running");
        }
    }
    else
    {
        wcscpy_s(g_nid.szTip, L"Audio Router - Stopped");
    }

    // Update the tray icon tooltip
    Shell_NotifyIcon(NIM_MODIFY, &g_nid);
}

void RestoreFromTray()
{
    if (g_isInTray)
    {
        // Show and restore the window
        ShowWindow(g_hWnd, SW_SHOW);
        ShowWindow(g_hWnd, SW_RESTORE);
        SetForegroundWindow(g_hWnd);

        // Remove tray icon
        RemoveTrayIcon();
    }
}

void MinimizeToTray()
{
    // Hide the window
    ShowWindow(g_hWnd, SW_HIDE);

    // Add tray icon
    AddTrayIcon();
    UpdateTrayTooltip();
}

void ShowTrayContextMenu()
{
    // Get cursor position
    POINT pt;
    GetCursorPos(&pt);

    // Create popup menu
    HMENU hMenu = CreatePopupMenu();
    AppendMenu(hMenu, MF_STRING, ID_TRAY_RESTORE, L"Restore");
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");

    // Required for popup menu to close properly
    SetForegroundWindow(g_hWnd);

    // Show the menu
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                   pt.x, pt.y, 0, g_hWnd, NULL);

    // Clean up
    DestroyMenu(hMenu);
}

void UpdateSpeexControlsVisibility()
{
    int noiseIndex = SendMessage(g_hNoiseCombo, CB_GETCURSEL, 0, 0);
    bool showSpeex = (noiseIndex == static_cast<int>(NoiseReductionType::Speex));
    int showCmd = showSpeex ? SW_SHOW : SW_HIDE;

    ShowWindow(g_hSpeexLevelLabel, showCmd);
    ShowWindow(g_hSpeexLevelSlider, showCmd);
    ShowWindow(g_hSpeexLevelValue, showCmd);
    ShowWindow(g_hSpeexVadCheck, showCmd);
    ShowWindow(g_hSpeexAgcCheck, showCmd);
    ShowWindow(g_hSpeexDereverbCheck, showCmd);
}

void UpdateSpeexLevelDisplay()
{
    int pos = SendMessage(g_hSpeexLevelSlider, TBM_GETPOS, 0, 0);
    wchar_t text[32];
    swprintf_s(text, L"-%d dB", pos);
    SetWindowText(g_hSpeexLevelValue, text);
}

void UpdateRnnoiseControlsVisibility()
{
    int noiseIndex = SendMessage(g_hNoiseCombo, CB_GETCURSEL, 0, 0);
    bool showRnnoise = (noiseIndex == static_cast<int>(NoiseReductionType::RNNoise));
    int showCmd = showRnnoise ? SW_SHOW : SW_HIDE;

    ShowWindow(g_hRnnoiseVadLabel, showCmd);
    ShowWindow(g_hRnnoiseVadSlider, showCmd);
    ShowWindow(g_hRnnoiseVadValue, showCmd);
    ShowWindow(g_hRnnoiseGraceLabel, showCmd);
    ShowWindow(g_hRnnoiseGraceSlider, showCmd);
    ShowWindow(g_hRnnoiseGraceValue, showCmd);
}

void UpdateRnnoiseVadDisplay()
{
    int pos = SendMessage(g_hRnnoiseVadSlider, TBM_GETPOS, 0, 0);
    wchar_t text[32];
    if (pos == 0)
    {
        wcscpy_s(text, L"Off");
    }
    else
    {
        swprintf_s(text, L"%d%%", pos);
    }
    SetWindowText(g_hRnnoiseVadValue, text);
}

void UpdateRnnoiseGraceDisplay()
{
    int pos = SendMessage(g_hRnnoiseGraceSlider, TBM_GETPOS, 0, 0);
    wchar_t text[32];
    swprintf_s(text, L"%d ms", pos);
    SetWindowText(g_hRnnoiseGraceValue, text);
}

NoiseReductionConfig GetNoiseConfigFromUI()
{
    NoiseReductionConfig config;

    int noiseIndex = SendMessage(g_hNoiseCombo, CB_GETCURSEL, 0, 0);
    config.type = static_cast<NoiseReductionType>(noiseIndex);

    // Get Speex settings (always populate, even if not using Speex)
    int sliderPos = SendMessage(g_hSpeexLevelSlider, TBM_GETPOS, 0, 0);
    config.speex.noiseSuppressionLevel = -sliderPos;  // Convert to negative
    config.speex.enableVAD = (SendMessage(g_hSpeexVadCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
    config.speex.enableAGC = (SendMessage(g_hSpeexAgcCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
    config.speex.enableDereverb = (SendMessage(g_hSpeexDereverbCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);

    // Get RNNoise settings
    int vadPos = SendMessage(g_hRnnoiseVadSlider, TBM_GETPOS, 0, 0);
    int gracePos = SendMessage(g_hRnnoiseGraceSlider, TBM_GETPOS, 0, 0);
    config.rnnoise.vadThreshold = vadPos / 100.0f;  // Convert 0-100 to 0.0-1.0
    config.rnnoise.vadGracePeriodMs = static_cast<float>(gracePos);
    config.rnnoise.attenuationFactor = 0.0f;        // Mute when below threshold

    return config;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_START_BUTTON)
        {
            OnStartStop();
        }
        else if (LOWORD(wParam) == IDC_SAVE_BUTTON)
        {
            SaveSettingsToBatchFile();
        }
        else if (LOWORD(wParam) == IDC_NOISE_COMBO && HIWORD(wParam) == CBN_SELCHANGE)
        {
            // Noise reduction type changed - update controls visibility
            UpdateSpeexControlsVisibility();
            UpdateRnnoiseControlsVisibility();
        }
        else if (LOWORD(wParam) == ID_TRAY_RESTORE)
        {
            RestoreFromTray();
        }
        else if (LOWORD(wParam) == ID_TRAY_EXIT)
        {
            // Stop audio if running
            if (g_isRunning)
            {
                g_audioEngine->Stop();
            }
            // Remove tray icon if present
            RemoveTrayIcon();
            // Close application
            DestroyWindow(hWnd);
        }
        break;

    case WM_HSCROLL:
        // Handle trackbar/slider changes
        if ((HWND)lParam == g_hSpeexLevelSlider)
        {
            UpdateSpeexLevelDisplay();
        }
        else if ((HWND)lParam == g_hRnnoiseVadSlider)
        {
            UpdateRnnoiseVadDisplay();
        }
        else if ((HWND)lParam == g_hRnnoiseGraceSlider)
        {
            UpdateRnnoiseGraceDisplay();
        }
        break;

    case WM_SYSCOMMAND:
        // Intercept minimize command
        if (wParam == SC_MINIMIZE)
        {
            MinimizeToTray();
            return 0;  // Prevent default minimize
        }
        return DefWindowProc(hWnd, message, wParam, lParam);

    case WM_TRAYICON:
        // Handle tray icon messages
        if (lParam == WM_LBUTTONDOWN || lParam == WM_LBUTTONDBLCLK)
        {
            // Left click or double-click: restore window
            RestoreFromTray();
        }
        else if (lParam == WM_RBUTTONDOWN)
        {
            // Right click: show context menu
            ShowTrayContextMenu();
        }
        break;

    case WM_APPENDDIAG:
        // Thread-safe diagnostic text update from audio thread
        {
            std::wstring* pText = (std::wstring*)lParam;
            AppendDiagnosticsImpl(*pText);
            delete pText;
        }
        break;

    case WM_DESTROY:
        // Clean up tray icon if present
        RemoveTrayIcon();
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}
