#!/bin/bash
# A/B: complete Loom Vulkan runs with several builds of the same Mesa tree (e.g. 26.3.1 without fix, with !46557,
# with !46521, with both). Each build is a file libvulkan_radeon.<name>.so in $W. The script copies each one over
# $MESA_PREFIX/lib/libvulkan_radeon.so in turn (use a private Mesa install prefix, not the system one).
# Usage: W=<dir with the .so files> MESA_PREFIX=<mesa install prefix> LOOM_BUILD=... ab-driver-builds.sh orig mr46557 ...
set -u
here=$(cd "$(dirname "$0")" && pwd)
W=$(realpath -m "${W:?}"); I=${MESA_PREFIX:?}; : "${LOOM_BUILD:?}"
export VK_DRIVER_FILES=$I/share/vulkan/icd.d/radeon_icd.x86_64.json
for v in "$@"; do
  cp "$W/libvulkan_radeon.$v.so" "$I/lib/libvulkan_radeon.so"
  sleep 61
  echo "$v $(OUT=$W "$here/cts-run.sh" 2>&1 | tail -1)" | tee -a "$W/ab-runs.txt"
done
