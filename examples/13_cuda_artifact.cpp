// Example 13: CUDA-backed executable artifact (real NVIDIA GPU, sm_120).
#include <cstdio>
#include "artifact_fabric/cuda.hpp"
int main() {
#ifdef ARTIFACT_FABRIC_WITH_CUDA
  af::CudaProofResult r = af::run_cuda_proof();
  std::printf("cuda_proof=%s %s\n", r.ok ? "OK" : "FAIL", r.detail.c_str());
  return r.ok ? 0 : 1;
#else
  std::printf("cuda example: build with -DARTIFACT_FABRIC_WITH_CUDA=ON and a supported NVIDIA GPU\n");
  return 0;
#endif
}
