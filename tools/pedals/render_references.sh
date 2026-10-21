#!/usr/bin/env bash
# Renders the pedal null tests' references: tests/fixtures/pedals/di_guitar.wav
# played through each original LV2 plugin, at the settings below. The smoke
# test (runPedalsSection) plays the same file through our port, at the same
# settings, and checks the difference is near silence.
#
# Linux only. Needs git, make, cmake, a C and C++ compiler (and, for the
# Golden Boost's original, JUCE's usual Linux build packages). Each plugin is
# built from its repository at the commit given:
#   - GxPlugins pedals with their own Makefile's "nogui" target,
#   - Guitarix plugins by compiling their LV2 wrapper the way its build does,
#   - KlonCentaur (ChowCentaur) with its CMake LV2 target (slow: it builds JUCE).
# The LV2 headers come from the LV2 repository. Everything is downloaded to a
# temporary folder (set KEEP=folder to keep and reuse it).
#
# Usage: tools/pedals/render_references.sh [pedal...]   (default: all)

set -euo pipefail

repo="$(cd "$(dirname "$0")/../.." && pwd)"
fixtures="$repo/tests/fixtures/pedals"

if [[ -n "${KEEP:-}" ]]; then
    work="$KEEP"; mkdir -p "$work"
else
    work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
fi

# One second of silence before the guitar, left out of the render: the
# original wrappers fade in over the first 8192 samples, and the DSPs glide
# their knobs to their settings. The test plays the same pre-roll.
preroll=48000

clone()   # clone URL TAG-OR-COMMIT FOLDER
{
    [[ -d "$3" ]] && return
    git init -q "$3"
    git -C "$3" fetch -q --depth 1 "$1" "$2"
    git -C "$3" -c advice.detachedHead=false checkout -q FETCH_HEAD
}

clone https://github.com/lv2/lv2 v1.18.10 "$work/lv2"

# Older plugins include <lv2.h> (the core header's old name); JUCE's LV2
# wrapper wants the even older lv2plug.in folder layout, which LV2 1.16 has.
mkdir -p "$work/shim"
echo '#include <lv2/core/lv2.h>' > "$work/shim/lv2.h"

cc -O2 -I"$work/lv2/include" "$repo/tools/pedals/lv2render.c" -o "$work/lv2render" -ldl

selected=("$@")
wanted() { [[ ${#selected[@]} -eq 0 || " ${selected[*]} " =~ " $1 " ]]; }

render()   # render NAME PLUGIN.so ARGS...
{
    local name="$1" so="$2"
    shift 2
    "$work/lv2render" "$so" "$fixtures/di_guitar.wav" "$fixtures/$name.wav" "$preroll" "$@"
}

# gx_pedal NAME REPOSITORY COMMIT BINARY ARGS...   (ARGS: see lv2render.c)
gx_pedal()
{
    local name="$1" repository="$2" commit="$3" binary="$4"
    shift 4
    wanted "$name" || return 0

    local src="$work/$repository"
    clone "https://github.com/brummer10/$repository" "$commit" "$src"

    # The Makefile adds its own flags to these (PKGCONFIG=true: the LV2
    # headers come from the folders above, not an installed package).
    [[ -f "$src/$binary.so" ]] || \
        (cd "$src" && CXXFLAGS="-I$work/shim -I$work/lv2/include" PKGCONFIG=true make -s nogui >/dev/null)

    render "$name" "$src/$binary.so" "$@"
}

guitarix_commit=666b564bb8a7194f200029fdbfbec2abe035f777

# guitarix_plugin NAME PLUGIN ARGS...   (PLUGIN: the folder in trunk/src/LV2)
guitarix_plugin()
{
    local name="$1" plugin="$2"
    shift 2
    wanted "$name" || return 0

    local src="$work/guitarix"
    clone https://github.com/brummer10/guitarix "$guitarix_commit" "$src"
    local lv2="$src/trunk/src/LV2"

    # As Guitarix's waf build compiles an LV2 plugin (its optimisation flags).
    [[ -f "$work/$plugin.so" ]] || \
        g++ -O3 -ffast-math -fPIC -shared -std=c++11 -I"$work/shim" -I"$work/lv2/include" \
            -I"$lv2/DSP" -I"$lv2/faust-generated" -I"$lv2/$plugin.lv2" \
            "$lv2/$plugin.lv2/$plugin.cpp" -o "$work/$plugin.so"

    render "$name" "$work/$plugin.so" "$@"
}

# klon NAME ARGS...
klon()
{
    local name="$1"
    shift
    wanted "$name" || return 0

    local src="$work/KlonCentaur"
    if [[ ! -d "$src" ]]; then
        git -c advice.detachedHead=false clone -q https://github.com/jatinchowdhury18/KlonCentaur "$src"
        git -C "$src" -c advice.detachedHead=false checkout -q f3bb633a593b6fbb22a44c1ef9d1dbedbfe92d5b
        git -C "$src" submodule update -q --init --recursive --depth 1
    fi

    if [[ ! -d "$work/lv2-old" ]]; then
        git -c advice.detachedHead=false clone -q --depth 1 --branch v1.16.0 https://github.com/lv2/lv2 "$work/lv2-old"
        local old="$work/lv2-old/lv2" ns="$work/lv2old-shim/lv2/lv2plug.in/ns"
        mkdir -p "$ns/ext" "$ns/extensions"
        ln -sfn "$old/core" "$ns/lv2core"
        for d in "$old"/*/; do ln -sfn "$d" "$ns/ext/$(basename "$d")"; done
        ln -sfn "$old/ui" "$ns/extensions/ui"
        ln -sfn "$old/units" "$ns/extensions/units"
    fi

    local so="$src/build/ChowCentaur/ChowCentaur_artefacts/Release/LV2/ChowCentaur.lv2/ChowCentaur.so"
    if [[ ! -f "$so" ]]; then
        cmake -S "$src" -B "$src/build" -DCMAKE_BUILD_TYPE=Release \
              -DCMAKE_CXX_FLAGS="-I$work/shim -I$work/lv2old-shim -I$work/lv2-old" >/dev/null
        cmake --build "$src/build" --target ChowCentaur_LV2 -j3 >/dev/null
    fi

    render "$name" "$so" "$@"
}

# The settings here must match the smoke test's (runPedalsSection, "Ported
# pedals sound like the originals"). Ports are the original's LV2 port
# numbers; "2=1" on the GxPlugins with a BYPASS port turns the pedal on.
gx_pedal yellow_drive  GxSD1.lv2             706407c0fecb9525ec87b4f2f23a8c1870701687 gx_sd1sim       in=1 out=0  2=1 3=0.7 4=-6 5=650
gx_pedal stack_drive   GxGuvnor.lv2          bf908c2e6f2c396f745cf3b8d07fb4dec4c0e96c gx_guvnor       in=1 out=0  2=0.5 3=0.7 4=0.5 5=0.6 6=0.5
gx_pedal hot_plate     GxHotBox.lv2          99098c026cc09cdea5bc312a06830ee1b270a38c gx_hotbox       in=1 out=0  2=1 3=0.5 4=0.6 5=0.6 6=0.3
gx_pedal tube_drive    GxBottleRocket.lv2    2db1f0e91c6caa446068e9bdae072f6fe3c52a12 gx_bottlerocket in=1 out=0  2=1 3=0.5 4=0.6 5=0.6 6=0.5
gx_pedal mellow_drive  GxEternity.lv2        bae424274b92dab90f62eb58323dde29f797c841 gx_eternity     in=1 out=0  2=1 3=0.6 4=0.5 5=0.5
gx_pedal sun_fuzz      GxSunFace.lv2         329f0e37311da238b1df2eb113f6960601f930bf gx_SunFace      in=1 out=0  2=1 3=0.7 4=0.5 5=0.5
gx_pedal vintage_fuzz  GxSuppaToneBender.lv2 44fb845ff433eb9828492413e09ad7da3a3bb233 gx_vstb         in=1 out=0  2=0.7 3=0.5 4=0.5
gx_pedal super_fuzz    GxSuperFuzz.lv2       eda1d498e71605d398781153c71ef2b373d72814 gx_sfp          in=1 out=0  2=0.5 3=0.6 4=0 5=0.25
gx_pedal envelope_wah  GxQuack.lv2           f77734abe6bcf899f14dcfde5094d6a415a833f0 gx_quack        in=1 out=0  2=1 3=0.8 4=0 5=0 6=2 7=8 8=1 9=0
gx_pedal slow_swell    GxSlowGear.lv2        20866563995ea1281b0ffd56b8ea248017e7fff7 gx_slowgear     in=1 out=0  2=5 3=1 4=200

guitarix_plugin jet_flanger  gx_flanger  in=7 out=6  0=1 1=5 2=0.5 3=-0.707 4=100 5=0
guitarix_plugin spring_tank  gx_reverb   in=7 silent=8 out=5 unused=6  0=50 1=0.2 2=0.6 3=0.4 4=0

klon golden_boost  atom=0 1=0 2=0 in=3 silent=4 out=5 unused=6  7=0.6 8=0.6 9=0.5 10=0 11=0 12=0
