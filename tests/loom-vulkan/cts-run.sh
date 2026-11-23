#!/bin/bash
# Run every Loom/IREE Vulkan test (ctest -R vulkan) on its own and summarize ok / failed / not built.
# Usage: LOOM_BUILD=<hrx-system>/build/cmake [OUT=<dir>] [VK_DRIVER_FILES=<icd.json>] cts-run.sh
# Result: $OUT/cts-<date>/{head.txt,result.tsv,logs/<n>.log}; the last line prints the result directory.
# Optional: GPU_GUARD=<wrapper> runs each test through a wrapper (we used one that waits for an idle GPU and
# enforces a timeout, exit 75 = busy, retried up to 6 times).
set -u
B=${LOOM_BUILD:?set LOOM_BUILD to the hrx-system cmake build directory}
A=$(realpath -m "${OUT:-$PWD}")/cts-$(date +%Y%m%d-%H%M)
mkdir -p "$A/logs"
cd "$B"
export LD_LIBRARY_PATH=   # pure Vulkan run, no ROCm libraries
ctest -N -R vulkan | sed -nE 's/^ *Test +#[0-9]+: (.*)$/\1/p' > "$A/tests.txt"
echo "$(wc -l < "$A/tests.txt") tests, hrx-system $(git -C .. rev-parse --short HEAD)" | tee "$A/head.txt"
vulkaninfo --summary 2>/dev/null | grep -E "driverVersion|deviceName|apiVersion" >> "$A/head.txt"
n=0
while read -r t; do
  n=$((n + 1))
  for try in 1 2 3 4 5 6; do
    ${GPU_GUARD:-} ctest -R "^${t//+/\\+}\$" --timeout 240 --output-on-failure > "$A/logs/$n.log" 2>&1
    rc=$?
    [ $rc -ne 75 ] && break
    sleep 60
  done
  if grep -q "100% tests passed" "$A/logs/$n.log"; then s=ok
  elif grep -q "Not Run\|Could not find executable" "$A/logs/$n.log"; then s=not_built
  elif grep -q "\*\*\*Timeout" "$A/logs/$n.log"; then s=timeout
  elif [ $rc -eq 75 ]; then s=gpu_busy
  else s=failed; fi
  printf '%s\t%s\t%s\n' "$n" "$s" "$t" >> "$A/result.tsv"
done < "$A/tests.txt"
cut -f2 "$A/result.tsv" | sort | uniq -c | tee -a "$A/head.txt"
# one fixed summary line for the callers
printf "summary:" > "$A/summary.txt"; for s in ok failed not_built timeout gpu_busy; do printf " %s %s" "$(cut -f2 "$A/result.tsv" | grep -cx $s)" $s >> "$A/summary.txt"; done; echo >> "$A/summary.txt"
echo "done $A"
