#!/usr/bin/env bash
# Builds Ikaros_Studio-<version>-x86_64.AppImage from the Release build.
#   packaging/appimage/build-appimage.sh
# Downloads linuxdeploy and appimagetool into build-appimage/tools on first run.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
out="$root/build-appimage"
tools="$out/tools"
appdir="$out/AppDir"
app_id="io.github.thecalamityjoe87.IkarosStudio"
version="$(sed -n 's/^project(IkarosStudio VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")"

mkdir -p "$tools"
fetch() {  # fetch <name> <url>
    [[ -x "$tools/$1" ]] || { curl -fsSL -o "$tools/$1" "$2" && chmod +x "$tools/$1"; }
}
fetch linuxdeploy  https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
fetch appimagetool https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage

# Run the tools without needing FUSE.
export APPIMAGE_EXTRACT_AND_RUN=1

cmake --build "$root/build-release" -j3

rm -rf "$appdir"
DESTDIR="$appdir" cmake --install "$root/build-release" --prefix /usr

# Copies the shared libraries the binary needs into the AppDir.
"$tools/linuxdeploy" --appdir "$appdir" \
    --desktop-file "$appdir/usr/share/applications/$app_id.desktop" \
    --icon-file "$root/resources/icons/$app_id.svg" \
    --executable "$appdir/usr/bin/ikaros-studio"

ARCH=x86_64 VERSION="$version" "$tools/appimagetool" "$appdir" \
    "$out/Ikaros_Studio-$version-x86_64.AppImage"
