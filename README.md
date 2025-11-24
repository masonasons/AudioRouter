# Audio Router

A simple Windows application for routing audio from input devices to output devices with optional RNNoise noise suppression.

## Download

**[Download Latest Build](https://github.com/masonasons/AudioRouter/releases/latest/download/AudioRouter.exe)**

Or visit the [Releases Page](https://github.com/masonasons/AudioRouter/releases/latest) for more information.

## Features

- Route audio from any input device (microphone, line-in) to any output device (speakers, headphones)
- RNNoise neural network noise suppression (optional, configurable)
- System tray integration with minimize to tray
- Command line parameters for automation (`--input`, `--output`, `--noise`, `--autostart`, `--autohide`)
- Batch file generation for startup scripts
- Sample rate and channel conversion support
- Simple Win32 interface with keyboard navigation (Tab, Ctrl+S)
- Low-latency audio routing using WASAPI event-driven mode

## Requirements

- Windows 10 or later
- Visual Studio 2019 or later (or MinGW with CMake)
- CMake 3.15 or later

## Building

### Using CMake (Command Line)

```batch
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

### Using Visual Studio

```batch
mkdir build
cd build
cmake .. -G "Visual Studio 16 2019"
```

Then open `AudioRouter.sln` in Visual Studio and build.

### Quick Build Script

You can also use the provided batch file:

```batch
build.bat
```

## Usage

### Basic Usage

1. Launch `AudioRouter.exe`
2. Select your input device (e.g., microphone) from the dropdown
3. Select your output device (e.g., speakers or headphones) from the dropdown
4. (Optional) Check "Enable Noise Suppression" to reduce background noise
5. Click "Start" to begin routing audio (or press Ctrl+S)
6. Click "Stop" to stop routing (or press Ctrl+S again)
7. Click "Save Settings" to create a batch file for quick startup

### Command Line Usage

```batch
AudioRouter.exe --input "Microphone" --output "Speakers" --noise --autostart --autohide
```

**Parameters:**
- `--input <device>` or `-i <device>` - Select input device by name or index
- `--output <device>` or `-o <device>` - Select output device by name or index
- `--noise` or `-n` - Enable noise suppression
- `--autostart` or `-a` - Automatically start audio routing
- `--autohide` or `-h` - Launch minimized to system tray

### System Tray

- Minimize the window to send it to the system tray
- Left-click the tray icon to restore the window
- Right-click the tray icon for options (Restore, Exit)
- Tray tooltip shows the current audio routing when active

## Noise Suppression

The application includes full RNNoise neural network noise suppression. RNNoise is automatically downloaded and built during the CMake build process.

**How it works:**
- Uses the Xiph.org RNNoise library for deep learning-based noise reduction
- Processes audio in 480-sample frames at any sample rate (with automatic resampling)
- Supports mono and stereo input/output with automatic channel conversion
- Maintains low latency while providing effective noise suppression

## Architecture

- **main.cpp**: Win32 GUI and application entry point
- **AudioDeviceManager**: Enumerates audio devices using WASAPI
- **AudioEngine**: Handles audio capture, routing, and playback
- **NoiseSuppress**: Wrapper for RNNoise noise suppression

## License

This is a simple example application. Feel free to modify and use as needed.
