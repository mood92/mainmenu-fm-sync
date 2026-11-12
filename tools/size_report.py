#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Where the bytes go: code, static RAM and pool of a built felucca.elf, by feature and by biggest symbols.
Usage: size_report.py build/felucca.elf
Groups are guessed from symbol names (the C++ ones demangled), so they are approximate."""
import re
import subprocess
import sys

elf = sys.argv[1] if len(sys.argv) > 1 else "build/felucca.elf"


def run(*a):
    return subprocess.run(a, capture_output=True, text=True).stdout


secs = {}
for m in re.finditer(r"^\s*\[\s*(\d+)\]\s+(\S+)\s+\S+\s+[0-9a-f]+ [0-9a-f]+ ([0-9a-f]+)", run("readelf", "-SW", elf), re.M):
    secs[m.group(1)] = (m.group(2), int(m.group(3), 16))

syms = []
for line in run("readelf", "-sW", elf).splitlines():
    f = line.split()
    if len(f) < 8 or not f[0].endswith(":") or f[3] not in ("FUNC", "OBJECT"):
        continue
    size = int(f[2], 16) if f[2].startswith("0x") else int(f[2])
    if size and f[6] in secs:
        syms.append((secs[f[6]][0], size, f[7]))

dem = run("c++filt", *[]) if False else None
names = "\n".join(s[2] for s in syms)
dem = subprocess.run(["c++filt"], input=names, capture_output=True, text=True).stdout.splitlines()
if len(dem) != len(syms):
    dem = [s[2] for s in syms]
syms = [(a, b, d) for (a, b, _), d in zip(syms, dem)]

GROUPS = [
    ("bluetooth (ble_/bt_)", r"^(ble_|bt_|hci|l2cap|gatt|sm_|att_|fml|fmlink)"), ("drum models (dm_)", r"^dm_|dmodels"),
    ("sequencer/song", r"^(seq|song|arr|pattern|perform|motion|lockbank)"), ("editor/usb/ota", r"^(editor|usb|ota|uac|midi_|project)"),
    ("ui/lcd/gfx/pages", r"^(pages|lcd|gfx|panel|font|icon|led_|enc|console)"), ("fx/filter/comp/dsp", r"^(fx|filter|comp|dsp|reson|lfo|insert|cue|phys|slicer|grids)"),
    ("samples/presets/tables", r"sample|preset|table|wave|kit"), ("libc/runtime", r"^(libc|mem|str|__|_)"),
]


def group(name):
    n = name.lower()
    for g, rx in GROUPS:
        if re.search(rx, n):
            return g
    return "other"


print(f"sections: " + ", ".join(f"{n} {s}" for n, s in secs.values() if n in ('.text', '.data', '.bss', '.pool')))
for sec in (".text", ".data", ".bss", ".pool"):
    rows = [s for s in syms if s[0] == sec]
    total = sum(s[1] for s in rows)
    print(f"\n== {sec}: {total} B in sized symbols; by group")
    agg = {}
    for _, size, name in rows:
        g = group(name)
        agg[g] = agg.get(g, 0) + size
    for g, v in sorted(agg.items(), key=lambda kv: -kv[1]):
        print(f"  {v:8d}  {g}")
    print(f"  -- biggest 40 symbols in {sec}")
    for _, size, name in sorted(rows, key=lambda s: -s[1])[:40]:
        print(f"  {size:8d}  {name[:90]}")
