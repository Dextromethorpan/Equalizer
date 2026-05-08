# =============================================================================
# CMakeLists.txt — Equalizer project root
#
# What this file does
# -------------------
# CMake reads this file and generates the actual build system (in our case,
# Visual Studio project files). Think of it as a recipe that describes:
#   - what C++ standard we require
#   - where to find external libraries (via vcpkg)
#   - what source files to compile
#   - how to link everything together
#
# CMake version requirement
# -------------------------
# 3.25 introduced better C++20 support and improved vcpkg integration.
# We require at least this version.
# =============================================================================

cmake_minimum_required(VERSION 3.25)

# -----------------------------------------------------------------------------
# Toolchain file — must be set BEFORE project()
#
# vcpkg integrates with CMake via a "toolchain file" — a CMake script that
# tells CMake where vcpkg-installed libraries live. Setting this here means
# find_package() calls below will automatically find our vcpkg libraries.
# -----------------------------------------------------------------------------
set(CMAKE_TOOLCHAIN_FILE
    "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake"
    CACHE STRING "vcpkg toolchain file"
)

# -----------------------------------------------------------------------------
# Project declaration
#
# NAME:     Equalizer
# VERSION:  0.1.0 (major.minor.patch — semantic versioning)
# LANGUAGES: CXX means C++ only (no C, no CUDA etc.)
# -----------------------------------------------------------------------------
project(Equalizer VERSION 0.1.0 LANGUAGES CXX)

# -----------------------------------------------------------------------------
# C++ standard
#
# We require C++20. CMAKE_CXX_STANDARD_REQUIRED means CMake will error out
# rather than silently fall back to an older standard.
# CMAKE_CXX_EXTENSIONS OFF disables compiler-specific extensions (e.g. GNU
# extensions on GCC) so our code stays portable and standards-compliant.
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -----------------------------------------------------------------------------
# Compiler warnings
#
# /W4  — warning level 4 (high, but not "all warnings")
# /WX  — treat warnings as errors (forces clean code)
# /permissive- — strict standards conformance mode
#
# These are MSVC-specific flags. In a cross-platform project you would
# use generator expressions to switch between MSVC and GCC/Clang flags.
# -----------------------------------------------------------------------------
add_compile_options(/W4 /WX /permissive-)

# -----------------------------------------------------------------------------
# Find external libraries (installed by vcpkg)
#
# find_package() searches CMake's package registry. vcpkg pre-populates this
# registry when you run vcpkg install. The REQUIRED keyword means CMake will
# error out if a package is not found, rather than silently continuing.
# -----------------------------------------------------------------------------
# -----------------------------------------------------------------------------
# Qt6 path — installed via the official Qt installer, not vcpkg.
# Adjust this path if you installed Qt to a different location.
# The path must point to the lib/cmake/Qt6 folder inside your Qt installation.
# Example: C:/Qt/6.7.0/msvc2019_64/lib/cmake/Qt6
# -----------------------------------------------------------------------------
set(Qt6_DIR "C:/Qt/6.11.0/msvc2022_64/lib/cmake/Qt6"
    CACHE PATH "Path to Qt6 cmake files"
)

find_package(FFTW3 CONFIG REQUIRED)
find_package(FFTW3f CONFIG REQUIRED)   # single-precision — used by StftProcessor
find_package(portaudio CONFIG REQUIRED)
find_package(SndFile CONFIG REQUIRED)  # WAV file reading
find_package(Qt6 COMPONENTS Core Widgets REQUIRED)

# -----------------------------------------------------------------------------
# Qt6 automatic code generation
#
# Qt uses a code generator called MOC (Meta-Object Compiler) to implement
# signals and slots. These three lines tell CMake to run MOC, UIC (UI
# compiler), and RCC (resource compiler) automatically on any file that
# needs them.
# -----------------------------------------------------------------------------
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# -----------------------------------------------------------------------------
# Source files
#
# We list every .cpp file explicitly rather than using GLOB (glob_recurse).
# The reason: if you use GLOB and add a new file, CMake does not re-run
# automatically and the new file silently gets excluded from the build.
# Explicit lists are always safer and more professional.
# -----------------------------------------------------------------------------
set(SOURCES
    src/main.cpp
    src/audio/AudioBuffer.cpp
    src/audio/StftProcessor.cpp
    src/audio/Equalizer.cpp
    src/gui/MainWindow.cpp
    src/gui/SpectrogramWidget.cpp
)

# -----------------------------------------------------------------------------
# Executable target
#
# add_executable() defines what we are building: an executable called
# "Equalizer" compiled from the listed source files.
# -----------------------------------------------------------------------------
add_executable(Equalizer ${SOURCES})

# -----------------------------------------------------------------------------
# Include directories
#
# target_include_directories() tells the compiler where to look for header
# files when it sees #include "...". We use target_include_directories
# rather than include_directories because it is scoped to this target only —
# important in multi-target projects.
#
# PROJECT_SOURCE_DIR is the directory containing this CMakeLists.txt file.
# -----------------------------------------------------------------------------
target_include_directories(Equalizer PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

# -----------------------------------------------------------------------------
# Link libraries
#
# target_link_libraries() tells the linker what libraries to link against.
# PRIVATE means these dependencies are an implementation detail of Equalizer
# and are not exposed to anything that depends on Equalizer.
# -----------------------------------------------------------------------------
target_link_libraries(Equalizer PRIVATE
    FFTW3::fftw3f
    portaudio
    SndFile::sndfile
    Qt6::Core
    Qt6::Widgets
)

# -----------------------------------------------------------------------------
# Tests subdirectory
#
# We keep tests in a separate CMakeLists.txt to keep this file clean.
# add_subdirectory() tells CMake to process that file as well.
# -----------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)