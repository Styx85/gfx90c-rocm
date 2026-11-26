# ROCm on gfx90c (Vega iGPU of Ryzen 5000G APUs) – patches and findings

Patches and measurements that make ROCm (HIP, rocBLAS, PyTorch, llama.cpp-HIP) run **natively** on the Vega iGPU of
Cezanne/Renoir APUs (**gfx90c**, GCN 5, Wave64), without `HSA_OVERRIDE_GFX_VERSION`.

Tested on: Ryzen 5 5600G (Vega 7, 7 CUs), DDR4-3200, Proxmox VE kernel 7.0.14, Arch Linux container, ROCm 10.1
(TheRock), Mesa 26.2 / 26.3.1.

> **How this was made:** AI-assisted (Claude Code agents running a measure-change-measure loop on real hardware over
> three days, November 2026). Every result below was measured, but on one machine only. Shared as-is for people
> with the same hardware. Nothing here is official; what has gone upstream is listed in the status table.

## What was broken and what fixes it

| # | Component | Problem | Fix (folder) |
|---|---|---|---|
| R1 | CLR (`rocclr/device/device.cpp`) | ISA table lists gfx902/909/90c as xnack `ANY`, ROCr reports them xnack-unsupported → assertion in `amd::Isa::isCompatible` for every HIP/OpenCL program | align the table with ROCr (`patches/rocm-systems`, commit 1) – overlaps with ROCm/rocm-systems#13903 |
| R1b | HIP (`hip_fatbin.cpp`) | `gfx90c:xnack-` code objects in a fat binary are not found on an xnack-off agent (`hipErrorInvalidKernelFile`, silently wrong results in programs that don't check) | ask COMGR with the feature string (`patches/rocm-systems`, commit 2) |
| R2 | rocBLAS/Tensile | no Tensile library for gfx90c and no fallback to `gfx9-generic` | exact arch → LLVM generic family fallback (`patches/rocm-libraries/*/0001`) |
| R3 | rocBLAS | only the slow generic fallback kernels | gfx90c logic generated from vega10 (`make_gfx90c_logic.sh`), selection tables re-tuned on gfx90c (`0004`–`0006`), plus one oto-derived HHS_BH solution (`0007`) |
| R4 | Tensile | `ROCBLAS_TENSILE_GEMM_OVERRIDE_PATH` / solution index → `internal_error` (code object of preloaded libraries never loaded) – architecture-independent | `patches/rocm-libraries/develop/0002` |
| K | Linux amdgpu (GC 9.3.0) | after a GPU reset (MODE2) KFD is dead until reboot: the HIQ (and stuck HWS queues) stay active across MODE2 | deactivate all compute HQDs except the KIQ before MODE2 – `patches/linux/v2/` (one patch for all gfx9 APUs, as sent upstream; the older 2-patch series v1 is kept next to it); **applied upstream** by the amdgpu maintainer on 23 Nov 2026 (amd-staging-drm-next aecf8b416) |
| C | llama.cpp HIP backend | GCN5 has no dot instructions; FA spills, slow mmvq decode | 11 patches (`patches/llama.cpp-hip`) |
| L | Loom/HRX SPIR-V (Vulkan) | clustered f32 subgroup sums (XOR butterfly) slower than native `ClusteredReduce` on GFX9/Wave64 for cluster sizes ≥ 8: XOR 4/16 lower to `ds_swizzle` (LDS) instead of DPP | **fixed upstream** by ROCm/hrx-system#3467 (reported in #3455) |
| M1 | Mesa NIR (RADV) | `(x >>s 15) & 0x7fff` on 16-bit integers returns `0xffff`: the unsigned-upper-bound analysis treated 16-bit `ishr` sources as non-negative and dropped the mask (wrong f16/bf16 min/max in Loom) | **fixed upstream** in Mesa e7dedf078, included in **Mesa 26.3.1** |
| M2 | Mesa ACO (RADV) | VGPRs spilled to LDS (`ds_write/read_addtid_b32`) carry no memory sync info, so the scheduler can move a spill store above the reload of the same slot → silently wrong values under register pressure | reported in mesa/mesa#16952; **fixed upstream** by mesa/mesa!46557 (Georg Lehmann, Reviewed-by Rhys Perry, merged 25 Nov as 7918498b, `Fixes:` tag for stable), tested on gfx90c: the affected Loom test passes with it (16 failing trials → 0) |
| M3 | Mesa NIR (RADV) | `try_fold_shared2` asserts (`st64 \|\| normal`) on a constant shared address that wraps around (only in builds with assertions) | mesa/mesa#16891, **fixed upstream** by mesa/mesa!46405 (Reviewed-by Timur Kristóf, approved by Marek Olšák, merged 23 Nov), confirmed on gfx90c with a minimal reproducer (`tests/mesa-16891`) |

Vulkan (RADV) patches for llama.cpp are in a separate repo: https://github.com/Styx85/gfx90c-vulkan-patches

Immich machine learning (CLIP smart search + face recognition) on gfx90c with PyTorch/ROCm, plus face-model notes:
https://github.com/Styx85/immich-face-recognition-notes

## Results (gfx90c)

Test programs and scripts for the GEMM check, the GPU reset test and the Vulkan/Mesa results: [`tests/`](tests/README.md).

**rocBLAS GEMM, GFLOPS** (gfx90c kernels vs. gfx9-generic fallback):

| n | 512 | 1024 | 2048 | 4096 |
|---|---|---|---|---|
| SGEMM | 673 / 672 | 929 / 762 | 977 / 769 | 1396 / 748 |
| HGEMM | 796 / 706 | 1494 / 816 | 1807 / 672 | 1834 / 1254 |

Measured with an earlier `tests/hip/gemm.cpp` that timed only 5 iterations at n = 4096 right after a CPU-side result
check, while the iGPU was still clocking up, so values at large n are too low. Fixed on 26 Nov (warm-up right before
timing, ≥ 500 ms timed): HGEMM 4096³ now 2680–2820 GFLOPS in repeated runs, with the vega10 and the re-tuned tables alike.

Re-tuned selection tables (R3): 3390 problem sizes, median 1.67× faster than the vega10 table choice (it picked tiny
tiles because of nearest-neighbour matching on raw sizes).

**llama.cpp-HIP**, Qwen3-0.6B Q4_K_M, `-fa 1`, t/s: pp512 219 → ~1350, tg128 53 → 85 (all patches + tuned rocBLAS).
Vulkan is still faster for token generation (~100 t/s), so for LLM serving use Vulkan; HIP is useful for PyTorch.

**PyTorch (fp16):** Qwen3-Embedding-0.6B 6.4× faster than the CPU; Whisper small 1.45× faster than the CPU (7.4×
real time, same WER) and the CPU stays free; fp16 matmul up to +82 % with the tuned tables.

Memory bandwidth under HIP and Vulkan is the same (~45 GB/s STREAM); KFD puts "VRAM" allocations into GTT on this APU.

**Mesa MRs on RADV (22 Nov):** mesa/mesa!45511 gives llama.cpp Vulkan pp512 +1.9 % (Qwen3-1.7B); six other MRs
showed no effect on the Loom suite, and llama.cpp stayed within noise where measured. Details: [`tests/README.md`](tests/README.md).

## Important lessons

- **Never use `amdgpu.sched_policy=2` (NO_HWS) on this APU.** Under load it froze the whole host (CP preemption
  timeout → hard lock, power button needed). Stay with HWS (default).
- **Use `amdgpu.noretry=1`.** The kernel runs gfx9 client APUs with retry faults on, ROCr runs them xnack-off.
  With HWS + noretry=1 a hang-hunting loop had 0 hangs in 30+ runs (under NO_HWS without it, about half of the runs
  hung); an earlier crash that looked like a rocBLAS kernel bug no longer reproduced.
- Without patch K, every GPU reset (e.g. a hung kernel) leaves KFD unusable until reboot, while graphics/Vulkan keep
  working. With it, resets recover (tested: requested resets and provoked preemption failures).
- `HSA_OVERRIDE_GFX_VERSION=9.0.0` with `gfx900:xnack-` code produced **silently wrong results** – don't rely on it.
- **Use Mesa ≥ 26.3.1 for Vulkan compute** on this GPU (M1 above). Older RADV miscompiles a common 16-bit
  integer pattern.
- **PyTorch: enable TunableOp** (`PYTORCH_TUNABLEOP_ENABLED=1`, `PYTORCH_TUNABLEOP_HIPBLASLT_ENABLED=0` – there is no
  hipBLASLt for gfx90c). The rocBLAS default solution is often not the fastest here: Whisper large-v3-turbo encoder
  3.61 → 3.04 s, a whole transcription −20 %, demucs −13 %, same results. Tune once, then run with
  `PYTORCH_TUNABLEOP_TUNING=0`; in a systemd unit write `%%d` in the file name.
- When comparing two llama.cpp builds, copied binaries keep the RUNPATH of their build folder and load the wrong
  library – run each from its own build directory.

## Using it

Forks with the branches: https://github.com/Styx85/rocm-systems (branches `gfx90c` on `therock-10.1`,
`gfx90c-develop`) and https://github.com/Styx85/rocm-libraries (`gfx90c`, `gfx90c-logic`,
`tensile-override-codeobject`). The tuned gfx90c logic files are too large for patches (18 MB YAML) – use the branch.

```bash
export LD_LIBRARY_PATH=/opt/rocm-gfx90c/rocblas/lib:/opt/rocm-gfx90c/lib   # own CLR + rocBLAS installed there
hipcc --offload-arch=gfx90c prog.hip   # no HSA_OVERRIDE_GFX_VERSION
```

The kernel patch (`patches/linux/v2/`) applies to `amd-staging-drm-next` and to the Proxmox 7.0.14 kernel; we build
only `amdgpu.ko` out of tree with the exact distribution sources and compiler. Check it with
`tests/kernel-reset/reset-test.sh`.

## oto kernels re-measured on gfx90c (26 Nov 2026)

Configurations from [oto](https://github.com/Netherquark/oto) benchmarked with Tensile (develop, Assembly kernels,
validation passed) against `rocblas-bench` with the gfx90c tables from R3. One machine (5600G, DDR4-3200). Configs and
how to run them: [`tests/tensile-oto/`](tests/tensile-oto/README.md).

**Square NN, GFLOPS (rocBLAS / best oto config):**

| n | 1024 | 1365 | 2048 | 4096 |
|---|---|---|---|---|
| SGEMM | 893 / 1192 (+34 %) | 1267 / 1285 (+1 %) | 975 / 1354 (+39 %) | 1441 / 1403 (−3 %) |
| HGEMM | 1512 / 2186 (+45 %) | 788 / 2298 (+192 %) | 1781 / 2524 (+42 %) | 2755 / 2640 (−4 %) |

Winners: SGEMM MT128x128x8, HGEMM MT128x128x16 (TT8x8, PrefetchGlobalRead 1). The large gains are sizes where the
current table picks a weak solution; at 4096³ rocBLAS is already level.

**PyTorch fp16 path** (fp16 in/out, fp32 accumulation, TN; Whisper large-v3 encoder shapes): the best kernel is
MT96x96x16 (TT6x6), 4–7 % faster than the current table inside rocBLAS (`rocblas-bench`, alternating runs) for 1280×1500×1280, 5120×1500×1280 and 1280×1500×5120. With fp32
accumulation this chip tops out at ~1.5–1.6 TFLOPS (pure fp16: ~2.6). Added as an extra solution to the gfx90c
`Alik_Bljk_HHS_BH` table (patch `0007`, Styx85/rocm-libraries branch `gfx90c`) and re-tuned PyTorch TunableOp for both libraries: Whisper large-v3-turbo encoder
3072 → 2990 ms (−2.7 %), full transcription 3298 → 3218 ms (−2.4 %), identical output.

**fp32 batched attention / demucs shapes:** the current selection is faster than every oto-style config tried (3–21 %).

Lesson: adding a solution to a Tensile table shifts rocBLAS solution indices, so existing PyTorch TunableOp files pick
wrong solutions afterwards (Whisper got 4 % *slower* until TunableOp was re-run). Regenerate TunableOp files after any
library change.

## Upstream status (26 Nov 2026)

| Item | Status |
|---|---|
| R1 | supported the existing PR ROCm/rocm-systems#13903 with test data; ROCm 10.2 does not contain the fix yet |
| R1 + R1b | PR ROCm/rocm-systems#16218 (with hip-tests regression tests since 20 Nov) |
| R4 | PR ROCm/rocm-libraries#16348 |
| R2 | PR ROCm/rocm-libraries#16349 |
| R3 | PR ROCm/rocm-libraries#16350; natively tuned gfx90c kernels from @Netherquark's [oto](https://github.com/Netherquark/oto) are planned to be added there; oto configurations re-measured on a 5600G: big gains where the current selection has gaps, small gains on the PyTorch fp16 path ([below](#oto-kernels-re-measured-on-gfx90c-26-nov-2026)) |
| L (Loom) | reported ROCm/hrx-system#3455, **fixed** by ROCm/hrx-system#3467 (merged 19 Nov), verified on gfx90c (see below) |
| K (kernel) | reported in https://gitlab.freedesktop.org/drm/amd/-/issues/6177; at the maintainer's request submitted to amd-gfx on 20 Nov 2026 as a 2-patch series; following review, squashed into one patch for all gfx9 APUs and **sent as v2 on 21 Nov 2026**: "[PATCH v2] drm/amdgpu/gfx9: deactivate all compute HQDs before MODE2 on APUs" (signed off by me, Assisted-by tag; patch in `patches/linux/v2/`); **reviewed and applied** by Alex Deucher on 23 Nov 2026 (https://lore.kernel.org/amd-gfx/CADnq5_MjcV0Zg1a=ud0rTmdfmPueu9aokn_fewgU47104UJwJg@mail.gmail.com/), in amd-staging-drm-next as aecf8b416, drm/amd#6177 closed on 25 Nov |
| K – possibly related | drm/amd#6053 (same reset-domain trylock problem on gfx1103/MES, found by AMD) and ROCm/TheRock#8357 (Lucienne gfx90c: ROCm broken after the first reset) – both linked to #6177 |
| Vulkan test suite | Loom/IREE Vulkan tests on gfx90c: 289/304 pass at first report (ROCm/hrx-system#3492); with Mesa 26.3.1 + !46557 and a full build **299/304**; Loom v0.3.0 and main (23 Nov) **303/308** – the same 5 tests still fail (`tests/README.md`) |
| M1 / M2 / M3 (Mesa) | M1 fixed in 26.3.1; M2 reported in mesa/mesa#16952, **fixed** by mesa/mesa!46557 (merged 25 Nov, #16952 closed; tested on gfx90c, Tested-by given); M3: fix mesa/mesa!46405 confirmed on gfx90c (Tested-by given) and **merged** (23 Nov) |
| Status trackers | gfx90c data points in ROCm/TheRock#7609 and #9221 (NO_HWS hard lock) |
| C, Vulkan | not submitted (llama.cpp does not accept AI-written PRs/issues) |

If you maintain one of these projects and want to pick something up: please do, credit is not needed.
Issues with numbers from other gfx90c/gfx902/gfx909 machines are welcome.

## Loom fix #3467, re-measured on gfx90c

Clustered f32 sums, subgroup 64, 32 chained sums, Vulkan (RADV/ACO), time per dispatch:

| Cluster size | butterfly before | butterfly with #3467 | native `ClusteredReduce` |
|---|---|---|---|
| 4 | 294 µs | 297 µs | 355 µs |
| 8 | 481 µs | 306 µs | 408 µs |
| 16 | 482 µs | 351 µs | 465 µs |
| 32 | 955 µs | 482 µs | 491 µs |

`ds_swizzle` instructions: 32 → 0 (C=8/16), 64 → 32 (C=32). With the fix the butterfly is faster than native at every
cluster size. Loom's documentation example tests that target Vulkan (saxpy, `guide/checks-and-benchmarks`,
`guide/functions-and-control`) pass on gfx90c.

## License

Each patch carries the license of the project it modifies (ROCm components: MIT; Linux kernel: GPL-2.0; llama.cpp:
MIT). Text in this repository: CC BY 4.0.
