#pragma once
// Artifact Fabric - deterministic typed compatibility engine.
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "descriptor.hpp"
#include "kind.hpp"

namespace af {

enum class CompatOutcome : std::int32_t {
  COMPATIBLE = 0,
  INCOMPATIBLE = 1,
  STALE = 2,
  INVALIDATED = 3,
  DEPENDENCY_STALE = 4,
  GENERATION_MISMATCH = 5,
  TOOLCHAIN_MISMATCH = 6,
  ARCH_MISMATCH = 7,
  ABI_MISMATCH = 8,
  INTEGRITY_FAILURE = 9,
  UNKNOWN = 10,
};

inline const char* compat_outcome_name(CompatOutcome o) {
  switch (o) {
    case CompatOutcome::COMPATIBLE: return "COMPATIBLE";
    case CompatOutcome::INCOMPATIBLE: return "INCOMPATIBLE";
    case CompatOutcome::STALE: return "STALE";
    case CompatOutcome::INVALIDATED: return "INVALIDATED";
    case CompatOutcome::DEPENDENCY_STALE: return "DEPENDENCY_STALE";
    case CompatOutcome::GENERATION_MISMATCH: return "GENERATION_MISMATCH";
    case CompatOutcome::TOOLCHAIN_MISMATCH: return "TOOLCHAIN_MISMATCH";
    case CompatOutcome::ARCH_MISMATCH: return "ARCH_MISMATCH";
    case CompatOutcome::ABI_MISMATCH: return "ABI_MISMATCH";
    case CompatOutcome::INTEGRITY_FAILURE: return "INTEGRITY_FAILURE";
    case CompatOutcome::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}

enum class CompatDimension : std::int32_t {
  KIND, MODEL, MODEL_GENERATION, ADAPTER, ADAPTER_GENERATION, COMPOSITION, RUNTIME, BACKEND,
  COMPILER, TOOLCHAIN, TOOLCHAIN_GENERATION, ARCH, COMPUTE_CAPABILITY, ABI, DTYPE, LAYOUT, SHAPE,
  SPECIALIZATION, LAUNCH_GEOMETRY, TOKENIZER, DEPENDENCY_GENERATION, POLICY_GENERATION,
  ARTIFACT_GENERATION, PROTOCOL_VERSION, VALIDATION, LIFECYCLE, INTEGRITY
};

inline const char* compat_dimension_name(CompatDimension d) {
  switch (d) {
    case CompatDimension::KIND: return "kind";
    case CompatDimension::MODEL: return "model";
    case CompatDimension::MODEL_GENERATION: return "model_generation";
    case CompatDimension::ADAPTER: return "adapter";
    case CompatDimension::ADAPTER_GENERATION: return "adapter_generation";
    case CompatDimension::COMPOSITION: return "composition";
    case CompatDimension::RUNTIME: return "runtime";
    case CompatDimension::BACKEND: return "backend";
    case CompatDimension::COMPILER: return "compiler";
    case CompatDimension::TOOLCHAIN: return "toolchain";
    case CompatDimension::TOOLCHAIN_GENERATION: return "toolchain_generation";
    case CompatDimension::ARCH: return "architecture";
    case CompatDimension::COMPUTE_CAPABILITY: return "compute_capability";
    case CompatDimension::ABI: return "abi";
    case CompatDimension::DTYPE: return "dtype";
    case CompatDimension::LAYOUT: return "layout";
    case CompatDimension::SHAPE: return "shape";
    case CompatDimension::SPECIALIZATION: return "specialization";
    case CompatDimension::LAUNCH_GEOMETRY: return "launch_geometry";
    case CompatDimension::TOKENIZER: return "tokenizer_vocab";
    case CompatDimension::DEPENDENCY_GENERATION: return "dependency_generation";
    case CompatDimension::POLICY_GENERATION: return "policy_generation";
    case CompatDimension::ARTIFACT_GENERATION: return "artifact_generation";
    case CompatDimension::PROTOCOL_VERSION: return "protocol_version";
    case CompatDimension::VALIDATION: return "validation";
    case CompatDimension::LIFECYCLE: return "lifecycle";
    case CompatDimension::INTEGRITY: return "integrity";
  }
  return "unknown";
}

// A typed compatibility requirement. A field that is not set ("any") is not
// checked; a set field must match (exact) or be a minimum (generations).
struct CompatRequirement {
  std::optional<ArtifactKind> kind;
  std::optional<ModelId> model;
  std::optional<ModelGeneration> min_model_generation;
  std::optional<AdapterId> adapter;
  std::optional<AdapterGeneration> min_adapter_generation;
  std::optional<RuntimeId> runtime;
  std::optional<BackendId> backend;
  std::optional<CompilerId> compiler;
  std::optional<ToolchainId> toolchain;
  std::optional<ToolchainGeneration> min_toolchain_generation;
  std::optional<std::string> architecture;
  std::optional<std::string> compute_capability;
  std::optional<std::string> abi;
  std::optional<std::string> dtype;
  std::optional<std::string> layout;
  std::optional<std::string> shape;
  std::optional<std::string> specialization;
  std::optional<std::string> launch_geometry;
  std::optional<std::string> tokenizer_vocab;
  std::optional<DependencyGeneration> min_dependency_generation;
  std::optional<PolicyGeneration> min_policy_generation;
  std::optional<ArtifactGeneration> min_artifact_generation;
  std::optional<std::int32_t> protocol_version;
  bool require_valid = false;
  bool require_reusable = false;
};

struct CompatResult {
  CompatOutcome outcome = CompatOutcome::UNKNOWN;
  std::vector<std::string> failed_dimensions;
  std::string message;

  bool compatible() const { return outcome == CompatOutcome::COMPATIBLE; }
};

// Evaluate a descriptor against a compatible requirement. The optional
// `dependency_fresh` predicate, when provided, returns false for a stale
// dependency and selects DEPENDENCY_STALE. Results are deterministic.
CompatResult evaluate_compatibility(
    const ArtifactDescriptor& desc, const CompatRequirement& req,
    std::function<bool(const ArtifactId&)> dependency_fresh = nullptr);

}  // namespace af
