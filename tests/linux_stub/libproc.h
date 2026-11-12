/* SPDX-License-Identifier: GPL-3.0-only. Linux stand-in for macOS <libproc.h> so tests/drum_host.h compiles in CI: no instruction counter (instr_now returns 0). */
#include <stdint.h>
#include <sys/types.h>
typedef void *rusage_info_t;
#define RUSAGE_INFO_V4 4
struct rusage_info_v4 { uint64_t ri_instructions, ri_cycles; };
static inline int proc_pid_rusage(int pid, int flavor, rusage_info_t *ri) { (void)pid; (void)flavor; (void)ri; return -1; }
