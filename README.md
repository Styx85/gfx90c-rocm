# ROCm on gfx90c (Vega iGPU of Ryzen 5000G APUs) – patches and findings

Patches and measurements that make ROCm (HIP, rocBLAS, PyTorch, llama.cpp-HIP) run **natively** on the Vega iGPU of
Cezanne/Renoir APUs (**gfx90c**, GCN 5, Wave64), without `HSA_OVERRIDE_GFX_VERSION`.

Tested on: Ryzen 5 5600G (Vega 7, 7 CUs), DDR4-3200, Proxmox VE kernel 7.0.14, Arch Linux container, ROCm 10.1
(TheRock), Mesa 26.2.

> **How this was made:** AI-assisted (Claude Code agents running a measure-change-measure loop on real hardware over
> three days, November 2026). Every result below was measured, but on one machine only. Shared as-is for people
> with the same hardware. Nothing here is official or upstream (see the status table).

## What was broken and what fixes it

| # | Component | Problem | Fix (folder) |
|---|---|---|---|
| R1 | CLR (`rocclr/device/device.cpp`) | ISA table lists gfx902/909/90c as xnack `ANY`, ROCr reports them xnack-unsupported → assertion in `amd::Isa::isCompatible` for every HIP/OpenCL program | align the table with ROCr (`patches/rocm-systems`, commit 1) – overlaps with ROCm/rocm-systems#13903 |
| R1b | HIP (`hip_fatbin.cpp`) | `gfx90c:xnack-` code objects in a fat binary are not found on an xnack-off agent (`hipErrorInvalidKernelFile`, silently wrong results in programs that don't check) | ask COMGR with the feature string (`patches/rocm-systems`, commit 2) |
| R2 | rocBLAS/Tensile | no Tensile library for gfx90c and no fallback to `gfx9-generic` | exact arch → LLVM generic family fallback (`patches/rocm-libraries/*/0001`) |
| R3 | rocBLAS | only the slow generic fallback kernels | gfx90c logic generated from vega10 (`make_gfx90c_logic.sh`), selection tables re-tuned on gfx90c (`0004`–`0006`) |
| R4 | Tensile | `ROCBLAS_TENSILE_GEMM_OVERRIDE_PATH` / solution index → `internal_error` (code object of preloaded libraries never loaded) – architecture-independent | `patches/rocm-libraries/develop/0002` |
| K | Linux amdgpu (GC 9.3.0) | after a GPU reset (MODE2) KFD is dead until reboot: the HIQ (and stuck HWS queues) stay active across MODE2 | deactivate all compute HQDs except the KIQ before MODE2 (`patches/linux`, 2 patches) |
| C | llama.cpp HIP backend | GCN5 has no dot instructions; FA spills, slow mmvq decode | 11 patches (`patches/llama.cpp-hip`) |
| L | Loom/HRX SPIR-V (Vulkan) | clustered f32 subgroup sums (XOR butterfly) slower than native `ClusteredReduce` on GFX9/Wave64 for cluster sizes ≥ 8: XOR 4/16 lower to `ds_swizzle` (LDS) instead of DPP | **fixed upstream** by ROCm/hrx-system#3467 (reported in #3455) |

Vulkan (RADV) patches for llama.cpp are in a separate repo: https://github.com/Styx85/gfx90c-vulkan-patches

## Results (gfx90c)

**rocBLAS GEMM, GFLOPS** (gfx90c kernels vs. gfx9-generic fallback):

| n | 512 | 1024 | 2048 | 4096 |
|---|---|---|---|---|
| SGEMM | 673 / 672 | 929 / 762 | 977 / 769 | 1396 / 748 |
| HGEMM | 796 / 706 | 1494 / 816 | 1807 / 672 | 1834 / 1254 |

Re-tuned selection tables (R3): 3390 problem sizes, median 1.67× faster than the vega10 table choice (it picked tiny
tiles because of nearest-neighbour matching on raw sizes).

**llama.cpp-HIP**, Qwen3-0.6B Q4_K_M, `-fa 1`, t/s: pp512 219 → ~1350, tg128 53 → 85 (all patches + tuned rocBLAS).
Vulkan is still faster for token generation (~100 t/s), so for LLM serving use Vulkan; HIP is useful for PyTorch.

**PyTorch (fp16):** Qwen3-Embedding-0.6B 6.4× faster than the CPU; Whisper small 1.45× faster than the CPU (7.4×
real time, same WER) and the CPU stays free; fp16 matmul up to +82 % with the tuned tables.

Memory bandwidth under HIP and Vulkan is the same (~45 GB/s STREAM); KFD puts "VRAM" allocations into GTT on this APU.

## Important lessons

- **Never use `amdgpu.sched_policy=2` (NO_HWS) on this APU.** Under load it froze the whole host (CP preemption
  timeout → hard lock, power button needed). Stay with HWS (default).
- **Use `amdgpu.noretry=1`.** The kernel runs gfx9 client APUs with retry faults on, ROCr runs them xnack-off.
  With HWS + noretry=1 a hang-hunting loop had 0 hangs in 30+ runs (under NO_HWS without it, about half of the runs
  hung); an earlier crash that looked like a rocBLAS kernel bug no longer reproduced.
- Without patch K, every GPU reset (e.g. a hung kernel) leaves KFD unusable until reboot, while graphics/Vulkan keep
  working. With it, resets recover (tested: requested resets and provoked preemption failures).
- `HSA_OVERRIDE_GFX_VERSION=9.0.0` with `gfx900:xnack-` code produced **silently wrong results** – don't rely on it.
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

The kernel patches are against `amd-staging-drm-next` and against the Proxmox 7.0.14 kernel; we build only
`amdgpu.ko` out of tree with the exact distribution sources and compiler.

## Upstream status (20 Nov 2026)

| Item | Status |
|---|---|
| R1 | supported the existing PR ROCm/rocm-systems#13903 with test data; ROCm 10.2 does not contain the fix yet |
| R1 + R1b | PR ROCm/rocm-systems#16218 |
| R4 | PR ROCm/rocm-libraries#16348 |
| R2 | PR ROCm/rocm-libraries#16349 |
| R3 | PR ROCm/rocm-libraries#16350 |
| L (Loom) | reported ROCm/hrx-system#3455, **fixed** by ROCm/hrx-system#3467 (merged 19 Nov), verified on gfx90c (see below) |
| K (kernel) | not submitted (needs a human submitter with Signed-off-by per kernel rules) |
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
