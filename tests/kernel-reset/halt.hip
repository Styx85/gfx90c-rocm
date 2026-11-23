// Smallest trigger for a preemption failure: one wave halts itself with s_sethalt.
// CWSR cannot save the halted wave, so when the process exits (queue removal) KFD logs
// "queue preemption time out" and triggers a GPU reset (MODE2 on gfx90c). Exercises the real KFD failure path.
// Run it on its own, deliberately, on a machine where a GPU reset is acceptable.
// Build: hipcc --offload-arch=gfx90c -O2 -o halt halt.hip
#include <hip/hip_runtime.h>
#include <cstdio>
#include <unistd.h>
__global__ void halt_kernel(int* flag) {
  if (threadIdx.x == 0) { atomicExch(flag, 1); __threadfence_system(); }
  asm volatile("s_sethalt 1");
}
int main() {
  int* flag; hipHostMalloc(&flag, sizeof(int), hipHostMallocCoherent); *flag = 0;
  hipLaunchKernelGGL(halt_kernel, dim3(1), dim3(64), 0, 0, flag);
  for (int i = 0; i < 50 && __atomic_load_n(flag, __ATOMIC_SEQ_CST) == 0; i++) usleep(100000);
  printf("kernel started: flag=%d; waiting 3 s, then exiting without synchronizing\n", *flag);
  fflush(stdout); sleep(3);
  return 0;  // process exit -> KFD removes the queue with the halted wave
}
