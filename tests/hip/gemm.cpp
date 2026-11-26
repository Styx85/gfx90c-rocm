// rocBLAS SGEMM/HGEMM check: 2000 random elements per size compared with a double-precision CPU reference, plus GFLOPS.
// Build: hipcc --offload-arch=gfx90c -O2 -o gemm gemm.cpp -lrocblas
// Usage: gemm [n...]   (default 256 1024 2048 4096); exit code 0 = all results within bounds
#include <hip/hip_runtime.h>
#include <hip/hip_fp16.h>
#include <rocblas/rocblas.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <random>
#define CK(x) do{auto _e=(x); if(_e!=hipSuccess){printf("HIP error %s in %s\n",hipGetErrorString(_e),#x);exit(2);}}while(0)
#define CB(x) do{auto _e=(x); if(_e!=rocblas_status_success){printf("rocBLAS error %s in %s\n",rocblas_status_to_string(_e),#x);exit(3);}}while(0)

template<class T> float tof(T v);
template<> float tof<float>(float v){return v;}
template<> float tof<rocblas_half>(rocblas_half v){return __half2float(*reinterpret_cast<__half*>(&v));}
template<class T> T fromf(float v);
template<> float fromf<float>(float v){return v;}
template<> rocblas_half fromf<rocblas_half>(float v){__half h=__float2half(v); return *reinterpret_cast<rocblas_half*>(&h);}

rocblas_status gemm(rocblas_handle h,int n,const float*a,const float*b,float*c,float al,float be){
  return rocblas_sgemm(h,rocblas_operation_none,rocblas_operation_none,n,n,n,&al,a,n,b,n,&be,c,n);}
static bool g_hpa=false; // true: rocblas_gemm_ex f16 in/out, f32 accumulation
rocblas_status gemm(rocblas_handle h,int n,const rocblas_half*a,const rocblas_half*b,rocblas_half*c,float al,float be){
  if(g_hpa) return rocblas_gemm_ex(h,rocblas_operation_none,rocblas_operation_none,n,n,n,&al,a,rocblas_datatype_f16_r,n,b,rocblas_datatype_f16_r,n,&be,
                                   c,rocblas_datatype_f16_r,n,c,rocblas_datatype_f16_r,n,rocblas_datatype_f32_r,rocblas_gemm_algo_standard,0,0);
  rocblas_half ah=fromf<rocblas_half>(al), bh=fromf<rocblas_half>(be);
  return rocblas_hgemm(h,rocblas_operation_none,rocblas_operation_none,n,n,n,&ah,a,n,b,n,&bh,c,n);}

template<class T> int test(rocblas_handle h,const char* name,int n,int reps,double u){
  size_t nn=(size_t)n*n; std::vector<T> A(nn),B(nn),C(nn); std::mt19937 g(42); std::uniform_real_distribution<float> d(-1,1);
  for(size_t i=0;i<nn;i++){A[i]=fromf<T>(d(g));B[i]=fromf<T>(d(g));C[i]=fromf<T>(0);}
  T *dA,*dB,*dC; CK(hipMalloc(&dA,nn*sizeof(T))); CK(hipMalloc(&dB,nn*sizeof(T))); CK(hipMalloc(&dC,nn*sizeof(T)));
  CK(hipMemcpy(dA,A.data(),nn*sizeof(T),hipMemcpyHostToDevice)); CK(hipMemcpy(dB,B.data(),nn*sizeof(T),hipMemcpyHostToDevice));
  CK(hipMemset(dC,0,nn*sizeof(T)));
  CB(gemm(h,n,dA,dB,dC,1.f,0.f)); CK(hipDeviceSynchronize());       // warm-up + result
  CK(hipMemcpy(C.data(),dC,nn*sizeof(T),hipMemcpyDeviceToHost));
  int bad=0; double maxerr=0; std::uniform_int_distribution<int> r(0,n-1);
  for(int s=0;s<2000;s++){int i=r(g),j=r(g); double ref=0,S=0; for(int k=0;k<n;k++){double p=(double)tof(A[i+(size_t)k*n])*tof(B[k+(size_t)j*n]); ref+=p; S+=fabs(p);}
    double e=fabs(tof(C[i+(size_t)j*n])-ref)/(u*sqrt((double)n)*S+u*fabs(ref)+1e-30); if(e>maxerr)maxerr=e; if(!(e<=4.0)) bad++;}
  // The CPU check above leaves the iGPU idle and clocked down: warm up for >= 200 ms right before timing, then time
  // enough iterations for >= 500 ms (a few iterations straight after the check caught the clock ramp-up).
  hipEvent_t e0,e1; CK(hipEventCreate(&e0)); CK(hipEventCreate(&e1)); float wms=0; int w=0; CK(hipEventRecord(e0));
  do{ CB(gemm(h,n,dA,dB,dC,1.f,0.f)); w++; CK(hipEventRecord(e1)); CK(hipEventSynchronize(e1)); CK(hipEventElapsedTime(&wms,e0,e1)); }while(wms<200.f);
  reps=std::max(reps,(int)std::ceil(500.0/(wms/w)));
  CK(hipEventRecord(e0));
  for(int i=0;i<reps;i++) CB(gemm(h,n,dA,dB,dC,1.f,0.f));
  CK(hipEventRecord(e1)); CK(hipEventSynchronize(e1)); float ms; CK(hipEventElapsedTime(&ms,e0,e1));
  double gf=2.0*n*n*(double)n*reps/(ms*1e6);
  printf("%s n=%d: %d/2000 samples wrong (max. error %.2f in units of u*sqrt(n)*S, limit 4), %.2f ms per GEMM, %.1f GFLOPS\n",name,n,bad,maxerr,ms/reps,gf);
  hipFree(dA);hipFree(dB);hipFree(dC); return bad;}

int main(int argc,char**argv){ hipDeviceProp_t p; CK(hipGetDeviceProperties(&p,0)); printf("device: %s %s, %d CUs\n",p.name,p.gcnArchName,p.multiProcessorCount);
  rocblas_handle h; CB(rocblas_create_handle(&h)); int bad=0;
  std::vector<int> ns={256,1024,2048,4096}; if(argc>1){ns.clear(); for(int i=1;i<argc;i++) ns.push_back(atoi(argv[i]));}
  for(int n: ns){ int reps=n>2048?5:(n>1024?10:50); bad+=test<float>(h,"SGEMM",n,reps,ldexp(1.0,-24));
    g_hpa=false; bad+=test<rocblas_half>(h,"HGEMM (hgemm, f16 acc.)",n,reps,ldexp(1.0,-11));
    g_hpa=true;  bad+=test<rocblas_half>(h,"HGEMM (gemm_ex, f32 acc.)",n,reps,ldexp(1.0,-11));}
  rocblas_destroy_handle(h); printf(bad?"RESULT: FAIL\n":"RESULT: OK\n"); return bad!=0;}
