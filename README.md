# Spectral Equalizer

A real-time spectral equalizer built with C++20 and Qt6.

Load a WAV file, select a region on the spectrogram, and modify its magnitude using the gain and brightness sliders. Play the original and modified audio to hear the difference.

## Stack

- **C++20** — RAII, move semantics, std::jthread, std::atomic
- **FFTW3** — STFT / iSTFT
- **Qt6** — desktop GUI
- **PortAudio** — audio playback
- **libsndfile** — WAV file loading

## Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE="<path_to_vcpkg>/scripts/buildsystems/vcpkg.cmake"
cmake --build . --config Debug
```

## Dependencies

Managed via vcpkg. See `vcpkg.json`.
