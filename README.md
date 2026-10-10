# Ikaros Studio

An easy-first, cross-platform music studio in the spirit of GarageBand. It's simple by default, and advanced controls stay hidden until you ask for them.

Built with C++20, [JUCE](https://juce.com) and [Tracktion Engine](https://github.com/Tracktion/tracktion_engine). It runs on Linux, Windows and macOS and looks the same on all three.

- App ID: `io.github.thecalamityjoe87.IkarosStudio`
- Status: **Milestone 1**. You can create sessions, add instrument tracks, make MIDI regions, play them back and save them.

## Getting the source

Tracktion Engine is a git submodule at `external/tracktion_engine`, and it has its own JUCE submodule. Tracktion's `.gitmodules` points JUCE at an SSH URL, so if you don't have a GitHub SSH key, tell git to use HTTPS:

```sh
git clone https://github.com/thecalamityjoe87/ikaros-studio.git
cd ikaros-studio
git -c url."https://github.com/".insteadOf="git@github.com:" submodule update --init --recursive
```

## Building

You need CMake ≥ 3.22 and a C++20 compiler. The first build compiles all of JUCE and Tracktion, so it takes several minutes.

### Linux (Debian / Ubuntu)

```sh
sudo apt install build-essential cmake ninja-build pkg-config \
    libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev \
    libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev \
    libxrandr-dev libxrender-dev libgl1-mesa-dev

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/IkarosStudio_artefacts/Release/Ikaros\ Studio
```

Use a Release (or RelWithDebInfo) build to judge how the app feels. Debug builds are unoptimised and draw about 9× slower, so window resizing looks choppy. For day-to-day development, `-DCMAKE_BUILD_TYPE=RelWithDebInfo` is a good middle ground: optimised, debuggable, and it relinks quickly (Release uses link-time optimisation, so relinking takes minutes).

You don't need GTK or WebKit, because the app turns off JUCE's web browser and curl support.

To install system-wide (binary, `.desktop` file, AppStream metainfo and icons):

```sh
sudo cmake --install build --prefix /usr/local
```

### Windows (Visual Studio 2022)

Install Visual Studio 2022 with the **Desktop development with C++** workload; it includes CMake. Then, from a *Developer PowerShell*:

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
.\build\IkarosStudio_artefacts\Release\"Ikaros Studio.exe"
```

You can also open the folder directly in Visual Studio, which detects `CMakeLists.txt`.

### macOS (Xcode)

Install Xcode and CMake (`brew install cmake`), then:

```sh
cmake -B build -G Xcode
cmake --build build --config Release --parallel
open "build/IkarosStudio_artefacts/Release/Ikaros Studio.app"
```

Or open `build/IkarosStudio.xcodeproj` in Xcode. The menus appear in the native macOS menu bar.

### Engine smoke test

There's an optional console test that checks the engine layer without any UI: tracks, regions, mute/solo/volume, undo/redo, and the save → open round-trip. It compiles Tracktion a second time, so it's off by default:

```sh
cmake -B build -DIKAROS_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### Flatpak

```sh
flatpak-builder --user --install --force-clean build-flatpak \
    packaging/flatpak/io.github.thecalamityjoe87.IkarosStudio.yml
flatpak run io.github.thecalamityjoe87.IkarosStudio
```

## Using it

| Action | How |
|---|---|
| Play / stop | Space, or the amber Play button |
| Go to start | Home / Return, or ⏮ |
| Loop | the Loop button; drag along the top edge of the bar ruler to set the loop range |
| Tempo | click the BPM in the LCD and type a value, or scroll over it |
| New track | **+ New Track**, or click the dashed drop zone |
| New region | double-click an empty spot in a track lane (creates a 4-bar chord region) |
| Edit a region | double-click it to open the **Editor** tab (piano roll, read-only for now) |
| Delete | select a region and press Delete |
| Play notes | click the on-screen keyboard, focus it and type A W S E D F T G…, or use a MIDI keyboard |
| Undo / redo | Ctrl/Cmd+Z, Ctrl/Cmd+Shift+Z |
| Save / open | File menu (sessions are `.tracktionedit` files, saved in *Documents/Ikaros Studio* by default) |

## Project layout

```
CMakeLists.txt                  app target, source lists (grouped like src/), install rules
external/tracktion_engine/      git submodule (bundles JUCE in modules/juce)
resources/
  fonts/                        Figtree + JetBrains Mono (SIL OFL, licences included)
  icons/                        app icon (SVG source + 256px PNG)
packaging/
  linux/                        .desktop file, AppStream metainfo
  flatpak/                      Flatpak manifest
tests/                          optional engine smoke test (-DIKAROS_BUILD_TESTS=ON)
src/
  app/                          entry point and app shell
    Main.cpp                    JUCEApplication: owns the theme, engine and window
    MainWindow.*                the window: custom title bar, maximise/restore
    MainComponent.*             layout, commands (menus + shortcuts), refresh loop
    AppMenuModel.*, CommandIDs.h
  engine/                       the ONLY code that includes Tracktion
    EngineTypes.h               plain structs the UI uses (TrackInfo, RegionInfo, ...)
    AudioEngine.*               te::Engine + te::Edit: sessions, transport, tempo, undo, meters
    TrackController.*           tracks, regions, mixer, sound controls, live MIDI
  platform/                     the ONLY OS-specific code (window-manager integration on Linux)
  ui/                           JUCE components only
    theme/                      Tokens.h (colours, sizes), Fonts, IkarosLookAndFeel
    widgets/                    reusable controls: Icons, IconButton, Knob, LevelMeter
    state/                      UI state shared between areas (Selection)
    titlebar/                   MenuBar (doubles as the title bar), WindowControls
    toolbar/                    Toolbar, TransportBar, LcdDisplay
    library/                    LibrarySidebar
    arrangement/                ArrangementView (ruler, lanes, playhead), TrackHeader, RegionComponent
    bottompanel/                BottomPanel (Sound | Editor | Mixer), PianoKeyboard, PianoRoll, MixerView
```

Where new code goes: a new area of the window gets its own folder under `ui/`. A control used by more than one area goes in `ui/widgets/`. Anything that needs Tracktion goes in `engine/`, behind a plain-struct API.

### Architecture in brief

- **Engine/UI split.** Only `src/engine/` includes `<tracktion_engine/tracktion_engine.h>`. The engine headers forward-declare Tracktion types and keep them behind a *pimpl* (`struct Impl`), so UI code can't reach `te::` even by accident. The UI refers to tracks and regions by ID and gets snapshots as plain structs.
- **Two refresh paths.** Model changes (tracks, regions, mute, undo, loading a session) come from `AudioEngine` as coalesced `ChangeBroadcaster` messages, and the UI re-reads the model. Fast state (playhead, meters, CPU) is polled by a 30 Hz `juce::Timer` in `MainComponent`. UI code never touches the audio thread.
- **Undo.** Every edit goes through Tracktion's `UndoManager`. Continuous controls (faders, knobs) begin an undo step on mouse-down, so a whole drag undoes in one go.
- **Custom title bar.** The OS title bar is turned off. `MenuBar` doubles as the title bar: drag it to move the window, double-click to maximise. `WindowControls` draws minimise/maximise/close, on the right (on the left on macOS). `MainWindow::findControlAtPoint` reports these areas to the OS, so Windows keeps Aero Snap and Snap Layouts. Drag a maximised window's title strip to restore it. On Linux, `platform/WindowManagerHints` routes maximise/restore and title-bar drags through the window manager (EWMH). That's needed because Mutter locks undecorated windows that size themselves to the whole monitor.
- **Commands.** Menu items, toolbar buttons and keyboard shortcuts all run through one `ApplicationCommandManager`, so each action is defined once in `MainComponent::getCommandInfo` / `perform`.

## Licences

- Ikaros Studio: TBD. Tracktion Engine and JUCE are GPLv3 for open-source projects, so a GPL-compatible licence is the simplest choice.
- Figtree and JetBrains Mono: SIL Open Font License 1.1 (see `resources/fonts/OFL-*.txt`).
