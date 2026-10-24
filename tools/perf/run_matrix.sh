#!/usr/bin/env bash
# Runs Ikaros Studio's stress scenarios headless and collects the numbers
# (see docs/perf-report.md and src/app/PerfHarness.h).
#
#   tools/perf/run_matrix.sh [build-dir] [what]
#
#   build-dir  default build-perf (RelWithDebInfo; a Debug build's numbers
#              are 3-10x worse and say little about real use)
#   what       quick | matrix | features | ui | overload | cores | threads |
#              rtcheck | profile | endurance | one
#              (default quick). "one" runs a single scenario given by
#              SCENARIO="realistic" ARGS="--perf-buffer=256 ...".
#
# Nothing is heard and no microphone is used: a private HOME holds an
# .asoundrc that sends the app's ALSA output to a PipeWire null sink and takes
# its input from a silent virtual source (both created here and left for
# later runs; remove them with tools/perf/run_matrix.sh cleanup).
# The app runs under Xvfb, so no window appears on the desktop.
#
# Results go to $IKAROS_PERF_OUT (default ~/ikaros-perf/runs/<date-time>):
#   summary.csv       one line per scenario run
#   <run>.csv         per second: callback %, xruns, CPU per thread, memory, paint
#   <run>.plugins.csv per second: each plug-in's share of the audio thread
#   <run>.threads.csv per second: CPU per thread (like top -H)
#   <run>.log         the app's output

set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${1:-build-perf}"
WHAT="${2:-quick}"

if [ "$BUILD" = "cleanup" ]; then
    pw-cli destroy ikaros-perf-null >/dev/null 2>&1
    pw-cli destroy ikaros-perf-src >/dev/null 2>&1
    exit 0
fi

case "$BUILD" in /*) ;; *) BUILD="$ROOT/$BUILD" ;; esac
APP="$(find "$BUILD/IkarosStudio_artefacts" -type f -name "Ikaros Studio" | head -1)"
[ -x "$APP" ] || { echo "No app in $BUILD"; exit 1; }

OUT="${IKAROS_PERF_OUT:-$HOME/ikaros-perf/runs/$(date +%Y%m%d-%H%M%S)}"
SOUNDS="${IKAROS_PERF_SOUNDS:-$HOME/ikaros-perf/sounds}"
APPHOME="${IKAROS_PERF_HOME:-$HOME/ikaros-perf/home}"
mkdir -p "$OUT" "$APPHOME"

# --- Silent audio -------------------------------------------------------------
make_node() {   # name media-class
    pw-cli ls Node 2>/dev/null | grep -q "node.name = \"$1\"" && return
    pw-cli create-node adapter "{ factory.name=support.null-audio-sink node.name=$1 media.class=$2
                                  audio.position=[FL FR] object.linger=true }" >/dev/null
}
make_node ikaros-perf-null Audio/Sink
make_node ikaros-perf-src Audio/Source/Virtual

cat > "$APPHOME/.asoundrc" <<'EOF'
# Stress runs only: play into a null sink, record from a silent source.
pcm.!default {
    type pipewire
    playback_node "ikaros-perf-null"
    capture_node "ikaros-perf-src"
    hint { show on description "Ikaros perf (silent)" }
}
ctl.!default { type pipewire }
EOF

# Third-party plug-ins the real user has (read-only: the scan only lists them).
export LV2_PATH="${LV2_PATH:-$HOME/.lv2:/usr/lib/lv2:/usr/local/lib/lv2}"

# The LD_PRELOAD helpers (tools/perf/*.c), built when needed.
TOOLS="$HOME/ikaros-perf/tools"
build_tools() {
    mkdir -p "$TOOLS"
    gcc -O2 -shared -fPIC -o "$TOOLS/rtcheck.so" "$ROOT/tools/perf/rtcheck.c" -ldl -lpthread || exit 1
    gcc -O2 -shared -fPIC -o "$TOOLS/sampler.so" "$ROOT/tools/perf/sampler.c" -ldl -lpthread -lrt || exit 1
}

# --- One run ------------------------------------------------------------------
run() {   # run-name scenario [harness options...]
    local name="$1" scenario="$2"; shift 2
    local xdg="$OUT/xdg/$name"
    rm -rf "$xdg"; mkdir -p "$xdg"

    echo "=== $name"
    env -u WAYLAND_DISPLAY -u DBUS_SESSION_BUS_ADDRESS \
        HOME="$APPHOME" XDG_DATA_HOME="$xdg" XDG_CONFIG_HOME="$APPHOME/.config" \
        IKAROS_PERF=1 IKAROS_SECOND_INSTANCE=1 IKAROS_PERF_CSV="$OUT/$name.csv" \
        IKAROS_USER_SOUNDS_DIR="$SOUNDS" \
        ${RUN_ENV:-} \
        timeout --signal=KILL "${RUN_TIMEOUT:-900}" \
        xvfb-run -a -s "-screen 0 1920x1080x24" ${RUN_PREFIX:-} "$APP" \
            --perf-scenario="$scenario" --perf-summary="$OUT/summary.csv" "$@" \
        > "$OUT/$name.log" 2>&1
    local status=$?

    if [ $status -ne 0 ]; then
        echo "    exit status $status (137 = killed); last lines:"
        tail -3 "$OUT/$name.log" | sed 's/^/    /'
        echo "$scenario,\"$name\",,,,,,,,,,,,,,,,,,,,,,,,,,,,,\"EXIT $status\"," >> "$OUT/summary.csv"
    fi

    grep "harness: result" "$OUT/$name.log" | tail -1 | cut -c1-200
    sleep 2   # let the previous instance's audio stream go away
}

rates() {   # scenario tracks seconds [extra...]: 48 kHz at 64-512, plus one 96 kHz run at 256
    local scenario="$1" tracks="$2" seconds="$3"; shift 3
    for buffer in 64 128 256 512; do
        run "$scenario-$tracks-48k-$buffer" "$scenario" --perf-tracks="$tracks" --perf-buffer="$buffer" \
            --perf-rate=48000 --perf-seconds="$seconds" "$@"
    done
    run "$scenario-$tracks-96k-256" "$scenario" --perf-tracks="$tracks" --perf-buffer=256 \
        --perf-rate=96000 --perf-seconds="$seconds" "$@"
}

case "$WHAT" in
    one)
        run "${SCENARIO:-realistic}" "${SCENARIO:-realistic}" ${ARGS:-} ;;
    quick)
        run realistic-48k-256 realistic --perf-buffer=256 --perf-rate=48000 --perf-seconds=32 ;;
    matrix)
        # 32 s = one pass of the 16-bar loop, so every run averages the same music.
        rates realistic 16 32
        for n in 8 16 32 64; do
            for s in audio synth sampled mix; do
                run "$s-$n-48k-256" "$s" --perf-tracks="$n" --perf-buffer=256 --perf-rate=48000 --perf-seconds=32
            done
        done
        rates mix 32 32
        rates mix 64 32 ;;
    features)
        run instrument-each instrument-each --perf-buffer=256 --perf-rate=48000 --perf-seconds=8 --perf-warmup=3
        run effect-each effect-each --perf-buffer=256 --perf-rate=48000 --perf-seconds=8 --perf-warmup=3
        for s in drummer tune effects-heavy sends automation; do
            run "$s-8-48k-256" "$s" --perf-tracks=8 --perf-buffer=256 --perf-rate=48000 --perf-seconds=15
        done
        run tempo-48k-256 tempo --perf-buffer=256 --perf-rate=48000 --perf-seconds=20
        run plugins3p-4-48k-256 plugins3p --perf-tracks=4 --perf-buffer=256 --perf-rate=48000 --perf-seconds=15 \
            ${PLUGIN:+--perf-plugin="$PLUGIN"}
        run idle-empty idle-empty --perf-buffer=256 --perf-rate=48000 --perf-seconds=15
        run idle-heavy idle-heavy --perf-buffer=256 --perf-rate=48000 --perf-seconds=15
        for a in playstop loop seek addremove plugins-addremove undo autosave tempo-changes record "record,monitor"; do
            run "actions-${a/,/-}" realistic --perf-buffer=256 --perf-rate=48000 --perf-seconds=20 --perf-actions="$a"
        done ;;
    ui)
        run ui-baseline realistic --perf-buffer=256 --perf-rate=48000 --perf-seconds=20
        for a in ui-scroll ui-zoom ui-mixer ui-resize ui-theme ui-library "ui-mixer,ui-scroll"; do
            run "${a/,/-}" realistic --perf-buffer=256 --perf-rate=48000 --perf-seconds=20 --perf-actions="$a"
        done
        for n in 8 32 64; do
            run "ui-mixer-scroll-$n" mix --perf-tracks="$n" --perf-buffer=256 --perf-rate=48000 --perf-seconds=20 \
                --perf-actions=ui-mixer,ui-scroll
        done ;;
    overload)
        # Burn from 0 to 200% of the buffer and back over a minute: the app must survive and recover.
        run overload-256 realistic --perf-buffer=256 --perf-rate=48000 --perf-seconds=60 --perf-burn-ramp=200
        run overload-64 realistic --perf-buffer=64 --perf-rate=48000 --perf-seconds=60 --perf-burn-ramp=200 ;;
    cores)
        # The same song with the whole app kept on one kind of core (Linux, hybrid CPUs:
        # see /sys/devices/cpu_core/cpus and cpu_atom/cpus; these are an Intel Core Ultra 7 155U's).
        for set in ${CORE_SETS:-0-3 4-11 12-13}; do
            RUN_PREFIX="taskset -c $set" run "cores-$set" realistic --perf-buffer=256 --perf-rate=48000 --perf-seconds=32
        done ;;
    threads)
        # Tracktion's render threads (Ikaros uses 1; see IkarosEngineBehaviour).
        for n in 1 2 4; do
            for buffer in 64 256; do
                RUN_ENV="IKAROS_PERF_AUDIO_THREADS=$n" run "threads-$n-mix32-$buffer" mix --perf-tracks=32 \
                    --perf-buffer="$buffer" --perf-rate=48000 --perf-seconds=32
            done
        done ;;
    rtcheck)
        # What the audio thread does that it shouldn't (allocations, locks, I/O); read with symbolize.py.
        build_tools
        for a in none addremove plugins-addremove undo record,monitor ui-mixer,ui-scroll; do
            RUN_ENV="LD_PRELOAD=$TOOLS/rtcheck.so IKAROS_RTCHECK_OUT=$OUT/rtcheck-${a/,/-}.txt" \
                run "rtcheck-${a/,/-}" realistic --perf-buffer=256 --perf-rate=48000 --perf-seconds=20 \
                    $([ "$a" != none ] && echo --perf-actions="$a")
        done ;;
    profile)
        # Where every thread spends its CPU time; read with symbolize.py.
        build_tools
        RUN_ENV="LD_PRELOAD=$TOOLS/sampler.so IKAROS_SAMPLER_OUT=$OUT/profile-playing.txt" \
            run profile-playing realistic --perf-buffer=256 --perf-rate=48000 --perf-seconds=32
        RUN_ENV="LD_PRELOAD=$TOOLS/sampler.so IKAROS_SAMPLER_OUT=$OUT/profile-ui.txt" \
            run profile-ui mix --perf-tracks=32 --perf-buffer=256 --perf-rate=48000 --perf-seconds=20 \
                --perf-actions=ui-mixer,ui-scroll
        RUN_ENV="LD_PRELOAD=$TOOLS/sampler.so IKAROS_SAMPLER_OUT=$OUT/profile-idle.txt" \
            run profile-idle idle-heavy --perf-buffer=256 --perf-rate=48000 --perf-seconds=20 ;;
    endurance)
        RUN_TIMEOUT=$(( ${ENDURANCE_SECONDS:-3600} + 600 )) \
            run endurance realistic --perf-buffer=256 --perf-rate=48000 --perf-seconds="${ENDURANCE_SECONDS:-3600}" ;;
    *)
        echo "unknown: $WHAT"; exit 1 ;;
esac

echo "Results: $OUT"
