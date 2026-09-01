#include "af_test.hpp"
#include "artifact_fabric/cuda.hpp"
#include "artifact_fabric/hash.hpp"

using namespace af;

AF_TEST(cuda_hardware_artifact_proof) {
  AF_CHECK(cuda_available());
  CudaProofResult r = run_cuda_proof();
  std::printf("  cuda_proof: %s\n", r.detail.c_str());
  AF_CHECK(r.hardware_validated);
  AF_CHECK(r.reuse_rejected_after_mutation);
  AF_CHECK(r.fresh_generation_executed);
  AF_CHECK(r.memory_returns_to_baseline);
  AF_CHECK(!r.artifact_digest.empty());
}

int main() { return testfw::run_all(); }
