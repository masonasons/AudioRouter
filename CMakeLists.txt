cmake_minimum_required(VERSION 3.15)
project(AudioRouter)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows specific settings
if(WIN32)
    add_definitions(-DUNICODE -D_UNICODE)
    set(CMAKE_WIN32_EXECUTABLE ON)
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/AudioDeviceManager.cpp
    src/AudioEngine.cpp
    src/NoiseSuppress.cpp
    src/RNNoiseProcessor.cpp
    src/SpeexProcessor.cpp
)

# Create executable
add_executable(AudioRouter WIN32 ${SOURCES})

# Link Windows libraries
target_link_libraries(AudioRouter
    ole32
    winmm
    avrt
)

#
# RNNoise Integration
#
set(RNNOISE_DIR "${CMAKE_SOURCE_DIR}/external/rnnoise")

# Check if RNNoise is available and determine integration method
if(EXISTS "${RNNOISE_DIR}")
    message(STATUS "RNNoise directory found: ${RNNOISE_DIR}")

    # Method 1: Pre-built library
    if(EXISTS "${RNNOISE_DIR}/lib/rnnoise.lib" OR EXISTS "${RNNOISE_DIR}/lib/librnnoise.a")
        message(STATUS "Using pre-built RNNoise library")

        # Add include directory
        target_include_directories(AudioRouter PRIVATE "${RNNOISE_DIR}/include")
        target_compile_definitions(AudioRouter PRIVATE HAVE_RNNOISE=1)

        # Link the library
        if(EXISTS "${RNNOISE_DIR}/lib/rnnoise.lib")
            target_link_libraries(AudioRouter "${RNNOISE_DIR}/lib/rnnoise.lib")
        else()
            target_link_libraries(AudioRouter "${RNNOISE_DIR}/lib/librnnoise.a")
        endif()

    # Method 2: Build from source files
    elseif(EXISTS "${RNNOISE_DIR}/src")
        message(STATUS "Building RNNoise from source")

        # Collect RNNoise source files (including x86 subdirectory)
        file(GLOB_RECURSE RNNOISE_SOURCES
            "${RNNOISE_DIR}/src/*.c"
            "${RNNOISE_DIR}/src/*.cpp"
        )

        # Exclude utility files that are not part of the library
        list(FILTER RNNOISE_SOURCES EXCLUDE REGEX "dump_features\\.c$")
        list(FILTER RNNOISE_SOURCES EXCLUDE REGEX "dump_rnnoise_tables\\.c$")
        list(FILTER RNNOISE_SOURCES EXCLUDE REGEX "write_weights\\.c$")
        # Windows is little-endian, so exclude big-endian data and use little-endian
        list(FILTER RNNOISE_SOURCES EXCLUDE REGEX "rnnoise_data\\.c$")

        # Exclude x86 optimizations that require special compiler flags
        # These can be enabled later with proper AVX2/SSE4.1 flags
        list(FILTER RNNOISE_SOURCES EXCLUDE REGEX "x86/nnet_avx2\\.c$")
        list(FILTER RNNOISE_SOURCES EXCLUDE REGEX "x86/nnet_sse4_1\\.c$")
        list(FILTER RNNOISE_SOURCES EXCLUDE REGEX "x86/x86_dnn_map\\.c$")
        list(FILTER RNNOISE_SOURCES EXCLUDE REGEX "x86/x86cpu\\.c$")

        # Create RNNoise static library
        if(RNNOISE_SOURCES)
            add_library(rnnoise STATIC ${RNNOISE_SOURCES})

            # Set include directories for RNNoise
            target_include_directories(rnnoise PUBLIC
                "${RNNOISE_DIR}/include"
                "${RNNOISE_DIR}/src"
            )

            # RNNoise specific compiler flags
            if(MSVC)
                # Visual Studio specific flags
                target_compile_options(rnnoise PRIVATE
                    /W3                     # Warning level 3
                    /wd4244                 # Suppress conversion warnings
                    /wd4267                 # Suppress size_t conversion warnings
                )
                # Define _CRT_SECURE_NO_WARNINGS if needed for MSVC
                target_compile_definitions(rnnoise PRIVATE _CRT_SECURE_NO_WARNINGS)
            else()
                # GCC/Clang flags
                target_compile_options(rnnoise PRIVATE
                    -Wno-sign-compare
                    -Wno-unused-parameter
                )
            endif()

            # Link RNNoise to main executable
            target_include_directories(AudioRouter PRIVATE "${RNNOISE_DIR}/include")
            target_link_libraries(AudioRouter rnnoise)
            target_compile_definitions(AudioRouter PRIVATE HAVE_RNNOISE=1)

            list(LENGTH RNNOISE_SOURCES RNNOISE_SOURCES_COUNT)
            message(STATUS "RNNoise library created with ${RNNOISE_SOURCES_COUNT} source files")
        else()
            message(WARNING "RNNoise src directory exists but no source files found")
        endif()

    # Method 3: Include-only header files
    elseif(EXISTS "${RNNOISE_DIR}/include")
        message(STATUS "Using RNNoise as header-only")
        target_include_directories(AudioRouter PRIVATE "${RNNOISE_DIR}/include")

    else()
        message(WARNING "RNNoise directory found but no recognizable structure (lib/, src/, or include/)")
    endif()
else()
    message(STATUS "RNNoise not found at ${RNNOISE_DIR} - building without noise suppression")
endif()

#
# Speex Integration
#
set(SPEEX_DIR "${CMAKE_SOURCE_DIR}/external/speex")
set(SPEEXDSP_DIR "${CMAKE_SOURCE_DIR}/external/speexdsp")
set(SPEEXDSP_CONFIG_DIR "${CMAKE_SOURCE_DIR}/external/speexdsp_config")

# Check if SpeexDSP is available (speexdsp contains the preprocessor we need)
if(EXISTS "${SPEEXDSP_DIR}")
    message(STATUS "SpeexDSP directory found: ${SPEEXDSP_DIR}")

    # Method 1: Pre-built library
    if(EXISTS "${SPEEXDSP_DIR}/lib/speexdsp.lib" OR EXISTS "${SPEEXDSP_DIR}/lib/libspeexdsp.a")
        message(STATUS "Using pre-built SpeexDSP library")

        target_include_directories(AudioRouter PRIVATE
            "${SPEEXDSP_DIR}/include"
            "${SPEEXDSP_CONFIG_DIR}"
        )
        target_compile_definitions(AudioRouter PRIVATE HAVE_SPEEX=1)

        if(EXISTS "${SPEEXDSP_DIR}/lib/speexdsp.lib")
            target_link_libraries(AudioRouter "${SPEEXDSP_DIR}/lib/speexdsp.lib")
        else()
            target_link_libraries(AudioRouter "${SPEEXDSP_DIR}/lib/libspeexdsp.a")
        endif()

    # Method 2: Build from source files
    elseif(EXISTS "${SPEEXDSP_DIR}/libspeexdsp")
        message(STATUS "Building SpeexDSP from source")

        # Collect SpeexDSP source files
        file(GLOB SPEEXDSP_SOURCES
            "${SPEEXDSP_DIR}/libspeexdsp/*.c"
        )

        # Exclude test files
        list(FILTER SPEEXDSP_SOURCES EXCLUDE REGEX "testdenoise\\.c$")
        list(FILTER SPEEXDSP_SOURCES EXCLUDE REGEX "testecho\\.c$")
        list(FILTER SPEEXDSP_SOURCES EXCLUDE REGEX "testjitter\\.c$")
        list(FILTER SPEEXDSP_SOURCES EXCLUDE REGEX "testresample\\.c$")
        list(FILTER SPEEXDSP_SOURCES EXCLUDE REGEX "testresample2\\.c$")

        if(SPEEXDSP_SOURCES)
            add_library(speexdsp STATIC ${SPEEXDSP_SOURCES})

            target_include_directories(speexdsp PUBLIC
                "${SPEEXDSP_DIR}/include"
                "${SPEEXDSP_DIR}/include/speex"
                "${SPEEXDSP_DIR}/libspeexdsp"
                "${SPEEXDSP_CONFIG_DIR}"
            )

            # Speex needs these defines
            target_compile_definitions(speexdsp PRIVATE
                HAVE_CONFIG_H=1
            )

            if(MSVC)
                target_compile_options(speexdsp PRIVATE
                    /W3
                    /wd4244
                    /wd4267
                    /wd4305
                )
                target_compile_definitions(speexdsp PRIVATE _CRT_SECURE_NO_WARNINGS)
            else()
                target_compile_options(speexdsp PRIVATE
                    -Wno-sign-compare
                    -Wno-unused-parameter
                )
            endif()

            target_include_directories(AudioRouter PRIVATE
                "${SPEEXDSP_DIR}/include"
                "${SPEEXDSP_CONFIG_DIR}"
            )
            target_link_libraries(AudioRouter speexdsp)
            target_compile_definitions(AudioRouter PRIVATE HAVE_SPEEX=1)

            list(LENGTH SPEEXDSP_SOURCES SPEEXDSP_SOURCES_COUNT)
            message(STATUS "SpeexDSP library created with ${SPEEXDSP_SOURCES_COUNT} source files")
        endif()

    # Method 3: Include-only header files
    elseif(EXISTS "${SPEEXDSP_DIR}/include")
        message(STATUS "Using SpeexDSP as header-only (linking will fail)")
        target_include_directories(AudioRouter PRIVATE
            "${SPEEXDSP_DIR}/include"
            "${SPEEXDSP_CONFIG_DIR}"
        )
    endif()
else()
    message(STATUS "SpeexDSP not found at ${SPEEXDSP_DIR} - Speex noise suppression will not be available")
endif()

# Set output directory
set_target_properties(AudioRouter PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
