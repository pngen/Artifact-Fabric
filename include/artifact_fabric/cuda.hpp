#pragma once
// Artifact Fabric - CUDA-backed hardware artifact proof (real NVIDIA GPU).
#include <string>

#include "catalog.hpp"

namespace af {

struct CudaProofResult {
  bool ok = false;               // the full hardware proof succeeded
  bool hardware_validated = false; // a real CUDA device of the expected class was used
  int devices = 0;               // number of CUDA devices present
  std::string detail;            // human-readable summary
  std::string artifact_digest;   // content digest of the kernel artifact
  bool reuse_rejected_after_mutation = false;
  bool fresh_generation_executed = false;
  bool memory_returns_to_baseline = false;
};

// Returns true if a CUDA device is present and usable.
bool cuda_available();

// Run the full accelerator-backed artifact proof:
//   compile a kernel -> publish as artifact -> load via CUDA driver ->
//   execute on device -> compare to CPU -> reuse check -> mutate compat ->
//   reject reuse -> publish fresh generation -> execute -> release -> memory baseline.
CudaProofResult run_cuda_proof();

// The kernel artifact descriptor used by the proof (for tests / examples).
struct CudaArtifactSpec;
// Build a canned kernel artifact descriptor for a given generation.
void build_kernel_descriptor(ArtifactDescriptor& d, std::uint64_t generation);

}  // namespace af
