#pragma once
// Artifact Fabric - deterministic explainability.
#include <cstdint>
#include <string>
#include <vector>

#include "descriptor.hpp"

namespace af {

// A structured, deterministic explanation about an artifact.
struct Explanation {
  ArtifactId artifact{};
  std::string why_exists;
  std::string origin;           // where it came from
  ProducerId producer{};
  std::string producer_summary;
  ProvenanceId provenance{};
  std::string provenance_summary;
  std::vector<ArtifactId> dependencies;
  std::vector<std::string> facts;       // related yes/no facts
  std::string validity_summary;
  std::string reuse_summary;
  std::vector<std::string> reuse_failures;
  std::vector<ArtifactId> superseded_by;
  std::vector<ArtifactId> superseded_predecessor;
  std::vector<ArtifactId> invalidated_dependents;
  ArtifactGeneration authoritative_generation{};
  std::vector<std::string> placement_summary;
  std::string invalidation_cause;

  // Deterministic explanation digest.
  std::string digest() const;
  std::string to_json() const;
  std::string to_text() const;
};

}  // namespace af
