# Group sync for M-VAVE FM-1 firmware (mainmenu-fm-sync)

A fork of DeadActive's Hortator (GPL-3.0, built on Felucca by Leo Kuroshita), used to grow FM-Link from
two units to a group: one leader, several followers, all locked to one transport over Bluetooth.
Goal: 3 to 6 FM-1s at the park, plugged into speakers, one leader sets the transport and tempo, everyone
else follows. Later, the same protocol goes into KARMA so Hortator and KARMA units can share a group.

Credit: FM-Link and the Bluetooth stack glue are DEADACTIVE's work (see README, LICENSING.md). Keep the
credits when reusing the code.

## Baseline (verified on real hardware, 2026-11-11)

- Two FM-1s with Hortator 1.0.1 Bluetooth build, FM-Link on: leader PLAY starts both, STOP stops both,
  tempo follows, metronomes stay locked, stable.
- Follower PLAY / STOP / tempo do nothing (`FOLLOWING LINK`). Leader-only transport is fine for this project.
- One unit first showed `FAILED NO RF TRIM IN FLASH`. Restoring official M-VAVE V15 with M-UPGRADE and
  reinstalling the Bluetooth build fixed it. Every unit in a group must pass this check.

## What the code does today

- The Bluetooth stack is configured for up to 3 links at once: 2 as central, 1 as peripheral
  (`firmware/src/ble/sdkcfg/app_config.h`: `TRANS_MULTI_BLE_MASTER_NUMS 2`, `TRANS_MULTI_BLE_SLAVE_NUMS 1`).
- The host role has two fixed slots: slot 0 a controller (BLE-MIDI), slot 1 one other FM-1 (FM-Link)
  (`ble_hslot_t s[2]` and the loops in `ble_host_step`, `firmware/src/ble/ble_central.c`).
- ROLE is HOST (scans, connects) or DEVICE (advertises). A follower is a DEVICE with one link to its host.
- The FM-Link engine (`firmware/src/fmlink.c`) is pure code with one peer: one offset estimate, one PONG
  due, one `seen` time. Glue is `firmware/src/bt_fml.c`; messages go out through `ble_fml_send` with no
  per-link addressing.
- Radio calibration record is required (`ble_vm.c`).
- Each failed connect holds interrupts off about 12 ms and can make audio late. RAM is tight (pool headroom
  about 21 KB in the FIT notes, `docs/ble/FIT.md`).

## Design: one leader, N followers (star)

The leader is ROLE HOST with N FM-1 slots (instead of one). Each follower is ROLE DEVICE with a single link
to the leader, exactly as now. Followers need no change in role handling.

Needed changes:
1. Link count: raise the central link count in `app_config.h` and the host slot arrays; keep the
   controller slot. Check what the stack and RAM allow (compile and measure with `BLE_MEASURE=1`).
2. Host logic: generalize `s[2]` / `for (i < 2)` in `ble_host_step` and its glue to N FM-1 slots plus the
   controller slot; saved-device list already holds 8.
3. Engine: leader keeps per-follower state (PING / PONG reply addressed to the link the PING came from,
   TIME / STATE beacons sent to every link). Follower side stays as is.
4. Page: the Bluetooth page lists several connected FM-1s; top line shows the count.
5. Tests: extend `tests/fmlink_test.c` so one leader and up to 6 simulated followers run against a
   simulated link; check lock, join late, drop and rejoin, tempo change.

## Open risks

- The prebuilt stack may cap central links below N. Unknown until built and run on hardware.
- Several simultaneous links raise connection-event load and may add timing jitter or audio stutter.
- RAM: each link costs heap; the build must still fit.
- Hardware testing needs 3 or more real FM-1s. Without them, only the simulated tests can be run.

## Later: KARMA

`fmlink.c` is plain code with no hardware calls. Plan: vendor it plus the Bluetooth glue into KARMA once the
group protocol is settled, hooked into KARMA's sequencer clock path (`G_CLOCK`, `seq.c`). KARMA's own repo is
not touched until then.
