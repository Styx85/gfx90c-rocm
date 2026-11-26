# AGENTS.md – guide for AI coding agents (and humans)

Patches, measurements and upstream status for running **ROCm natively on AMD Vega iGPUs** – **gfx90c** (Renoir,
Cezanne, Lucienne; Ryzen 4000/5000G and 5000U APUs, GCN 5, Wave64) – without `HSA_OVERRIDE_GFX_VERSION`: HIP/CLR,
rocBLAS/Tensile, the amdgpu kernel driver (KFD dead after MODE2 reset), llama.cpp HIP backend, plus related Mesa/RADV and
Loom findings. Results and the upstream status table are in `README.md`.

Keywords: ROCm, HIP, gfx90c, gfx902, gfx909, Vega iGPU, Renoir, Cezanne, Lucienne, Ryzen 5600G, APU, xnack, rocBLAS,
Tensile, hipErrorInvalidKernelFile, amd::Isa::isCompatible assertion, KFD, HIQ, MODE2 reset, qcm fence wait loop
timeout, amdgpu.noretry, sched_policy NO_HWS hard lock, PyTorch ROCm, TunableOp, llama.cpp HIP, Mesa RADV, ACO.

## Layout

| Path | What | Upstream |
|---|---|---|
| `patches/rocm-systems/{develop,therock-10.1}/` | CLR ISA table vs. ROCr (xnack), HIP fat-binary selection of `gfx90c:xnack-` | ROCm/rocm-systems#16218 (and #13903) |
| `patches/rocm-libraries/{develop,therock-10.1}/` | Tensile generic fallback, override code object fix, gfx90c logic + re-tuned selection tables (`make_gfx90c_logic.sh`) | ROCm/rocm-libraries#16348, #16349, #16350 |
| `patches/linux/v2/` | deactivate all compute HQDs except the KIQ before MODE2 on gfx9 APUs – the version sent to amd-gfx | drm/amd#6177, "[PATCH v2] drm/amdgpu/gfx9: deactivate all compute HQDs before MODE2 on APUs" (21 Nov 2026), applied by Alex Deucher 23 Nov 2026, in the drm-fixes-7.4 and drm-next-7.5 pulls of 26 Nov 2026 |
| `patches/linux/{amd-staging-drm-next,proxmox-7.0.14}/` | v1: the same fix as a 2-patch series, Renoir only | superseded by v2 |
| `tests/` | test programs and scripts (HIP/rocBLAS checks, GPU reset test, Loom Vulkan suite runner, Mesa MR rounds, #16891 reproducer) | – |
| `patches/llama.cpp-hip/` | 11 GCN5 patches for the HIP backend | not submitted (see rules) |

Branches with complete trees: https://github.com/Styx85/rocm-systems and https://github.com/Styx85/rocm-libraries.
Vulkan patches for llama.cpp: https://github.com/Styx85/gfx90c-vulkan-patches. Immich ML on gfx90c:
https://github.com/Styx85/immich-face-recognition-notes.

## Rules

- **Never use `amdgpu.sched_policy=2` (NO_HWS)** on this APU when testing – it hard-locks the whole host. Use HWS and
  `amdgpu.noretry=1`.
- Measurements are from **one machine** (Ryzen 5 5600G, Vega 7, DDR4-3200). Say so when you quote them; don't present
  them as general.
- **Kernel patches:** only a human who takes responsibility may add a `Signed-off-by` and submit them (Linux requires an
  `Assisted-by:` tag for AI help). v2 was signed off and sent by the repository owner; v1 carries no `Signed-off-by`.
- `tests/kernel-reset/` resets the GPU on purpose – run it only where that is acceptable.
- **llama.cpp does not accept AI-written pull requests, issues or comments.** Don't open any there from this material;
  the patches are shared here for people to use and, if they want, to submit in their own words.
- ROCm projects accept agent-assisted pull requests, but the PR text must be reviewed and approved by a human.
- Keep the upstream status table in `README.md` current when something gets merged, closed or answered.

## Contributing

Numbers from other gfx90c/gfx902/gfx909 machines are welcome as issues. AI assistance is fine here – please say so.
