#!/usr/bin/env bash
# Builds Ikaros_Studio-<version>-macos.dmg, with one app that runs on both
# Apple Silicon and Intel Macs.
#   packaging/macos/build-dmg.sh
# Needs Xcode (or its command line tools), CMake and Ninja.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
out="$root/build-dmg"
app_name="Ikaros Studio.app"
exe_path="Contents/MacOS/Ikaros Studio"

# sfizz can't be built for both architectures at once (see CMakeLists.txt),
# so each gets a Release build of its own, and lipo joins the two programs.
for arch in arm64 x86_64; do
    cmake -S "$root" -B "$root/build-macos-$arch" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$arch"
    cmake --build "$root/build-macos-$arch"
done

version="$(cat "$root/build-macos-arm64/version.txt")"   # written by CMake
arm_app="$root/build-macos-arm64/IkarosStudio_artefacts/Release/$app_name"
intel_app="$root/build-macos-x86_64/IkarosStudio_artefacts/Release/$app_name"

rm -rf "$out"
mkdir -p "$out/dmg"
app="$out/dmg/$app_name"

# Everything but the program is the same in both builds (Info.plist, icon,
# sounds), so the Apple Silicon one is the starting point.
ditto "$arm_app" "$app"
lipo -create "$arm_app/$exe_path" "$intel_app/$exe_path" -output "$app/$exe_path"

# An ad-hoc signature ("-"): Apple Silicon Macs don't run a program without
# one, and lipo's output has none. It isn't a Developer ID, so macOS still
# asks before opening the app the first time (System Settings > Privacy &
# Security > Open Anyway).
codesign --force --sign - "$app"

# The disk image shows the app next to a link to Applications, to drag it onto.
ln -s /Applications "$out/dmg/Applications"
hdiutil create -volname "Ikaros Studio" -srcfolder "$out/dmg" -format UDZO -ov \
    "$out/Ikaros_Studio-$version-macos.dmg"
