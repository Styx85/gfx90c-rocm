# Tensile benchmarks with oto-style configurations

Configurations from [@Netherquark's oto](https://github.com/Netherquark/oto) (and variants) benchmarked with Tensile on
gfx90c (Ryzen 5 5600G, Vega 7, DDR4-3200, one machine). Results are in the main [README](../../README.md#oto-kernels-re-measured-on-gfx90c-26-nov-2026).

| File | Problem |
|---|---|
| `sgemm-nn.yaml` | SGEMM NN, square 1024–4096 |
| `hgemm-nn.yaml` | HGEMM NN, square 1024–4096 |
| `whisper-hpa-tn.yaml` | fp16 in/out with fp32 accumulation (HHS_BH, what PyTorch uses), TN, Whisper large-v3 encoder shapes |
| `fp32-attention-demucs.yaml` | fp32 batched attention (Whisper, demucs) and demucs linear layers |

Run (Tensile from rocm-libraries develop, prebuilt `tensile-client`; GPU access through a wrapper with a time limit):

    PYTHONPATH=<rocm-libraries>/shared/tensile python <rocm-libraries>/shared/tensile/Tensile/bin/Tensile \
        sgemm-nn.yaml out --prebuilt-client=<path>/tensile-client

Per-size results are in the Tensile log (CSV lines `…,Contraction_…,<size>,<solution>,PASSED,<µs>,<GFLOPS>,…`);
`00_Final.csv` of a problem only lists the solutions of the last benchmark group.

Baseline: `rocblas-bench` with the gfx90c tables from ROCm/rocm-libraries#16350, e.g.

    rocblas-bench -f gemm_ex --a_type f16_r --b_type f16_r --c_type f16_r --d_type f16_r --compute_type f32_r \
        --transposeA T --transposeB N -m 5120 -n 1500 -k 1280 --lda 1280 --ldb 1280 --ldc 5120 --ldd 5120 -i 30 -j 5

`rocblas-bench` includes launch overhead, Tensile's `KernelTime` does not; use enough iterations (`-i 30 -j 5`), a
first cold run can be 15 % slower.
