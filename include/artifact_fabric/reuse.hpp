#pragma once
// Artifact Fabric - first-class reuse eligibility. A cache hit or artifact
// lookup must never bypass reuse eligibility.
#include <string>
#include <vector>

#include "compat.hpp"
#include "descriptor.hpp"

namespace af {

// External state needed to judge reuse. The catalog fills these from live
// storage / provenance / authority state.
struct ReuseInputs {
  bool integrity_ok = true;
  bool placement_available = true;
  bool provenance_valid = true;
  bool authority_current = true;
  std::function<bool(const ArtifactId&)> dependency_fresh = nullptr;
};

struct ReuseResult {
  bool reusable = false;
  std::vector<std::string> failures;   // deterministic, human-readable
  CompatResult compat;

  std::string reason_text() const {
    std::string s = reusable ? "REUSABLE" : "NOT_REUSABLE";
    for (const auto& f : failures) s += std::string("; ") + f;
    return s;
  }
};

// Evaluate reuse eligibility deterministically. Checks, in order: integrity,
// provenance, authority, lifecycle, validation, placement, then compatibility.
// Returns the failed reasons; empty failures => reusable.
ReuseResult check_reuse(const ArtifactDescriptor& desc, const CompatRequirement& req, const ReuseInputs& inputs);

// Convenience wrapper for the trivial case.
ReuseResult check_reuse(const ArtifactDescriptor& desc, const CompatRequirement& req);

}  // namespace af
