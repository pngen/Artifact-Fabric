#include "artifact_fabric/explain.hpp"

#include <sstream>

#include "artifact_fabric/hash.hpp"
#include "artifact_fabric/id.hpp"
#include "artifact_fabric/kind.hpp"
#include "artifact_fabric/lifecycle.hpp"
#include "artifact_fabric/validation.hpp"

namespace af {

// Deterministic JSON emission (field order fixed; insertion order in vectors
// is preserved only when the producer sorted them, which the catalog does).
static std::string json_escape(const std::string& s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '"': out += "\""; break;
      case '\\': out += "\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += c;
    }
  }
  return out;
}

std::string Explanation::to_json() const {
  std::ostringstream o;
  o << "{";
  o << "\"artifact\":\"" << artifact.to_string() << "\"";
  o << ",\"why_exists\":\"" << json_escape(why_exists) << "\"";
  o << ",\"origin\":\"" << json_escape(origin) << "\"";
  o << ",\"producer\":\"" << producer.to_string() << "\"";
  o << ",\"producer_summary\":\"" << json_escape(producer_summary) << "\"";
  o << ",\"provenance\":\"" << provenance.to_string() << "\"";
  o << ",\"provenance_summary\":\"" << json_escape(provenance_summary) << "\"";
  o << ",\"dependencies\":[";
  for (std::size_t i = 0; i < dependencies.size(); ++i) {
    if (i) o << ",";
    o << "\"" << dependencies[i].to_string() << "\"";
  }
  o << "]";
  o << ",\"validity\":\"" << json_escape(validity_summary) << "\"";
  o << ",\"reuse\":\"" << json_escape(reuse_summary) << "\"";
  o << ",\"reuse_failures\":[";
  for (std::size_t i = 0; i < reuse_failures.size(); ++i) {
    if (i) o << ",";
    o << "\"" << json_escape(reuse_failures[i]) << "\"";
  }
  o << "]";
  o << ",\"superseded_by\":[";
  for (std::size_t i = 0; i < superseded_by.size(); ++i) { if (i) o << ","; o << "\"" << superseded_by[i].to_string() << "\""; }
  o << "]";
  o << ",\"superseded_predecessor\":[";
  for (std::size_t i = 0; i < superseded_predecessor.size(); ++i) { if (i) o << ","; o << "\"" << superseded_predecessor[i].to_string() << "\""; }
  o << "]";
  o << ",\"invalidated_dependents\":[";
  for (std::size_t i = 0; i < invalidated_dependents.size(); ++i) { if (i) o << ","; o << "\"" << invalidated_dependents[i].to_string() << "\""; }
  o << "]";
  o << ",\"authoritative_generation\":\"" << authoritative_generation.to_string() << "\"";
  o << ",\"invalidation_cause\":\"" << json_escape(invalidation_cause) << "\"";
  o << ",\"placement\":[";
  for (std::size_t i = 0; i < placement_summary.size(); ++i) { if (i) o << ","; o << "\"" << json_escape(placement_summary[i]) << "\""; }
  o << "]";
  o << "}";
  return o.str();
}

std::string Explanation::to_text() const {
  std::ostringstream o;
  o << "Artifact " << artifact.to_string() << "\n";
  o << "  why: " << why_exists << "\n";
  o << "  origin: " << origin << "\n";
  o << "  producer: " << producer_summary << "\n";
  o << "  provenance: " << provenance_summary << "\n";
  o << "  validity: " << validity_summary << "\n";
  o << "  reuse: " << reuse_summary << "\n";
  for (const auto& f : reuse_failures) o << "  reuse-failure: " << f << "\n";
  o << "  dependencies (" << dependencies.size() << "):";
  for (const auto& d : dependencies) o << " " << d.to_string();
  o << "\n";
  if (!superseded_by.empty()) { o << "  superseded_by:"; for (auto& s : superseded_by) o << " " << s.to_string(); o << "\n"; }
  if (!superseded_predecessor.empty()) { o << "  superseded_predecessor:"; for (auto& s : superseded_predecessor) o << " " << s.to_string(); o << "\n"; }
  if (!invalidation_cause.empty()) o << "  invalidation_cause: " << invalidation_cause << "\n";
  o << "  authoritative_generation: " << authoritative_generation.to_string() << "\n";
  for (const auto& p : placement_summary) o << "  placement: " << p << "\n";
  return o.str();
}

std::string Explanation::digest() const {
  return to_hex(Sha256::digest(to_json()));
}

}  // namespace af
