#!/bin/bash
# Test Mesa merge requests on RADV: build libvulkan_radeon.so from Mesa main without / with one MR each, then run the
# complete Loom Vulkan suite (cts-run.sh) and llama-bench (Vulkan) with each build. Result: $R/result.txt
# Usage: MESA=<mesa checkout with a configured build/ dir> LOOM_BUILD=... LLAMA_BENCH=<path> MODEL=<gguf> \
#        mesa-mr-round.sh <mr> [<mr>...]
# The baseline is the commit checked out in $MESA (check out main first). MRs that do not apply are reported; test them on their own base instead (mesa-mr-own-base.sh).
set -u
here=$(cd "$(dirname "$0")" && pwd)
R=$(realpath -m "${R:-$PWD/radv-round}"); M=${MESA:?}; : "${LOOM_BUILD:?}" "${LLAMA_BENCH:?}" "${MODEL:?}"; E=$R/result.txt; mkdir -p "$R"
printf %s "{\"ICD\":{\"api_version\":\"1.4.363\",\"library_path\":\"$R/libvulkan_radeon.so\"},\"file_format_version\":\"1.0.1\"}" > "$R/icd.json"
export VK_DRIVER_FILES=$R/icd.json
bench() { ${GPU_GUARD:-} "$LLAMA_BENCH" -m "$MODEL" -ngl 99 -fa 1 -p 512 -n 128 -r 3 -o csv 2>/dev/null | grep -E '^"' | tail -2 | awk -F, '{print $(NF-1)}' | tr -d '"' | tr '\n' ' '; }
run() { v=$1; cp "$R/libvulkan_radeon.$v.so" "$R/libvulkan_radeon.so"; sleep 61   # let the GPU cool down
  d=$(OUT=$R "$here/cts-run.sh" 2>&1 | tail -1 | awk '{print $2}')
  s=$(cat "$d/summary.txt")
  echo "$v | $d | $s | llama pp512/tg128 t/s: $(bench)" | tee -a "$E"; }
build() { nice ninja -C build src/amd/vulkan/libvulkan_radeon.so; }
: > "$E"
cd "$M"; git checkout -q -f HEAD; git clean -qfd -e build; build > /dev/null && cp build/src/amd/vulkan/libvulkan_radeon.so "$R/libvulkan_radeon.main.so"
for mr in "$@"; do
  cd "$M"; git checkout -q -f HEAD; git clean -qfd -e build
  curl -s -o "$R/mr$mr.diff" "https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/$mr.diff"
  if git apply --3way "$R/mr$mr.diff" > "$R/mr$mr.apply.log" 2>&1 && build > "$R/mr$mr.build.log" 2>&1; then
    cp build/src/amd/vulkan/libvulkan_radeon.so "$R/libvulkan_radeon.mr$mr.so"
  else echo "mr$mr | does not apply/build on main (see mr$mr.*.log)" | tee -a "$E"; fi
done
cd "$M"; git checkout -q -f HEAD; git clean -qfd -e build
run main
for mr in "$@"; do [ -f "$R/libvulkan_radeon.mr$mr.so" ] && run "mr$mr"; done
run main   # second baseline run shows the noise
echo END >> "$E"
