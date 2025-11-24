#include <windows.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <vector>
#include <algorithm>
#include "AudioDeviceManager.h"
#include "AudioEngine.h"

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")

// Control IDs
#define IDC_INPUT_COMBO      1001
#define IDC_OUTPUT_COMBO     1002
#define IDC_NOISE_CHECK      1003
#define IDC_START_BUTTON     1004
#define IDC_STATUS_TEXT      1005
#define IDC_SAVE_BUTTON      1006
#define IDC_DIAG_TEXT        1007

// System tray
#define WM_TRAYICON          (WM_USER + 1)
#define ID_TRAY_RESTORE      2001
#define ID_TRAY_EXIT         2002
#define TRAY_ICON_ID         1

// Global variables
HWND g_hWnd = NULL;
HWND g_hInputCombo = NULL;
HWND g_hOutputCombo = NULL;
HWND g_hNoiseCheck = NULL;
HWND g_hStartButton = NULL;
HWND g_hStatusText = NULL;
HWND g_hDiagText = NULL;

AudioDeviceManager* g_deviceManager = nullptr;
AudioEngine* g_audioEngine = nullptr;
bool g_isRunning = false;

// System tray
NOTIFYICONDATA g_nid = {};
bool g_isInTray = false;

// Command line parameters
struct CommandLineParams
{
    std::wstring inputDevice;
    std::wstring outputDevice;
    bool enableNoise = false;
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
void ParseCommandLine(CommandLineParams& params);
void ApplyCommandLineParams(const CommandLineParams& params);
void SaveSettingsToBatchFile();
void AddTrayIcon();
void RemoveTrayIcon();
void UpdateTrayTooltip();
void RestoreFromTray();
void MinimizeToTray();
void ShowTrayContextMenu();

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // Initialize COM
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

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

    // Create main window (increased height for diagnostics)
    g_hWnd = CreateWindowEx(
        0,
        L"AudioRouterClass",
        L"Audio Router",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 450, 400,
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

    // Input device label
    HWND hLabel = CreateWindow(L"STATIC", L"Input Device:",
        WS_VISIBLE | WS_CHILD,
        10, 10, 120, 20, hWnd, NULL, NULL, NULL);
    SendMessage(hLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Input device combo box
    g_hInputCombo = CreateWindow(L"COMBOBOX", NULL,
        WS_VISIBLE | WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        10, 35, 360, 200, hWnd, (HMENU)IDC_INPUT_COMBO, NULL, NULL);
    SendMessage(g_hInputCombo, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Output device label
    hLabel = CreateWindow(L"STATIC", L"Output Device:",
        WS_VISIBLE | WS_CHILD,
        10, 70, 120, 20, hWnd, NULL, NULL, NULL);
    SendMessage(hLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Output device combo box
    g_hOutputCombo = CreateWindow(L"COMBOBOX", NULL,
        WS_VISIBLE | WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        10, 95, 360, 200, hWnd, (HMENU)IDC_OUTPUT_COMBO, NULL, NULL);
    SendMessage(g_hOutputCombo, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Noise suppression checkbox
    g_hNoiseCheck = CreateWindow(L"BUTTON", L"Enable Noise Suppression",
        WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
        10, 130, 200, 20, hWnd, (HMENU)IDC_NOISE_CHECK, NULL, NULL);
    SendMessage(g_hNoiseCheck, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Start/Stop button
    g_hStartButton = CreateWindow(L"BUTTON", L"Start",
        WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
        10, 160, 100, 30, hWnd, (HMENU)IDC_START_BUTTON, NULL, NULL);
    SendMessage(g_hStartButton, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Save Settings button
    HWND hSaveButton = CreateWindow(L"BUTTON", L"Save Settings",
        WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
        120, 160, 120, 30, hWnd, (HMENU)IDC_SAVE_BUTTON, NULL, NULL);
    SendMessage(hSaveButton, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Status text
    g_hStatusText = CreateWindow(L"STATIC", L"Status: Stopped",
        WS_VISIBLE | WS_CHILD,
        10, 200, 420, 20, hWnd, (HMENU)IDC_STATUS_TEXT, NULL, NULL);
    SendMessage(g_hStatusText, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Diagnostic info text (multi-line, read-only edit control for better formatting)
    g_hDiagText = CreateWindowEx(
        WS_EX_CLIENTEDGE,
        L"EDIT", L"",
        WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
        10, 230, 420, 120, hWnd, (HMENU)IDC_DIAG_TEXT, NULL, NULL);
    SendMessage(g_hDiagText, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Set initial focus to the first interactive control
    SetFocus(g_hInputCombo);
}

void PopulateDeviceLists()
{
    // Clear combo boxes
    SendMessage(g_hInputCombo, CB_RESETCONTENT, 0, 0);
    SendMessage(g_hOutputCombo, CB_RESETCONTENT, 0, 0);

    // Get input devices
    std::vector<AudioDevice> inputDevices = g_deviceManager->GetInputDevices();
    for (const auto& device : inputDevices)
    {
        int index = SendMessage(g_hInputCombo, CB_ADDSTRING, 0, (LPARAM)device.name.c_str());
        SendMessage(g_hInputCombo, CB_SETITEMDATA, index, (LPARAM)new std::wstring(device.id));
    }
    if (!inputDevices.empty())
        SendMessage(g_hInputCombo, CB_SETCURSEL, 0, 0);

    // Get output devices
    std::vector<AudioDevice> outputDevices = g_deviceManager->GetOutputDevices();
    for (const auto& device : outputDevices)
    {
        int index = SendMessage(g_hOutputCombo, CB_ADDSTRING, 0, (LPARAM)device.name.c_str());
        SendMessage(g_hOutputCombo, CB_SETITEMDATA, index, (LPARAM)new std::wstring(device.id));
    }
    if (!outputDevices.empty())
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

        // Check if noise suppression is enabled
        bool noiseSuppression = (SendMessage(g_hNoiseCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);

        // Set up status callback to display diagnostics
        g_audioEngine->SetStatusCallback([](const std::wstring& status) {
            AppendDiagnostics(status);
        });

        // Start audio engine
        if (g_audioEngine->Start(*inputId, *outputId, noiseSuppression))
        {
            g_isRunning = true;
            SetWindowText(g_hStartButton, L"Stop");
            EnableWindow(g_hInputCombo, FALSE);
            EnableWindow(g_hOutputCombo, FALSE);
            EnableWindow(g_hNoiseCheck, FALSE);
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
        EnableWindow(g_hNoiseCheck, TRUE);
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

void AppendDiagnostics(const std::wstring& text)
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
        else if (arg == L"--noise" || arg == L"-n")
        {
            params.enableNoise = true;
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

        // First check if it's a numeric index
        int numericIndex = _wtoi(params.inputDevice.c_str());
        if (numericIndex > 0 || params.inputDevice == L"0")
        {
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
                std::wstring searchLower = params.inputDevice;
                std::transform(deviceNameLower.begin(), deviceNameLower.end(), deviceNameLower.begin(), ::towlower);
                std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::towlower);

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

        // First check if it's a numeric index
        int numericIndex = _wtoi(params.outputDevice.c_str());
        if (numericIndex > 0 || params.outputDevice == L"0")
        {
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
                std::wstring searchLower = params.outputDevice;
                std::transform(deviceNameLower.begin(), deviceNameLower.end(), deviceNameLower.begin(), ::towlower);
                std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::towlower);

                if (deviceNameLower.find(searchLower) != std::wstring::npos)
                {
                    SendMessage(g_hOutputCombo, CB_SETCURSEL, i, 0);
                    break;
                }
            }
        }
    }

    // Apply noise suppression setting
    if (params.enableNoise)
    {
        SendMessage(g_hNoiseCheck, BM_SETCHECK, BST_CHECKED, 0);
    }
}

void SaveSettingsToBatchFile()
{
    // Get current selections
    int inputIndex = SendMessage(g_hInputCombo, CB_GETCURSEL, 0, 0);
    int outputIndex = SendMessage(g_hOutputCombo, CB_GETCURSEL, 0, 0);
    bool noiseEnabled = (SendMessage(g_hNoiseCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);

    if (inputIndex == CB_ERR || outputIndex == CB_ERR)
    {
        MessageBox(g_hWnd, L"Please select input and output devices first", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    // Get device names
    wchar_t inputName[256] = {0};
    wchar_t outputName[256] = {0};
    SendMessage(g_hInputCombo, CB_GETLBTEXT, inputIndex, (LPARAM)inputName);
    SendMessage(g_hOutputCombo, CB_GETLBTEXT, outputIndex, (LPARAM)outputName);

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
        cmdLine += L" --input \"" + std::wstring(inputName) + L"\"";
        cmdLine += L" --output \"" + std::wstring(outputName) + L"\"";
        if (noiseEnabled)
            cmdLine += L" --noise";
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
            wchar_t inputName[256] = {0};
            wchar_t outputName[256] = {0};
            SendMessage(g_hInputCombo, CB_GETLBTEXT, inputIndex, (LPARAM)inputName);
            SendMessage(g_hOutputCombo, CB_GETLBTEXT, outputIndex, (LPARAM)outputName);

            // Build tooltip: "Audio Router\nInput → Output"
            std::wstring tooltip = L"Audio Router\n";
            tooltip += inputName;
            tooltip += L" \u2192 ";  // Unicode arrow →
            tooltip += outputName;

            wcscpy_s(g_nid.szTip, tooltip.c_str());
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
