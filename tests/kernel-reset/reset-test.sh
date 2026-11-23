#!/bin/bash
# Patch K check: HIP + rocBLAS before and after a requested GPU reset (MODE2).
# Run as root on the host (debugfs), with the patched amdgpu.ko loaded, HWS (default) and amdgpu.noretry=1.
# Needs vadd and gemm from ../hip built for gfx90c. Never run this with amdgpu.sched_policy=2 (NO_HWS).
#
# Without the patch: the reset reports success, but the next HIP program hangs and dmesg shows
#   "qcm fence wait loop timeout expired" / "DQM create queue type 0 failed. ret -62", followed by another reset.
# With the patch: both test rounds pass; with dynamic debug enabled (done below) dmesg shows
# "deactivated N active compute HQDs before MODE2" (on a 5600G: 9 = 8 KCQs + HIQ).
set -u
HIP_DIR=${HIP_DIR:-$(dirname "$0")/../hip}
[ "$(id -u)" = 0 ] || { echo "run as root (debugfs)"; exit 2; }
if [ -z "${GPU:-}" ]; then f=$(grep -l amdgpu /sys/kernel/debug/dri/*/name 2>/dev/null | head -1)
  [ -n "$f" ] || { echo "no amdgpu device in /sys/kernel/debug/dri (debugfs mounted?)"; exit 2; }
  GPU=$(basename "$(dirname "$f")"); fi
REC=/sys/kernel/debug/dri/$GPU/amdgpu_gpu_recover
[ -e "$REC" ] || { echo "$REC missing"; exit 2; }
runtests() { "$HIP_DIR/vadd" && "$HIP_DIR/gemm" 1024 2048; }
echo "== before reset";  runtests || { echo "tests fail before the reset - stop"; exit 1; }
echo "file gfx_v9_0.c +p" > /sys/kernel/debug/dynamic_debug/control 2>/dev/null || true
n=$(dmesg | wc -l)   # only show kernel messages from this run
echo "== requesting GPU reset on dri/$GPU"
timeout 60 cat "$REC" || { echo "reset request failed"; exit 1; }
sleep 5
msg=$(dmesg | tail -n +$((n + 1)))
echo "$msg" | grep -Ei "mode2|reset|hqd|qcm|preempt" | tail -20
echo "$msg" | grep -qi "reset" || { echo "RESULT: FAIL (no reset seen in dmesg)"; exit 1; }
echo "== after reset";   runtests && echo "RESULT: OK (KFD usable after reset)" || { echo "RESULT: FAIL"; exit 1; }
