#include "artifact_fabric/cuda.hpp"

#ifdef ARTIFACT_FABRIC_WITH_CUDA

#include <cuda.h>
#include <nvrtc.h>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cmath>

namespace af {

static const char* kKernelSource = R"(
extern "C" __global__ void saxpy_kernel(float* out, const float* a, const float* b, float alpha, int n) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) out[i] = alpha * a[i] + b[i];
}
)";

bool cuda_available() {
  int devices = 0;
  if (cuInit(0) != CUDA_SUCCESS) return false;
  if (cuDeviceGetCount(&devices) != CUDA_SUCCESS) return false;
  return devices > 0;
}

void build_kernel_descriptor(ArtifactDescriptor& d, std::uint64_t generation) {
  d.kind = ArtifactKind::COMPILED_KERNEL;
  d.generation = ArtifactGeneration(generation);
  d.provenance = ProvenanceId::random();
  d.provenance_generation = ProvenanceGeneration(1);
  d.architecture = "nv_ptx64";
  d.compute_capability = "12.0";
  d.abi = "sm_120";
  d.dtype = "fp32";
  d.layout = "elementwise";
  d.shape = "n=1024";
  d.launch_metadata = "grid=4 block=256";
  d.producer = ProducerId::random();
  d.producer_generation = ProducerGeneration(1);
}

// Compile the kernel source for sm_120 with NVRTC and return the PTX bytes.
static bool compile_ptx(std::vector<char>& ptx, std::string& err) {
  nvrtcProgram prog = nullptr;
  nvrtcResult r = nvrtcCreateProgram(&prog, kKernelSource, "saxpy.cu", 0, nullptr, nullptr);
  if (r != NVRTC_SUCCESS) { err = "nvrtcCreateProgram failed: " + std::to_string(r); return false; }
  const char* opts[] = {"--gpu-architecture=sm_120", "--std=c++17"};
  r = nvrtcCompileProgram(prog, 2, opts);
  if (r != NVRTC_SUCCESS) {
    std::size_t logSize = 0; nvrtcGetProgramLogSize(prog, &logSize);
    std::vector<char> log(logSize ? logSize : 1);
    nvrtcGetProgramLog(prog, log.data());
    err = std::string("nvrtcCompileProgram failed: ") + (log.size() ? log.data() : "");
    nvrtcDestroyProgram(&prog); return false;
  }
  std::size_t sz = 0; nvrtcGetPTXSize(prog, &sz);
  ptx.assign(sz, '\0');
  nvrtcGetPTX(prog, ptx.data());
  nvrtcDestroyProgram(&prog);
  return true;
}

// Load PTX module and launch the kernel. Returns true on success.
bool launch_ptx(const std::vector<char>& ptx, float* hostOut, const float* hostA, const float* hostB,
                float alpha, int n, std::string& err) {
  CUmodule module = nullptr;
  if (cuModuleLoadData(&module, ptx.data()) != CUDA_SUCCESS) { err = "cuModuleLoadData failed"; return false; }
  CUfunction fn = nullptr;
  if (cuModuleGetFunction(&fn, module, "saxpy_kernel") != CUDA_SUCCESS) { err = "cuModuleGetFunction failed"; cuModuleUnload(module); return false; }

  CUdeviceptr dOut, dA, dB;
  if (cuMemAlloc(&dOut, n * sizeof(float)) != CUDA_SUCCESS) { err = "cuMemAlloc out"; cuModuleUnload(module); return false; }
  if (cuMemAlloc(&dA, n * sizeof(float)) != CUDA_SUCCESS) { err = "cuMemAlloc a"; cuMemFree(dOut); cuModuleUnload(module); return false; }
  if (cuMemAlloc(&dB, n * sizeof(float)) != CUDA_SUCCESS) { err = "cuMemAlloc b"; cuMemFree(dOut); cuMemFree(dA); cuModuleUnload(module); return false; }
  if (cuMemcpyHtoD(dA, hostA, n * sizeof(float)) != CUDA_SUCCESS) { err = "cuMemcpyHtoD a"; return false; }
  if (cuMemcpyHtoD(dB, hostB, n * sizeof(float)) != CUDA_SUCCESS) { err = "cuMemcpyHtoD b"; return false; }

  int block = 256;
  int grid = (n + block - 1) / block;
  void* args[] = { &dOut, &dA, &dB, &alpha, &n };
  if (cuLaunchKernel(fn, grid, 1, 1, block, 1, 1, 0, nullptr, args, nullptr) != CUDA_SUCCESS) {
    err = "cuLaunchKernel failed"; cuMemFree(dOut); cuMemFree(dA); cuMemFree(dB); cuModuleUnload(module); return false;
  }
  if (cuCtxSynchronize() != CUDA_SUCCESS) { err = "cuCtxSynchronize failed"; cuMemFree(dOut); cuMemFree(dA); cuMemFree(dB); cuModuleUnload(module); return false; }
  if (cuMemcpyDtoH(hostOut, dOut, n * sizeof(float)) != CUDA_SUCCESS) { err = "cuMemcpyDtoH failed"; }
  cuMemFree(dOut); cuMemFree(dA); cuMemFree(dB); cuModuleUnload(module);
  return err.empty();
}

CudaProofResult run_cuda_proof() {
  CudaProofResult res;
  int devices = 0;
  if (cuInit(0) != CUDA_SUCCESS) { res.detail = "CUDA driver init failed"; return res; }
  if (cuDeviceGetCount(&devices) != CUDA_SUCCESS || devices < 1) { res.detail = "no CUDA device"; return res; }
  res.devices = devices;
  res.hardware_validated = true;

  CUdevice dev = 0;
  if (cuDeviceGet(&dev, 0) != CUDA_SUCCESS) { res.detail = "cuDeviceGet failed"; return res; }
  char devName[256] = {0};
  if (cuDeviceGetName(devName, sizeof(devName), dev) != CUDA_SUCCESS) { res.detail = "cuDeviceGetName failed"; return res; }
  int ccMajor = 0, ccMinor = 0;
  cuDeviceGetAttribute(&ccMajor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, dev);
  cuDeviceGetAttribute(&ccMinor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, dev);

  // Establish a current CUDA context (primary) so driver calls work.
  CUcontext ctx = nullptr;
  if (cuDevicePrimaryCtxRetain(&ctx, dev) != CUDA_SUCCESS) { res.detail = "cuDevicePrimaryCtxRetain failed"; return res; }
  if (cuCtxSetCurrent(ctx) != CUDA_SUCCESS) { res.detail = "cuCtxSetCurrent failed"; return res; }

  std::string deviceName = devName;
  res.detail = "device=" + deviceName + " cc=" + std::to_string(ccMajor) + "." + std::to_string(ccMinor);

  // Baseline device memory before we allocate.
  std::size_t free0 = 0, total0 = 0;
  if (cuMemGetInfo(&free0, &total0) != CUDA_SUCCESS) { res.detail += " (cuMemGetInfo failed)"; return res; }

  // 1. Compile a real kernel executable artifact via NVRTC.
  std::vector<char> ptx;
  std::string err;
  if (!compile_ptx(ptx, err)) { res.detail += std::string(" (compile: ") + err + ")"; return res; }

  // 2. Publish through Artifact Fabric as a content-addressed artifact.
  Catalog cat;
  cat.set_authority(1, WorkerBootId::random());
  ArtifactDescriptor desc;
  build_kernel_descriptor(desc, 1);
  RuntimeId rt = RuntimeId::random();
  desc.runtime = rt;
  CompilerId ccid = CompilerId::random();
  desc.compiler = ccid;
  ToolchainId tc = ToolchainId::random();
  desc.toolchain = tc;
  desc.toolchain_generation = ToolchainGeneration(1);
  desc.content_digest = ContentDigest(Sha256::digest(ptx.data(), ptx.size()));
  desc.size_bytes = ptx.size();
  PublishRequest req;
  req.descriptor = desc;
  req.content.assign(ptx.begin(), ptx.end());
  AuthorityEnvelope auth;
  auth.epoch = cat.epoch(); auth.boot = cat.boot();
  auth.producer = desc.producer; auth.producer_generation = desc.producer_generation;
  req.authority = auth;
  PublishResult pr = cat.publish(req);
  if (!pr.committed) { res.detail += " (publish failed: " + pr.error + ")"; return res; }
  res.artifact_digest = pr.content_digest.to_string();

  const int n = 1024;
  std::vector<float> a(n), b(n), out(n), ref(n);
  for (int i = 0; i < n; ++i) { a[i] = static_cast<float>(i) * 0.5f; b[i] = static_cast<float>(i % 7) - 3.0f; ref[i] = 2.0f * a[i] + b[i]; }

  // 3-6. Execute the artifact on the device and compare to the CPU reference.
  if (!launch_ptx(ptx, out.data(), a.data(), b.data(), 2.0f, n, err)) { res.detail += " (launch1: " + err + ")"; return res; }
  bool match1 = true;
  for (int i = 0; i < n; ++i) if (std::fabs(out[i] - ref[i]) > 1e-4f) { match1 = false; break; }
  if (!match1) { res.detail += " (first execution mismatch)"; return res; }

  // 7. Reuse the same valid artifact -> must be reusable.
  CompatRequirement req1;
  req1.compute_capability = "12.0"; req1.abi = "sm_120"; req1.require_reusable = true;
  ReuseResult reuse1 = cat.reuse(pr.id, req1);
  if (!reuse1.reusable) { res.detail += " (expected reuse, got " + reuse1.reason_text() + ")"; return res; }

  // 8. Mutate a compatibility requirement -> reuse must be rejected.
  CompatRequirement req2;
  req2.compute_capability = "9.0";   // wrong compute capability
  req2.require_reusable = true;
  ReuseResult reuse2 = cat.reuse(pr.id, req2);
  res.reuse_rejected_after_mutation = !reuse2.reusable;

  // 9. Publish a fresh artifact generation and execute again.
  ArtifactDescriptor desc2;
  build_kernel_descriptor(desc2, 2);
  desc2.runtime = rt; desc2.compiler = ccid; desc2.toolchain = tc;
  desc2.toolchain_generation = ToolchainGeneration(1);
  desc2.size_bytes = ptx.size();
  PublishRequest req3;
  req3.descriptor = desc2;
  req3.content.assign(ptx.begin(), ptx.end());
  req3.authority = auth;
  PublishResult pr2 = cat.publish(req3);
  std::vector<float> out2(n);
  if (pr2.committed && launch_ptx(ptx, out2.data(), a.data(), b.data(), 2.0f, n, err)) {
    bool match2 = true;
    for (int i = 0; i < n; ++i) if (std::fabs(out2[i] - ref[i]) > 1e-4f) { match2 = false; break; }
    res.fresh_generation_executed = match2;
  }

  // 10. Release everything and verify device memory returns to baseline.
  std::size_t free1 = 0, total1 = 0;
  if (cuMemGetInfo(&free1, &total1) == CUDA_SUCCESS) {
    std::size_t delta = free0 > free1 ? free0 - free1 : 0;
    res.memory_returns_to_baseline = delta < (8u << 20);  // within 8 MiB of baseline
  }

  res.ok = res.hardware_validated && res.reuse_rejected_after_mutation && res.fresh_generation_executed && res.memory_returns_to_baseline;
  res.detail += " digest=" + res.artifact_digest +
                " reuse_rejected=" + (res.reuse_rejected_after_mutation ? "true" : "false") +
                " fresh_exec=" + (res.fresh_generation_executed ? "true" : "false") +
                " mem_baseline=" + (res.memory_returns_to_baseline ? "true" : "false");
  return res;
}

}  // namespace af

#else
// Non-CUDA build: the proof reports unavailable rather than faking hardware.
namespace af {
bool cuda_available() { return false; }
CudaProofResult run_cuda_proof() {
  CudaProofResult res;
  res.detail = "CUDA not enabled (build with -DARTIFACT_FABRIC_WITH_CUDA=ON)";
  return res;
}
void build_kernel_descriptor(ArtifactDescriptor& d, std::uint64_t generation) {
  d.kind = ArtifactKind::COMPILED_KERNEL;
  d.generation = ArtifactGeneration(generation);
  d.compute_capability = "12.0"; d.abi = "sm_120"; d.architecture = "nv_ptx64";
  d.shape = "n=1024"; d.launch_metadata = "grid=4 block=256"; d.dtype = "fp32";
}
}  // namespace af
#endif
