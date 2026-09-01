#include "artifact_fabric/persistence.hpp"

#include <cmath>
#include <cstring>
#include <limits>

namespace af {

static std::uint64_t double_bits(double v) {
  std::uint64_t u = 0;
  static_assert(sizeof(u) == sizeof(v), "double must be 64-bit");
  std::memcpy(&u, &v, sizeof(u));
  return u;
}
static double bits_double(std::uint64_t u) {
  double v = 0;
  std::memcpy(&v, &u, sizeof(v));
  return v;
}

static void write_dependency(Encoder& e, const DependencyRef& dep) {
  e.id(dep.artifact);
  e.id(dep.dependency_id);
  write_generation(e, dep.generation);
  e.digest(dep.digest);
  if (!is_valid_dependency_kind(static_cast<std::int32_t>(dep.kind))) throw PersistenceError("invalid dependency kind");
  e.u32(static_cast<std::uint32_t>(dep.kind));
}
static DependencyRef read_dependency(Decoder& d) {
  DependencyRef dep;
  dep.artifact = d.id<ArtifactIdTag, 16>();
  dep.dependency_id = d.id<DependencyIdTag, 16>();
  dep.generation = read_generation<DependencyGenerationTag>(d);
  dep.digest = d.digest<ContentDigestTag>();
  auto k = d.u32();
  if (!is_valid_dependency_kind(static_cast<std::int32_t>(k))) throw PersistenceError("invalid dependency kind");
  dep.kind = static_cast<DependencyKind>(k);
  return dep;
}

static void write_placement(Encoder& e, const Placement& pl) {
  e.id(pl.id);
  e.id(pl.location);
  if (!is_valid_storage_kind(static_cast<std::int32_t>(pl.kind))) throw PersistenceError("invalid storage kind");
  e.u32(static_cast<std::uint32_t>(pl.kind));
  e.str(pl.locator);
  e.u64(pl.byte_size);
  write_generation(e, pl.generation);
  e.u64(double_bits(pl.locality));
  e.u64(double_bits(pl.cost));
  e.u64(double_bits(pl.readiness));
  e.present(pl.integrity_ok);
  e.present(pl.persistent);
}
static Placement read_placement(Decoder& d) {
  Placement pl;
  pl.id = d.id<PlacementIdTag, 16>();
  pl.location = d.id<StorageLocationIdTag, 16>();
  auto k = d.u32();
  if (!is_valid_storage_kind(static_cast<std::int32_t>(k))) throw PersistenceError("invalid storage kind");
  pl.kind = static_cast<StorageKind>(k);
  pl.locator = d.str_fixed(kMaxFieldBytes);
  pl.byte_size = d.u64();
  pl.generation = read_generation<PlacementGenerationTag>(d);
  pl.locality = bits_double(d.u64());
  pl.cost = bits_double(d.u64());
  pl.readiness = bits_double(d.u64());
  if (!std::isfinite(pl.locality) || !std::isfinite(pl.cost) || !std::isfinite(pl.readiness))
    throw PersistenceError("NaN/Inf in placement numeric field");
  pl.integrity_ok = d.present();
  pl.persistent = d.present();
  return pl;
}

void write_descriptor(Encoder& e, const ArtifactDescriptor& d) {
  e.u32(kArtifactImageMagic);
  e.u32(kArtifactImageSchemaVersion);
  e.id(d.id);
  write_kind(e, d.kind);
  write_generation(e, d.generation);
  e.digest(d.content_digest);
  e.id(d.producer);
  write_generation(e, d.producer_generation);
  e.id(d.provenance);
  write_generation(e, d.provenance_generation);
  e.i64(d.created_ns);

  e.present(d.model.has_value()); if (d.model) e.id(*d.model);
  e.present(d.model_generation.has_value()); if (d.model_generation) write_generation(e, *d.model_generation);
  e.present(d.adapter.has_value()); if (d.adapter) e.id(*d.adapter);
  e.present(d.adapter_generation.has_value()); if (d.adapter_generation) write_generation(e, *d.adapter_generation);
  e.present(d.runtime.has_value()); if (d.runtime) e.id(*d.runtime);
  e.present(d.backend.has_value()); if (d.backend) e.id(*d.backend);
  e.present(d.compiler.has_value()); if (d.compiler) e.id(*d.compiler);
  e.present(d.toolchain.has_value()); if (d.toolchain) e.id(*d.toolchain);

  write_generation(e, d.toolchain_generation);
  e.str(d.architecture);
  e.str(d.compute_capability);
  e.str(d.abi);
  e.str(d.dtype);
  e.str(d.layout);
  e.str(d.shape);
  e.str(d.launch_metadata);

  e.u32(static_cast<std::uint32_t>(d.dependencies.size()));
  if (d.dependencies.size() > kMaxDependencyCount) throw PersistenceError("too many dependencies");
  for (const auto& dep : d.dependencies) write_dependency(e, dep);

  e.digest(d.dependency_digest);
  e.digest(d.metadata_digest);
  e.digest(d.compatibility_digest);
  e.str(d.policy_config);
  write_generation(e, d.policy_generation);
  write_validation(e, d.validation_state);
  write_lifecycle(e, d.lifecycle);

  e.u32(static_cast<std::uint32_t>(d.placements.size()));
  if (d.placements.size() > kMaxDescriptorCount) throw PersistenceError("too many placements");
  for (const auto& pl : d.placements) write_placement(e, pl);

  e.u64(d.size_bytes);
  e.str(d.reuse_metadata);
  e.str(d.authority_metadata);
}

template <typename T>
static std::optional<T> read_opt(Decoder& d) { if (!d.present()) return std::nullopt; return T(d); }

ArtifactDescriptor read_descriptor(Decoder& d) {
  ArtifactDescriptor out;
  auto magic = d.u32();
  if (magic != kArtifactImageMagic) throw PersistenceError("bad artifact image magic");
  auto schema = d.u32();
  if (schema != kArtifactImageSchemaVersion) throw PersistenceError("unsupported schema version");

  out.id = d.id<ArtifactIdTag, 16>();
  out.kind = read_kind(d);
  out.generation = read_generation<ArtifactGenerationTag>(d);
  out.content_digest = d.digest<ContentDigestTag>();
  out.producer = d.id<ProducerIdTag, 16>();
  out.producer_generation = read_generation<ProducerGenerationTag>(d);
  out.provenance = d.id<ProvenanceIdTag, 16>();
  out.provenance_generation = read_generation<ProvenanceGenerationTag>(d);
  out.created_ns = d.i64();

  if (d.present()) out.model = d.id<ModelIdTag, 16>();
  if (d.present()) out.model_generation = read_generation<ModelGenerationTag>(d);
  if (d.present()) out.adapter = d.id<AdapterIdTag, 16>();
  if (d.present()) out.adapter_generation = read_generation<AdapterGenerationTag>(d);
  if (d.present()) out.runtime = d.id<RuntimeIdTag, 16>();
  if (d.present()) out.backend = d.id<BackendIdTag, 16>();
  if (d.present()) out.compiler = d.id<CompilerIdTag, 16>();
  if (d.present()) out.toolchain = d.id<ToolchainIdTag, 16>();

  out.toolchain_generation = read_generation<ToolchainGenerationTag>(d);
  out.architecture = d.str_fixed();
  out.compute_capability = d.str_fixed();
  out.abi = d.str_fixed();
  out.dtype = d.str_fixed();
  out.layout = d.str_fixed();
  out.shape = d.str_fixed();
  out.launch_metadata = d.str_fixed();

  auto dep_count = d.u32();
  if (dep_count > kMaxDependencyCount) throw PersistenceError("dependency count exceeds limit");
  for (std::uint32_t i = 0; i < dep_count; ++i) out.dependencies.push_back(read_dependency(d));

  out.dependency_digest = d.digest<DependencyDigestTag>();
  out.metadata_digest = d.digest<MetadataDigestTag>();
  out.compatibility_digest = d.digest<CompatibilityDigestTag>();
  out.policy_config = d.str_fixed();
  out.policy_generation = read_generation<PolicyGenerationTag>(d);
  out.validation_state = read_validation(d);
  out.lifecycle = read_lifecycle(d);

  auto plc_count = d.u32();
  if (plc_count > kMaxDescriptorCount) throw PersistenceError("placement count exceeds limit");
  for (std::uint32_t i = 0; i < plc_count; ++i) out.placements.push_back(read_placement(d));

  out.size_bytes = d.u64();
  out.reuse_metadata = d.str_fixed();
  out.authority_metadata = d.str_fixed();
  return out;
}

std::vector<std::uint8_t> encode_descriptor(const ArtifactDescriptor& d) {
  Encoder e;
  write_descriptor(e, d);
  return e.finalize();  // payload + CRC-32
}

ArtifactDescriptor decode_descriptor(const std::vector<std::uint8_t>& bytes) {
  auto payload = verify_crc_frame(bytes);
  Decoder d(std::string_view(reinterpret_cast<const char*>(payload.data()), payload.size()));
  ArtifactDescriptor out = read_descriptor(d);
  d.require_end();
  return out;
}

}  // namespace af
