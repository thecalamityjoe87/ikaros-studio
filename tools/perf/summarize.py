#!/usr/bin/env python3
"""Markdown tables from tools/perf/run_matrix.sh results (for docs/perf-report.md).

    python3 tools/perf/summarize.py ~/ikaros-perf/runs/matrix [more run folders...]
    python3 tools/perf/summarize.py --plugins ~/ikaros-perf/runs/features/effect-each.plugins.csv
    python3 tools/perf/summarize.py --threads ~/ikaros-perf/runs/threads [more run folders...]

The first form prints one row per run from each folder's summary.csv; the
second, each plug-in's average and peak share of the audio thread from a
per-plug-in CSV, grouped by the harness's label; the third, one row per run
(by run name) with where the audio thread and Tracktion's render workers ran.
"""

import collections
import csv
import os
import sys


def num(row, key, places=1):
    try:
        return ("%." + str(places) + "f") % float(row[key])
    except (KeyError, ValueError, TypeError):
        return "-"


def summary(folders):
    print("| Run | Tracks | Rate / buffer | Callback avg % | p99 % | max % | Over budget | Device xruns "
          "| Meter avg % | UI thread % | Paint ms/s | Memory MB (start→end) | RT |")
    print("|---|---|---|---|---|---|---|---|---|---|---|---|---|")

    for folder in folders:
        path = os.path.join(folder, "summary.csv")
        if not os.path.exists(path):
            continue
        for r in csv.DictReader(open(path)):
            if r.get("seconds", "") == "":
                print("| %s | | | **%s** | | | | | | | | | |" % (r["label"], r.get("actions", "failed")))
                continue
            label = r["label"] + (" + " + r["actions"] if r.get("actions") else "")
            rate = "%s / %s" % (num(r, "sample_rate", 0)[:-3] + "k" if r["sample_rate"] else "-", r["buffer"])
            print("| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s→%s | %s |" % (
                label, r["tracks"], rate, num(r, "cb_avg_pct"), num(r, "cb_p99_pct"), num(r, "cb_max_pct"),
                r["over_budget"], r["device_xruns"], num(r, "juce_meter_avg"), num(r, "ui_cpu"),
                num(r, "paint_ms_per_s"), num(r, "rss_start_mb", 0), num(r, "rss_end_mb", 0),
                "yes" if r["always_rt"] == "1" else "**no**"))


def threads(folders):
    print("| Run | Callback avg % | p99 % | max % | Over budget | Device xruns | Slow-core callbacks % "
          "| Audio RT | Workers | Workers RT | Worker CPU % | Worker cores |")
    print("|---|---|---|---|---|---|---|---|---|---|---|---|")

    for folder in folders:
        path = os.path.join(folder, "summary.csv")
        if not os.path.exists(path):
            continue
        for r in csv.DictReader(open(path)):
            name = r.get("csv", "").removesuffix(".csv") or r["label"]
            if r.get("seconds", "") == "":
                print("| %s | **%s** | | | | | | | | | | |" % (name, r.get("actions", "failed")))
                continue
            workers = r.get("workers") or "0"
            print("| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |" % (
                name, num(r, "cb_avg_pct"), num(r, "cb_p99_pct"), num(r, "cb_max_pct"), r["over_budget"],
                r["device_xruns"], num(r, "slow_core_pct"), "yes" if r["always_rt"] == "1" else "**no**",
                workers, "-" if workers == "0" else ("yes" if r.get("workers_always_rt") == "1" else "**no**"),
                num(r, "workers_cpu"), r.get("worker_cores") or "-"))


def plugins(path):
    rows = list(csv.DictReader(open(path)))
    by_label = collections.defaultdict(lambda: collections.defaultdict(list))

    for r in rows:
        by_label[r["label"]][(r["track"], r["plugin"])].append((float(r["avg_pct"]), float(r["max_pct"])))

    print("| Label | Plug-in | avg % of buffer | peak % |")
    print("|---|---|---|---|")
    for label, plugs in by_label.items():
        for (track, plugin), values in sorted(plugs.items(), key=lambda kv: -sum(v[0] for v in kv[1])):
            avg = sum(v[0] for v in values) / len(values)
            peak = max(v[1] for v in values)
            if peak >= 0.1:
                print("| %s | %s / %s | %.2f | %.2f |" % (label, track, plugin, avg, peak))


if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[1] == "--plugins":
        plugins(sys.argv[2])
    elif len(sys.argv) > 2 and sys.argv[1] == "--threads":
        threads(sys.argv[2:])
    else:
        summary(sys.argv[1:])
