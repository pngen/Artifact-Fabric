#include "artifact_fabric/descriptor.hpp"

#include "artifact_fabric/persistence.hpp"

namespace af {

bool ArtifactDescriptor::operator==(const ArtifactDescriptor& o) const {
  return canonical_string(*this) == canonical_string(o);
}

// Build a partial canonical encoding of just the compatibility-relevant fields.
static void encode_compat_fields(Encoder& e, const ArtifactDescriptor& d) {
  write_kind(e, d.kind);
  e.present(d.model.has_value());
  if (d.model) e.id(*d.model);
  e.present(d.model_generation.has_value());
  if (d.model_generation) write_generation(e, *d.model_generation);
  e.present(d.adapter.has_value());
  if (d.adapter) e.id(*d.adapter);
  e.present(d.adapter_generation.has_value());
  if (d.adapter_generation) write_generation(e, *d.adapter_generation);
  e.present(d.runtime.has_value());
  if (d.runtime) e.id(*d.runtime);
  e.present(d.backend.has_value());
  if (d.backend) e.id(*d.backend);
  e.present(d.compiler.has_value());
  if (d.compiler) e.id(*d.compiler);
  e.present(d.toolchain.has_value());
  if (d.toolchain) e.id(*d.toolchain);
  write_generation(e, d.toolchain_generation);
  e.str(d.architecture);
  e.str(d.compute_capability);
  e.str(d.abi);
  e.str(d.dtype);
  e.str(d.layout);
  e.str(d.shape);
  e.str(d.launch_metadata);
  write_generation(e, d.policy_generation);
  write_generation(e, d.generation);
  e.digest(d.dependency_digest);
  e.u32(ARTIFACT_FABRIC_PROTOCOL_VERSION);
}

// Deterministic canonical encoding of the full descriptor.
std::string canonical_string(const ArtifactDescriptor& d) {
  Encoder e;
  write_descriptor(e, d);
  return std::string(reinterpret_cast<const char*>(e.data().data()), e.data().size());
}

sha256_t compute_metadata_digest(const ArtifactDescriptor& d) {
  Encoder e;
  write_descriptor(e, d);
  return Sha256::digest(e.data().data(), e.data().size());
}

sha256_t compute_compatibility_digest(const ArtifactDescriptor& d) {
  Encoder e;
  encode_compat_fields(e, d);
  return Sha256::digest(e.data().data(), e.data().size());
}

ArtifactId derive_artifact_id(const ArtifactDescriptor& semantic_key, const ArtifactGeneration& gen) {
  Encoder e;
  encode_compat_fields(e, semantic_key);
  write_generation(e, gen);
  e.id(semantic_key.producer);
  sha256_t h = Sha256::digest(e.data().data(), e.data().size());
  return ArtifactId::from_digest(h);
}

void refresh_descriptor_digests(ArtifactDescriptor& d) {
  d.metadata_digest = MetadataDigest(compute_metadata_digest(d));
  d.compatibility_digest = CompatibilityDigest(compute_compatibility_digest(d));
}

}  // namespace af
