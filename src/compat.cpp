#include "artifact_fabric/compat.hpp"

namespace af {

// Severity ordering: higher = more severe; used to pick a concrete outcome
// when several dimensions fail. The failed dimensions list remains complete.
static int severity(CompatOutcome o) {
  switch (o) {
    case CompatOutcome::INTEGRITY_FAILURE: return 9;
    case CompatOutcome::ABI_MISMATCH: return 8;
    case CompatOutcome::ARCH_MISMATCH: return 7;
    case CompatOutcome::TOOLCHAIN_MISMATCH: return 6;
    case CompatOutcome::GENERATION_MISMATCH: return 5;
    case CompatOutcome::DEPENDENCY_STALE: return 4;
    case CompatOutcome::STALE: return 3;
    case CompatOutcome::INVALIDATED: return 2;
    case CompatOutcome::INCOMPATIBLE: return 1;
    case CompatOutcome::COMPATIBLE:
    case CompatOutcome::UNKNOWN:
    default:
      return 0;
  }
}

static void upgrade(CompatResult& r, CompatOutcome o) {
  if (severity(o) > severity(r.outcome)) r.outcome = o;
}

static void fail(CompatResult& r, const char* dim, CompatOutcome o) {
  r.failed_dimensions.push_back(dim);
  upgrade(r, o);
}

CompatResult evaluate_compatibility(const ArtifactDescriptor& desc, const CompatRequirement& req,
                                    std::function<bool(const ArtifactId&)> dependency_fresh) {
  CompatResult r;
  r.outcome = CompatOutcome::COMPATIBLE;

  // Validation / integrity gates.
  if (req.require_valid && desc.validation_state != ValidationState::VALID) {
    if (desc.validation_state == ValidationState::INTEGRITY_FAILURE)
      fail(r, "validation", CompatOutcome::INTEGRITY_FAILURE);
    else
      fail(r, "validation", CompatOutcome::INVALIDATED);
  }
  if (req.require_reusable) {
    LifecycleState s = desc.lifecycle;
    if (s == LifecycleState::SUPERSEDED) fail(r, "lifecycle", CompatOutcome::STALE);
    else if (s == LifecycleState::QUARANTINED || s == LifecycleState::INVALIDATED)
      fail(r, "lifecycle", CompatOutcome::INVALIDATED);
    else if (s == LifecycleState::EVICTED || s == LifecycleState::RETIRED || s == LifecycleState::FAILED)
      fail(r, "lifecycle", CompatOutcome::INCOMPATIBLE);
  }

  // Exact identity matches.
  if (req.kind && desc.kind != *req.kind) fail(r, "kind", CompatOutcome::INCOMPATIBLE);
  if (req.model && (!desc.model || *desc.model != *req.model)) fail(r, "model", CompatOutcome::INCOMPATIBLE);
  if (req.adapter && (!desc.adapter || *desc.adapter != *req.adapter)) fail(r, "adapter", CompatOutcome::INCOMPATIBLE);
  if (req.runtime && (!desc.runtime || *desc.runtime != *req.runtime)) fail(r, "runtime", CompatOutcome::INCOMPATIBLE);
  if (req.backend && (!desc.backend || *desc.backend != *req.backend)) fail(r, "backend", CompatOutcome::INCOMPATIBLE);
  if (req.compiler && (!desc.compiler || *desc.compiler != *req.compiler)) fail(r, "compiler", CompatOutcome::INCOMPATIBLE);
  if (req.toolchain && (!desc.toolchain || *desc.toolchain != *req.toolchain)) fail(r, "toolchain", CompatOutcome::TOOLCHAIN_MISMATCH);

  if (req.architecture && desc.architecture != *req.architecture) fail(r, "architecture", CompatOutcome::ARCH_MISMATCH);
  if (req.compute_capability && desc.compute_capability != *req.compute_capability)
    fail(r, "compute_capability", CompatOutcome::ARCH_MISMATCH);
  if (req.abi && desc.abi != *req.abi) fail(r, "abi", CompatOutcome::ABI_MISMATCH);
  if (req.dtype && desc.dtype != *req.dtype) fail(r, "dtype", CompatOutcome::INCOMPATIBLE);
  if (req.layout && desc.layout != *req.layout) fail(r, "layout", CompatOutcome::INCOMPATIBLE);
  if (req.shape && desc.shape != *req.shape) fail(r, "shape", CompatOutcome::INCOMPATIBLE);
  if (req.specialization && desc.shape != *req.specialization) fail(r, "specialization", CompatOutcome::INCOMPATIBLE);
  if (req.launch_geometry && desc.launch_metadata != *req.launch_geometry)
    fail(r, "launch_geometry", CompatOutcome::INCOMPATIBLE);
  if (req.tokenizer_vocab && desc.reuse_metadata.find(*req.tokenizer_vocab) == std::string::npos)
    fail(r, "tokenizer_vocab", CompatOutcome::INCOMPATIBLE);

  // Minimum generation checks.
  if (req.min_model_generation && !(desc.model_generation && *desc.model_generation >= *req.min_model_generation))
    fail(r, "model_generation", CompatOutcome::GENERATION_MISMATCH);
  if (req.min_adapter_generation && !(desc.adapter_generation && *desc.adapter_generation >= *req.min_adapter_generation))
    fail(r, "adapter_generation", CompatOutcome::GENERATION_MISMATCH);
  if (req.min_toolchain_generation && desc.toolchain_generation < *req.min_toolchain_generation)
    fail(r, "toolchain_generation", CompatOutcome::TOOLCHAIN_MISMATCH);
  if (req.min_dependency_generation) {
    // The descriptor's effective dependency generation is the maximum required
    // generation across its direct dependencies.
    DependencyGeneration eff{};
    for (const auto& dep : desc.dependencies) if (dep.generation > eff) eff = dep.generation;
    if (eff < *req.min_dependency_generation) fail(r, "dependency_generation", CompatOutcome::DEPENDENCY_STALE);
  }
  if (req.min_policy_generation && desc.policy_generation < *req.min_policy_generation)
    fail(r, "policy_generation", CompatOutcome::STALE);
  if (req.min_artifact_generation && desc.generation < *req.min_artifact_generation)
    fail(r, "artifact_generation", CompatOutcome::GENERATION_MISMATCH);
  if (req.protocol_version && req.protocol_version != ARTIFACT_FABRIC_PROTOCOL_VERSION)
    fail(r, "protocol_version", CompatOutcome::INCOMPATIBLE);

  // Dependency freshness (transitive) via the supplied predicate.
  if (dependency_fresh) {
    for (const auto& dep : desc.dependencies) {
      if (!dependency_fresh(dep.artifact)) { fail(r, "dependency", CompatOutcome::DEPENDENCY_STALE); break; }
    }
  }

  if (r.failed_dimensions.empty()) r.outcome = CompatOutcome::COMPATIBLE;
  if (r.outcome == CompatOutcome::COMPATIBLE) r.message = "compatible";
  else {
    r.message = "incompatible: ";
    for (std::size_t i = 0; i < r.failed_dimensions.size(); ++i) {
      if (i) r.message += ",";
      r.message += r.failed_dimensions[i];
    }
    r.message += std::string(" [") + compat_outcome_name(r.outcome) + "]";
  }
  return r;
}

}  // namespace af
