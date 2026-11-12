#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""CI helper: in a full clone of the SDK V1.2.0 branch, put every pinned file (tools/ble_libs.py PINS, SOURCE_PINS)
back to the version whose sha256 matches, taking each from whichever commit has it.  usage: sdk_pin_restore.py SDKDIR"""
import hashlib, subprocess, sys
sys.path.insert(0, __file__.rsplit("/", 1)[0])
import ble_libs
sdk = sys.argv[1]
def git(*a):
    return subprocess.run(["git", "-C", sdk, *a], capture_output=True).stdout
miss = 0
for table in (ble_libs.PINS, ble_libs.SOURCE_PINS):
    for rel, pin in table.items():
        found = None
        for c in git("rev-list", "--all", "--", rel).decode().split():
            if hashlib.sha256(git("show", f"{c}:{rel}")).hexdigest() == pin:
                found = c; break
        if found:
            subprocess.run(["git", "-C", sdk, "checkout", "-q", found, "--", rel], check=True)
        else:
            miss += 1
        print(f"{'ok  ' if found else 'MISS'} {rel} {found or ''}")
sys.exit(1 if miss else 0)
