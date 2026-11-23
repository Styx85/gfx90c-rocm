// Smoke test: saxpy with managed memory, checks every element and prints the bandwidth.
// Build: hipcc --offload-arch=gfx90c -O2 -o vadd vadd.hip   (no HSA_OVERRIDE_GFX_VERSION)
#include <hip/hip_runtime.h>
#include <cstdio>
__global__ void saxpy(int n, float a, const float* x, float* y){int i=blockIdx.x*blockDim.x+threadIdx.x; if(i<n) y[i]=a*x[i]+y[i];}
int main(){const int n=1<<24; float *x,*y; hipDeviceProp_t p; hipGetDeviceProperties(&p,0); printf("device: %s %s\n",p.name,p.gcnArchName);
 if(hipMallocManaged(&x,n*4)||hipMallocManaged(&y,n*4)){puts("malloc failed");return 1;}
 for(int i=0;i<n;i++){x[i]=2;y[i]=3;}
 hipEvent_t a,b; hipEventCreate(&a); hipEventCreate(&b); hipEventRecord(a);
 for(int r=0;r<20;r++) hipLaunchKernelGGL(saxpy,dim3((n+255)/256),dim3(256),0,0,n,4.f,x,y);
 hipEventRecord(b); hipError_t e=hipDeviceSynchronize(); float ms; hipEventElapsedTime(&ms,a,b);
 int bad=0; for(int i=0;i<n;i++) if(y[i]!=3+20*8) bad++;
 printf("status %s, %d of %d wrong, %.2f ms per run, %.1f GB/s\n",hipGetErrorString(e),bad,n,ms/20,3.0*n*4/(ms/20)/1e6); return bad!=0;}
