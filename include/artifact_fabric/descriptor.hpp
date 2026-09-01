#pragma once
// Artifact Fabric - canonical artifact descriptor.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "version.hpp"

#include "authority.hpp"
#include "dependency.hpp"
#include "digest.hpp"
#include "generation.hpp"
#include "id.hpp"
#include "kind.hpp"
#include "lifecycle.hpp"
#include "placement.hpp"
#include "validation.hpp"

namespace af {

struct ArtifactDescriptor;
std::string canonical_string(const ArtifactDescriptor& d);

// The canonical descriptor for a machine-produced artifact. Once a generation
// is finalized and published, its semantic identity and content are immutable.
struct ArtifactDescriptor {
  ArtifactId id{};
  ArtifactKind kind = ArtifactKind::OTHER;
  ArtifactGeneration generation{};
  ContentDigest content_digest{};
  ProducerId producer{};
  ProducerGeneration producer_generation{};
  ProvenanceId provenance{};
  ProvenanceGeneration provenance_generation{};
  std::int64_t created_ns = 0;
  std::optional<ModelId> model;
  std::optional<ModelGeneration> model_generation;
  std::optional<AdapterId> adapter;
  std::optional<AdapterGeneration> adapter_generation;
  std::optional<RuntimeId> runtime;
  std::optional<BackendId> backend;
  std::optional<CompilerId> compiler;
  std::optional<ToolchainId> toolchain;
  ToolchainGeneration toolchain_generation{};
  std::string architecture;          // target ISA
  std::string compute_capability;     // e.g. "12.0"
  std::string abi;                    // e.g. "sm_120", "cudart12.0"
  std::string dtype;                  // e.g. "fp16"
  std::string layout;                 // e.g. "NHWC"
  std::string shape;                  // shape / specialization
  std::string launch_metadata;        // launch geometry & config
  std::vector<DependencyRef> dependencies;
  DependencyDigest dependency_digest{};
  MetadataDigest metadata_digest{};
  CompatibilityDigest compatibility_digest{};
  std::string policy_config;          // configuration / policy fingerprint
  PolicyGeneration policy_generation{};
  ValidationState validation_state = ValidationState::UNVALIDATED;
  LifecycleState lifecycle = LifecycleState::DECLARED;
  std::vector<Placement> placements;
  std::uint64_t size_bytes = 0;
  std::string reuse_metadata;
  std::string authority_metadata;     // numeric epoch string, or empty

  bool operator==(const ArtifactDescriptor& o) const;
  bool operator<(const ArtifactDescriptor& o) const { return canonical_string(*this) < canonical_string(o); }
};

// Deterministic canonical encoding used to derive metadata and compatibility
// digests. Same runtime => same bytes => same digest.
std::string canonical_string(const ArtifactDescriptor& d);

// Digest only the compatibility-relevant subset of the descriptor.
sha256_t compute_metadata_digest(const ArtifactDescriptor& d);
sha256_t compute_compatibility_digest(const ArtifactDescriptor& d);
// Distinct artifact identity derived from a logical "semantic key" + generation.
ArtifactId derive_artifact_id(const ArtifactDescriptor& semantic_key, const ArtifactGeneration& gen);

// Fill the descriptor's metadata/compatibility digests from its current fields.
void refresh_descriptor_digests(ArtifactDescriptor& d);

}  // namespace af
