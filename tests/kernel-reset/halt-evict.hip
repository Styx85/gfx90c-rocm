// Preemption failure through the eviction path: a halted wave (s_sethalt) plus a userptr invalidation (munmap of a
// hipHostRegister'ed range) forces KFD to save the queues with CWSR -> expected "queue preemption time out" /
// "Failed to evict" and a GPU reset. Run it on its own, deliberately.
// Build: hipcc --offload-arch=gfx90c -O2 -o halt-evict halt-evict.hip
#include <hip/hip_runtime.h>
#include <cstdio>
#include <unistd.h>
#include <sys/mman.h>
__global__ void halt_kernel(int* flag) {
  if (threadIdx.x == 0) { atomicExch(flag, 1); __threadfence_system(); }
  asm volatile("s_sethalt 1");
}
int main() {
  int* flag; if (hipHostMalloc(&flag, sizeof(int), hipHostMallocCoherent) != hipSuccess) return 2; *flag = 0;
  size_t sz = 2 << 20;
  void* buf = mmap(nullptr, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  ((char*)buf)[0] = 1;
  printf("hipHostRegister: %s\n", hipGetErrorString(hipHostRegister(buf, sz, hipHostRegisterDefault)));
  hipLaunchKernelGGL(halt_kernel, dim3(1), dim3(64), 0, 0, flag);
  for (int i = 0; i < 50 && __atomic_load_n(flag, __ATOMIC_SEQ_CST) == 0; i++) usleep(100000);
  printf("kernel started: flag=%d; munmap of the registered range (eviction)\n", *flag); fflush(stdout);
  munmap(buf, sz);
  sleep(5);
  printf("5 s after munmap, exiting with _exit\n"); fflush(stdout);
  _exit(0);
}
