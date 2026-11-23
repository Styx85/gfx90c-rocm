# Tests

The test programs and scripts behind the numbers in the main README, cleaned of machine-specific paths. All results
are from one machine (Ryzen 5 5600G, Vega 7, DDR4-3200, Proxmox VE kernel 7.0.14, HWS + `amdgpu.noretry=1`).

| Folder | What | Used for |
|---|---|---|
| `hip/` | `vadd.hip` (saxpy smoke test, checks every element), `gemm.cpp` (rocBLAS SGEMM/HGEMM vs. a double CPU reference + GFLOPS) | R1/R1b/R2/R3, before/after reset in K |
| `kernel-reset/` | `reset-test.sh` (HIP + rocBLAS before and after a requested MODE2 reset), `halt.hip` and `halt-evict.hip` (provoke a real preemption failure → KFD-triggered reset) | patch K |
| `loom-vulkan/` | `cts-run.sh` (every Loom/IREE Vulkan test on its own), `mesa-mr-round.sh` / `mesa-mr-own-base.sh` (Mesa MRs: suite + llama-bench per build), `ab-driver-builds.sh` (A/B of several builds of one Mesa tree) | M1, M2, Mesa MR testing |
| `mesa-16891/` | `shared2run.c` + two compute shaders: reproducer for the `try_fold_shared2` assertion | mesa/mesa#16891, fix !46405 |

> **Warning:** `halt.hip`, `halt-evict.hip` and `reset-test.sh` reset the GPU on purpose. Without patch K, ROCm/KFD stays
> unusable until reboot afterwards. Never test with `amdgpu.sched_policy=2` (NO_HWS): on this APU that hard-locked the
> whole host.

## Patch K: GPU reset (kernel v2)

Patch `patches/linux/v2/` (applies to `amd-staging-drm-next` and to the Proxmox 7.0.14 kernel), built as an out-of-tree
`amdgpu.ko`, loaded at boot:

- `reset-test.sh`: the requested reset deactivated 9 HQDs (8 KCQs + HIQ), skipped the KIQ seen through the MEC2 pipe
  alias; MODE2 succeeded; HIP (`vadd`) and rocBLAS GEMM fp32/fp16 correct before and after, no `qcm fence` error.
- Without the patch (or with the old path for the first reset in an instrumented build): the HIQ was still active with
  its old MQD after MODE2, the next HIP program hung the GPU and KFD stayed dead until reboot.
- `halt.hip` / `halt-evict.hip` (with the earlier version of the patch, 19 Nov): the preemption failure triggers a
  reset, KFD recovers.

## Loom/IREE Vulkan suite (22 Nov 2026)

hrx-system `16a60473`, 304 Vulkan tests, RADV on gfx90c, Mesa 26.3.1 built from source with and without fixes
(`ab-driver-builds.sh`):

| Mesa build | ok | failed | not built |
|---|---|---|---|
| 26.3.1 | 291 | 6 | 7 |
| 26.3.1 + !46557 (M2 fix) | **292** | 5 | 7 |
| 26.3.1 + !46521 | 291 | 6 | 7 |
| 26.3.1 + both | 292 | 5 | 7 |
| main (26.99.99, 22 Nov, without !46557) | 291 | 6 | 7 |

!46557 fixes `numeric_vector_f32_test` (in a single-test A/B on the same tree: 16 failing trials → 0). The 7 "not
built" tests were a gap in our partial build, not a Loom problem: with a full build (23 Nov) all 7 pass.

### Loom versions (23 Nov 2026, Mesa 26.3.1 + !46557, full build)

| Loom | tests | ok | failed |
|---|---|---|---|
| `16a60473` (tested 22 Nov) | 304 | 299 | 5 |
| v0.3.0 (`e2c41a72`) | 308 | 303 | 5 |
| main (`6de1eb17`, 23 Nov) | 308 | 303 | 5 |

The same 5 tests fail on all three: `inverse_hyperbolic_stress`, `vulkan_tests`, `numeric_matrix_epilogue`,
`numeric_matrix_publication`, `control_branch_guards`. The 4 tests new in v0.3.0 pass. Building main now needs Node.js
with corepack for the serving dashboard (`build_tools/serve_web`); without it, `ninja -k 0` builds everything else.

## Mesa MRs on RADV/gfx90c (22 Nov 2026)

`mesa-mr-round.sh` / `mesa-mr-own-base.sh`: each MR against its own baseline, full Loom suite + llama-bench
(Qwen3-1.7B Q4_K_M, Vulkan, `-fa 1`, pp512/tg128):

| MR | Loom suite | llama.cpp |
|---|---|---|
| !46557 | +1 test (see above) | – |
| !45511 | no change | **pp512 +1.9 %** (542.8 → 553.1 t/s, mean of 3 rounds of 3 runs each; tg128 unchanged) |
| !43527, !45548, !44608 (on main) | no change | within noise |
| !46009, !44527 (on own base) | no change | within noise |
| !46521 | no change | – |

## mesa/mesa#16891 (`try_fold_shared2` assertion)

```bash
glslangValidator -V shared2_wrap.comp -o shared2_wrap.spv   # same for shared2_wrap_store.comp
cc -O2 -o shared2run shared2run.c -lvulkan
./shared2run shared2_wrap.spv          # 256 workgroups, result checked against a CPU oracle
```

The assertion only shows in a Mesa build with assertions (`-Db_ndebug=false`). Without the fix both shaders abort in
`vkCreateComputePipelines` with `nir_opt_offsets.c: try_fold_shared2: Assertion 'st64 || normal' failed`; with !46405
both compile and run correctly (256 × 128 invocations, 0 wrong words).
