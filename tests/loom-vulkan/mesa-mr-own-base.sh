#!/bin/bash
# For MRs that do not apply to Mesa main: build the MR's base commit and the MR head, run both like mesa-mr-round.sh.
# Same environment as mesa-mr-round.sh (MESA, LOOM_BUILD, LLAMA_BENCH, MODEL, optional R and GPU_GUARD).
# Usage: mesa-mr-own-base.sh <mr> [<mr>...]   (leaves $MESA on a detached commit; check out main afterwards)
set -u
here=$(cd "$(dirname "$0")" && pwd)
R=$(realpath -m "${R:-$PWD/radv-round}"); M=${MESA:?}; : "${LOOM_BUILD:?}" "${LLAMA_BENCH:?}" "${MODEL:?}"; E=$R/result.txt; mkdir -p "$R"
printf %s "{\"ICD\":{\"api_version\":\"1.4.363\",\"library_path\":\"$R/libvulkan_radeon.so\"},\"file_format_version\":\"1.0.1\"}" > "$R/icd.json"
export VK_DRIVER_FILES=$R/icd.json
bench() { ${GPU_GUARD:-} "$LLAMA_BENCH" -m "$MODEL" -ngl 99 -fa 1 -p 512 -n 128 -r 3 -o csv 2>/dev/null | grep -E '^"' | tail -2 | awk -F, '{print $(NF-1)}' | tr -d '"' | tr '\n' ' '; }
run() { v=$1; cp "$R/libvulkan_radeon.$v.so" "$R/libvulkan_radeon.so"; sleep 61
  d=$(OUT=$R "$here/cts-run.sh" 2>&1 | tail -1 | awk '{print $2}')
  s=$(cat "$d/summary.txt")
  echo "$v | $d | $s | llama pp512/tg128 t/s: $(bench)" | tee -a "$E"; }
build_at() { name=$1 sha=$2
  cd "$M"; git fetch -q origin "$sha" || return 1
  git checkout -q -f "$sha" && git clean -qfd -e build && nice ninja -C build src/amd/vulkan/libvulkan_radeon.so > "$R/$name.build.log" 2>&1 \
    && cp build/src/amd/vulkan/libvulkan_radeon.so "$R/libvulkan_radeon.$name.so"; }
echo "--- own base: base commit vs. MR head" >> "$E"
for mr in "$@"; do
  cd "$M"; git fetch -q origin "refs/merge-requests/$mr/head" || { echo "mr$mr | fetch failed" | tee -a "$E"; continue; }
  head=$(git rev-parse FETCH_HEAD)
  base=$(curl -s "https://gitlab.freedesktop.org/api/v4/projects/mesa%2Fmesa/merge_requests/$mr" | python3 -c 'import json,sys;print(json.load(sys.stdin)["diff_refs"]["base_sha"])')
  if build_at "base$mr" "$base" && build_at "mr$mr" "$head"; then run "base$mr"; run "mr$mr"
  else echo "mr$mr | build on own base failed" | tee -a "$E"; fi
done
echo END >> "$E"
