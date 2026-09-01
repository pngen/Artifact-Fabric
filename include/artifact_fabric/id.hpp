#pragma once
// Artifact Fabric - strongly typed identity model.
// Every identity is a distinct, fixed-size binary type. No generic integers
// or strings are used where a typed identity is required.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "hash.hpp"

namespace af {

// Tag types that give each id a distinct static type. These are empty and
// carry no data; they exist purely so that the Id<...> template instantiations
// are distinct types.
struct ArtifactIdTag {};
struct ArtifactContentIdTag {};
struct ArtifactSetIdTag {};
struct ProducerIdTag {};
struct ProvenanceIdTag {};
struct DependencyIdTag {};
struct ModelIdTag {};
struct AdapterIdTag {};
struct KernelIdTag {};
struct GraphIdTag {};
struct CompilerIdTag {};
struct ToolchainIdTag {};
struct RuntimeIdTag {};
struct BackendIdTag {};
struct DeviceIdTag {};
struct WorkerIdTag {};
struct WorkerBootIdTag {};
struct AttemptIdTag {};
struct PlacementIdTag {};
struct ReplicaIdTag {};
struct StorageLocationIdTag {};
struct EngineIdTag {};

inline std::uint64_t xor_half(const std::uint8_t* p, std::size_t n) {
  std::uint64_t h = 1469598103934665603ull;
  for (std::size_t i = 0; i < n; ++i) h = (h ^ static_cast<std::uint64_t>(p[i])) * 1099511628211ull;
  return h;
}

// ---------------------------------------------------------------------------
// Id<Tag,N> : opaque fixed-size binary identifier.
// N defaults to 16 (128-bit). Content-derived identities use N=32.
// ---------------------------------------------------------------------------
template <typename Tag, std::size_t N = 16>
class Id {
 public:
  static constexpr std::size_t byte_size() { return N; }

  Id() { bytes_.fill(0); }

  // Construct from raw big-endian bytes.
  static Id from_bytes(const std::uint8_t* p) {
    Id out;
    for (std::size_t i = 0; i < N; ++i) out.bytes_[i] = p[i];
    return out;
  }

  // Construct a content-derived id directly from a SHA-256 digest. The digest
  // bytes are used verbatim so that the id is a pure content hash.
  static Id from_digest(const sha256_t& d) {
    Id out;
    const std::size_t n = N;
    for (std::size_t i = 0; i < n; ++i) out.bytes_[i] = d[i];
    return out;
  }

  static Id random();

  static Id from_hex(std::string_view hex);

  const std::uint8_t* bytes() const { return bytes_.data(); }
  std::uint8_t* bytes() { return bytes_.data(); }

  bool is_zero() const {
    for (std::size_t i = 0; i < N; ++i) if (bytes_[i] != 0) return false;
    return true;
  }

  std::string to_string() const;

  bool operator==(const Id& o) const { return bytes_ == o.bytes_; }
  bool operator!=(const Id& o) const { return bytes_ != o.bytes_; }
  bool operator<(const Id& o) const { return bytes_ < o.bytes_; }
  bool operator<=(const Id& o) const { return bytes_ <= o.bytes_; }
  bool operator>(const Id& o) const { return bytes_ > o.bytes_; }
  bool operator>=(const Id& o) const { return bytes_ >= o.bytes_; }

  std::uint64_t hash() const { return xor_half(bytes_.data(), N); }

 private:
  std::array<std::uint8_t, N> bytes_{};
};

template <typename Tag, std::size_t N>
Id<Tag, N> Id<Tag, N>::random() {
  Id out;
  static std::atomic<std::uint64_t> seed{0x9e3779b97f4a7c15ull};
  std::uint64_t s = seed.fetch_add(1, std::memory_order_relaxed) + 1;
  for (std::size_t i = 0; i < N; ++i) {
    s = (s * 6364136223846793005ull) + 1442695040888963407ull;
    out.bytes_[i] = static_cast<std::uint8_t>(s >> 56);
  }
  return out;
}

template <typename Tag, std::size_t N>
Id<Tag, N> Id<Tag, N>::from_hex(std::string_view hex) {
  Id out;
  const std::size_t want = N * 2;
  if (hex.size() < want) throw std::invalid_argument("id hex too short");
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    throw std::invalid_argument("non-hex id character");
  };
  for (std::size_t i = 0; i < N; ++i) {
    out.bytes_[i] = static_cast<std::uint8_t>((nib(hex[i * 2]) << 4) | nib(hex[i * 2 + 1]));
  }
  return out;
}

template <typename Tag, std::size_t N>
std::string Id<Tag, N>::to_string() const {
  return to_hex(bytes_.data(), N);
}

template <typename Tag, std::size_t N>
std::ostream& operator<<(std::ostream& os, const Id<Tag, N>& id) {
  return os << id.to_string();
}

// Distinct identity types.
using ArtifactId = Id<ArtifactIdTag, 16>;
using FullContentId = Id<ArtifactContentIdTag, 32>;  // content-derived (full digest)
using ArtifactContentId = Id<ArtifactContentIdTag, 16>;  // 128-bit stable content identity
using ArtifactSetId = Id<ArtifactSetIdTag, 16>;
using ProducerId = Id<ProducerIdTag, 16>;
using ProvenanceId = Id<ProvenanceIdTag, 16>;
using DependencyId = Id<DependencyIdTag, 16>;
using ModelId = Id<ModelIdTag, 16>;
using AdapterId = Id<AdapterIdTag, 16>;
using KernelId = Id<KernelIdTag, 32>;   // kernel identity (digest-derived)
using GraphId = Id<GraphIdTag, 16>;
using CompilerId = Id<CompilerIdTag, 16>;
using ToolchainId = Id<ToolchainIdTag, 16>;
using RuntimeId = Id<RuntimeIdTag, 16>;
using BackendId = Id<BackendIdTag, 16>;
using DeviceId = Id<DeviceIdTag, 16>;
using WorkerId = Id<WorkerIdTag, 16>;
using WorkerBootId = Id<WorkerBootIdTag, 16>;
using AttemptId = Id<AttemptIdTag, 16>;
using PlacementId = Id<PlacementIdTag, 16>;
using ReplicaId = Id<ReplicaIdTag, 16>;
using StorageLocationId = Id<StorageLocationIdTag, 16>;
using EngineId = Id<EngineIdTag, 16>;

}  // namespace af

namespace std {
template <typename Tag, std::size_t N>
struct hash<af::Id<Tag, N>> {
  std::size_t operator()(const af::Id<Tag, N>& id) const noexcept {
    return static_cast<std::size_t>(id.hash());
  }
};
}  // namespace std

