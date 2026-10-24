#!/usr/bin/env python3
"""Turns the raw stacks from sampler.so and rtcheck.so into readable reports.

    python3 tools/perf/symbolize.py prof.txt [--top 25] [--thread NAME]

sampler.so output: per thread, its share of the samples and the functions
it spent its time in, by self time (the innermost frame) and inclusive time
(anywhere on the stack).
rtcheck.so output: what the audio thread did that it shouldn't, by kind, with
the call stacks that did it.

Addresses are looked up with addr2line (binutils), one call per binary.
"""

import argparse
import bisect
import collections
import os
import subprocess
import sys


def read(path):
    maps, threads, samples, stacks, header = [], {}, [], [], []

    with open(path, errors="replace") as f:
        for line in f:
            kind, _, rest = line.rstrip("\n").partition(" ")

            if kind == "map":
                parts = rest.split(None, 5)
                if len(parts) == 6 and parts[1][2] == "x" and parts[5].startswith("/"):
                    start, end = (int(x, 16) for x in parts[0].split("-"))
                    maps.append((start, end, int(parts[2], 16), parts[5]))
            elif kind == "thread":
                tid, _, name = rest.partition(" ")
                threads[int(tid)] = name.strip()
            elif kind == "sample":
                fields = rest.split()
                samples.append((int(fields[0]), [int(a, 16) for a in fields[1:]]))
            elif kind == "stack":
                fields = rest.split()
                stacks.append((fields[0], [int(a, 16) for a in fields[1:]]))
            else:
                header.append(line.rstrip("\n"))

    maps.sort()
    return header, maps, threads, samples, stacks


class Symbolizer:
    def __init__(self, maps):
        self.maps = maps
        self.starts = [m[0] for m in maps]
        self.cache = {}

    def locate(self, address):
        i = bisect.bisect_right(self.starts, address) - 1
        if i >= 0 and self.maps[i][0] <= address < self.maps[i][1]:
            start, _, offset, path = self.maps[i]
            return path, address - start + offset
        return None, address

    def resolve(self, addresses):
        # Return addresses point just after the call; step back one byte so
        # the line found is the call itself.
        wanted = collections.defaultdict(set)
        for a in addresses:
            if a not in self.cache:
                path, offset = self.locate(a)
                if path is None:
                    self.cache[a] = "?? 0x%x" % a
                else:
                    wanted[path].add((a, max(0, offset - 1)))

        for path, items in wanted.items():
            items = sorted(items)
            if not os.path.exists(path):
                for a, _ in items:
                    self.cache[a] = os.path.basename(path)
                continue
            out = subprocess.run(["addr2line", "-f", "-C", "-e", path] + ["0x%x" % o for _, o in items],
                                 capture_output=True, text=True).stdout.split("\n")
            for k, (a, _) in enumerate(items):
                name = out[2 * k] if 2 * k < len(out) else "??"
                if name in ("??", ""):
                    name = "%s+0x%x" % (os.path.basename(path), self.locate(a)[1])
                self.cache[a] = shorten(name)

        return [self.cache[a] for a in addresses]


def shorten(name):
    """Drops argument lists and long template arguments, which make the report unreadable."""
    depth, out = 0, []
    for ch in name:
        if ch in "<(":
            if depth == 0:
                out.append("<>" if ch == "<" else "()")
            depth += 1
        elif ch in ">)":
            depth = max(0, depth - 1)
        elif depth == 0:
            out.append(ch)
    return "".join(out).replace("()()", "()")


def report_samples(header, threads, samples, sym, top, only):
    all_addresses = sorted({a for _, frames in samples for a in frames})
    names = dict(zip(all_addresses, sym.resolve(all_addresses)))
    by_thread = collections.defaultdict(list)

    for tid, frames in samples:
        by_thread[tid].append([names[a] for a in frames])

    total = len(samples)
    interval = next((int(h.split()[1]) for h in header if h.startswith("interval_us")), 1000)
    print("%d samples, one per %d us of a thread's CPU time\n" % (total, interval))

    for tid, stacks in sorted(by_thread.items(), key=lambda kv: -len(kv[1])):
        name = threads.get(tid, "tid %d" % tid)
        if only and only not in name:
            continue
        if len(stacks) < total * 0.005 and not only:
            continue

        print("=== %s (tid %d): %d samples = %.1f s of CPU, %.1f%% of all" %
              (name, tid, len(stacks), len(stacks) * interval / 1e6, 100.0 * len(stacks) / total))

        self_counts = collections.Counter(s[0] for s in stacks if s)
        inclusive = collections.Counter()
        for s in stacks:
            inclusive.update(set(s))

        print("  self:")
        for fn, n in self_counts.most_common(top):
            print("    %5.1f%%  %s" % (100.0 * n / len(stacks), fn))
        print("  inclusive:")
        for fn, n in inclusive.most_common(top):
            print("    %5.1f%%  %s" % (100.0 * n / len(stacks), fn))
        print()


def report_rtcheck(header, stacks, sym, top):
    for h in header:
        print(h)
    print()

    all_addresses = sorted({a for _, frames in stacks for a in frames})
    names = dict(zip(all_addresses, sym.resolve(all_addresses)))
    groups = collections.Counter()

    for event, frames in stacks:
        # The first frames that are ours or the engine's say more than libc's.
        chain = [names[a] for a in frames[:10]]
        groups[(event, " <- ".join(chain[:7]))] += 1

    print("Sampled stacks (all events until the buffer was half full, then 1 in 64):\n")
    for (event, chain), n in groups.most_common(top):
        print("%5d  %-10s %s\n" % (n, event, chain))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("file")
    parser.add_argument("--top", type=int, default=25)
    parser.add_argument("--thread", default="")
    args = parser.parse_args()

    header, maps, threads, samples, stacks = read(args.file)
    sym = Symbolizer(maps)

    if samples:
        report_samples(header, threads, samples, sym, args.top, args.thread)
    if stacks or any(h.startswith("callbacks") for h in header):
        report_rtcheck(header, stacks, sym, args.top)


if __name__ == "__main__":
    sys.exit(main())
