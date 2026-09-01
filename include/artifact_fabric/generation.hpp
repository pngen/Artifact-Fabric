#pragma once
// Artifact Fabric - separately typed generations.
// Each authority domain has its own generation type so that generations are
// never cross-contaminated by a single generic counter.
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace af {

// Tag types for generations.
struct ArtifactGenerationTag {};
struct ProducerGenerationTag {};
struct ProvenanceGenerationTag {};
struct DependencyGenerationTag {};
struct ModelGenerationTag {};
struct AdapterGenerationTag {};
struct RuntimeGenerationTag {};
struct ToolchainGenerationTag {};
struct PlacementGenerationTag {};
struct ReplicaGenerationTag {};
struct AttemptGenerationTag {};
struct PolicyGenerationTag {};

// A 64-bit, per-domain monotonic generation counter. Distinct per tag.
template <typename Tag>
class Generation {
 public:
  using value_type = std::uint64_t;
  static constexpr value_type kFirst = 1;

  Generation() : value_(0) {}              // zero == "unset/absent"
  explicit Generation(value_type v) : value_(v) {}

  value_type value() const { return value_; }
  bool is_set() const { return value_ != 0; }

  Generation next() const {
    if (value_ == std::numeric_limits<value_type>::max()) throw std::overflow_error("generation overflow");
    return Generation(value_ + 1);
  }
  Generation& operator++() {
    if (value_ == std::numeric_limits<value_type>::max()) throw std::overflow_error("generation overflow");
    ++value_; return *this;
  }
  Generation operator++(int) { Generation t(*this); ++(*this); return t; }

  bool operator==(const Generation& o) const { return value_ == o.value_; }
  bool operator!=(const Generation& o) const { return value_ != o.value_; }
  bool operator<(const Generation& o) const { return value_ < o.value_; }
  bool operator<=(const Generation& o) const { return value_ <= o.value_; }
  bool operator>(const Generation& o) const { return value_ > o.value_; }
  bool operator>=(const Generation& o) const { return value_ >= o.value_; }

  std::string to_string() const { return std::to_string(value_); }

 private:
  value_type value_;
};

// Distinct generation types.
using ArtifactGeneration = Generation<ArtifactGenerationTag>;
using ProducerGeneration = Generation<ProducerGenerationTag>;
using ProvenanceGeneration = Generation<ProvenanceGenerationTag>;
using DependencyGeneration = Generation<DependencyGenerationTag>;
using ModelGeneration = Generation<ModelGenerationTag>;
using AdapterGeneration = Generation<AdapterGenerationTag>;
using RuntimeGeneration = Generation<RuntimeGenerationTag>;
using ToolchainGeneration = Generation<ToolchainGenerationTag>;
using PlacementGeneration = Generation<PlacementGenerationTag>;
using ReplicaGeneration = Generation<ReplicaGenerationTag>;
using AttemptGeneration = Generation<AttemptGenerationTag>;
using PolicyGeneration = Generation<PolicyGenerationTag>;

}  // namespace af

namespace std {
template <typename Tag>
struct hash<af::Generation<Tag>> {
  std::size_t operator()(const af::Generation<Tag>& g) const noexcept {
    return static_cast<std::size_t>(g.value());
  }
};
}  // namespace std
