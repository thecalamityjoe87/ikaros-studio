# Ikaros Studio

A simple music studio for Linux, Windows and macOS, in the spirit of GarageBand. Built with JUCE and Tracktion Engine.

Early development. You can make instrument tracks, add MIDI regions, play them back, add effects, and save sessions. Recording, audio files and plug-in hosting aren't in yet.

## Building

You need CMake 3.22+ and a C++20 compiler. Tracktion Engine (which includes JUCE) lives in `external/tracktion_engine`. The first build takes a while because it compiles both.

```sh
git clone --recursive https://github.com/thecalamityjoe87/ikaros-studio.git
cd ikaros-studio
```

### Linux

On Debian/Ubuntu:

```sh
sudo apt install build-essential cmake ninja-build pkg-config \
    libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev \
    libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev \
    libxrandr-dev libxrender-dev libgl1-mesa-dev

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/IkarosStudio_artefacts/Release/Ikaros\ Studio
```

`sudo cmake --install build` installs the binary, desktop file, metainfo and icons.

Debug builds draw a lot slower, so resizing feels choppy. Use `RelWithDebInfo` for day-to-day work; it's fast and relinks much quicker than `Release`.

### Windows

Visual Studio 2022 with the C++ desktop workload. From a Developer PowerShell:

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### macOS

Xcode, plus CMake (`brew install cmake`):

```sh
cmake -B build -G Xcode
cmake --build build --config Release
open "build/IkarosStudio_artefacts/Release/Ikaros Studio.app"
```

### Tests

There's a small engine test, off by default:

```sh
cmake -B build -DIKAROS_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```

### Flatpak

The manifest in `packaging/flatpak` is a starting point, not ready for Flathub yet.

## License

Not decided yet. JUCE and Tracktion Engine are GPLv3 for open-source projects, so it'll likely be GPL. Figtree and JetBrains Mono are under the SIL Open Font License (see `resources/fonts`).
