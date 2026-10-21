# Ikaros Studio

A simple music studio for Linux, Windows and macOS, in the spirit of GarageBand. Built with JUCE and Tracktion Engine.

Early development. You can make instrument and audio tracks, record from a MIDI keyboard, a microphone, a guitar or a stereo source (with a count-in if you like), pick sounds and loops from the library, edit regions and notes, add effects, send tracks to a shared reverb and delay, draw automation, and export the song or its tracks. Loops follow the session's key and tempo, the tempo and time signature can change partway through, and audio regions can be trimmed, faded, reversed and transposed in their own editor. Turn on Flex for an audio track to drag the hits in a recording to new places (the audio in between stretches to fit), or quantize them onto the grid like notes, with a strength and swing of your choice. Mark the song's sections (intro, verse, chorus) above the tracks, then drag a section to move its music on every track, or Alt-drag to copy it. Guitar and bass tracks get a pedalboard of twenty stompboxes, from overdrives and fuzzes to a flanger and a spring reverb, which you can reorder by dragging, and there's a tuner in the display. After the pedals comes an amp with six voices (Clean, Crunch, Chime, British, High Gain and Bass), each with its own tone controls and speaker cabinet, which you can swap or turn off. Most of the pedals' sound comes from GxPlugins, Guitarix and KlonCentaur, each one checked against the original, and the amp is built from Guitarix's amp models and cabinets. VST3 and LV2 plug-ins work on Linux and Windows, and VST3 and Audio Units on macOS.

A saved session is a single `.iksession` file, like a GarageBand project: it holds the session and every recording, loop and audio file it uses, so you can move it, back it up or send it to someone on Linux, Windows or macOS. Instruments from sound packs aren't copied into it; the session finds them by pack on whichever computer opens it. Sessions saved as folders by earlier versions still open, and saving one writes a `.iksession` next to its folder.

While you work, the app keeps its own recovery copy of the session a few seconds after you stop making changes, and recordings go to disk as you play. Your saved file only changes when you save. If the app crashes, it offers to bring back your unsaved work the next time it starts, including a take that was cut short.

Recordings line up with the music on their own: the app allows for the delay of your audio interface. If the audio device stops or changes, or the disk fills up, in the middle of a take, recording stops and keeps what you played so far, and the app tells you what happened. It also lets you know when your input was too loud.

## Sounds and plug-ins

The library's sampled instruments and loops come in sound packs: plain folders with an `instrument.json` per instrument and a `loops.json` for loops (see `src/engine/SoundLibrary.h`). The app ships one, Ikaros Basics, which is made from scratch by `tools/generate_sounds.py`, so there are no third-party samples in the repo. To add more, drop a pack folder into `Documents/Ikaros Studio/Sounds`, or download one: packs that aren't installed are listed in their library category with a download button, and Sounds > Manage Sounds... (or the button at the bottom of the library) lists every pack, to download, update or remove.

Plug-ins are found by a scan in the background at startup. Each plug-in is loaded in a separate helper process, so one that crashes or freezes is just skipped (and remembered) instead of taking the app down. Mix > Plug-ins... shows what was found in the library and can scan again; effects are added from a track's Plug-ins section in the inspector.

## Building

You need CMake 3.22+ and a C++20 compiler. Tracktion Engine (which includes JUCE) lives in `external/tracktion_engine`, sfizz, which plays SFZ instruments, in `external/sfizz`, miniz, which reads and writes session files, in `external/miniz`, Rubber Band, which stretches loops to the song's tempo and key, in `external/rubberband`, the DSP behind the pedals in `external/gxplugins` and `external/kloncentaur`, and behind the amp in `external/guitarix`. The first build takes a while because it compiles all of them. The projects Ikaros Studio is built on, and their licenses, are listed in `CREDITS`.

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

`sudo cmake --install build` installs the binary, desktop file, metainfo, icons and the sound packs.

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

Add `-DIKAROS_BUILD_TEST_PLUGINS=ON` to also build a few tiny VST3/LV2 plug-ins (and two deliberately broken ones) that the test scans and loads. They end up in `build/TestPlugins`, never in your real plug-in folders. The test keeps its own settings, so it doesn't touch the app's.

### Flatpak

The manifest in `packaging/flatpak` is a starting point, not ready for Flathub yet.

## License

Not decided yet. JUCE and Tracktion Engine are GPLv3 for open-source projects, so it'll likely be GPL. Figtree and JetBrains Mono are under the SIL Open Font License (see `resources/fonts`). Some instrument icons are from [Lucide](https://lucide.dev) (ISC License; the notice is in `src/ui/widgets/Icons.cpp`).
