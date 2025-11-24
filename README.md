# Audio Router

A simple Windows application for routing audio from input devices to output devices with optional noise suppression.

## Features

- Route audio from any input device (microphone, line-in) to any output device (speakers, headphones)
- Optional RNNoise-based noise suppression (disabled by default)
- Simple Win32 interface for maximum accessibility
- Low-latency audio routing using WASAPI

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

1. Launch `AudioRouter.exe`
2. Select your input device (e.g., microphone) from the dropdown
3. Select your output device (e.g., speakers or headphones) from the dropdown
4. (Optional) Check "Enable Noise Suppression" to reduce background noise
5. Click "Start" to begin routing audio
6. Click "Stop" to stop routing

## Noise Suppression

The application includes stub support for RNNoise noise suppression. To enable full RNNoise functionality:

1. Download or build the RNNoise library from: https://github.com/xiph/rnnoise
2. Place `rnnoise.h` in `external/rnnoise/include/`
3. Place `rnnoise.lib` in `external/rnnoise/lib/`
4. Update `src/NoiseSuppress.cpp` to include the real `rnnoise.h`
5. Uncomment the RNNoise lines in `CMakeLists.txt`
6. Rebuild the project

## Architecture

- **main.cpp**: Win32 GUI and application entry point
- **AudioDeviceManager**: Enumerates audio devices using WASAPI
- **AudioEngine**: Handles audio capture, routing, and playback
- **NoiseSuppress**: Wrapper for RNNoise noise suppression

## License

This is a simple example application. Feel free to modify and use as needed.
