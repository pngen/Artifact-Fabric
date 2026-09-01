#pragma once
// Artifact Fabric - explicit artifact dependency graph.
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "digest.hpp"
#include "generation.hpp"
#include "id.hpp"

namespace af {

enum class DependencyKind : std::int32_t {
  BUILD = 0,      // compile-time / build-time dependency
  RUNTIME = 1,    // runtime dependency
  PROVENANCE = 2, // provenance dependency
  COMPOSITION = 3,// adapter / composition dependency
};

inline bool is_valid_dependency_kind(std::int32_t v) { return v >= 0 && v <= 3; }

inline const char* dependency_kind_name(DependencyKind k) {
  switch (k) {
    case DependencyKind::BUILD: return "BUILD";
    case DependencyKind::RUNTIME: return "RUNTIME";
    case DependencyKind::PROVENANCE: return "PROVENANCE";
    case DependencyKind::COMPOSITION: return "COMPOSITION";
  }
  return "UNKNOWN";
}

// A reference to a single dependency of an artifact.
struct DependencyRef {
  ArtifactId artifact{};          // the dependency artifact id
  DependencyId dependency_id{};   // stable dependency-relationship id (not the artifact id)
  DependencyGeneration generation{};  // required dependency generation
  ContentDigest digest{};         // content digest of the dependency at declaration time
  DependencyKind kind = DependencyKind::BUILD;
};

// A graph edge with the resolved dependency reference.
struct DependencyEdge {
  DependencyRef ref;
};

// Directed acyclic dependency graph.
class DependencyGraph {
 public:
  static constexpr std::size_t kMaxNodes = 1u << 20;

  void add_node(const ArtifactId& id) {
    throw_if_full();
    nodes_.insert(id);
    out_.emplace(id, std::vector<DependencyRef>{});
    in_.emplace(id, std::vector<ArtifactId>{});
  }

  bool has_node(const ArtifactId& id) const { return nodes_.count(id) != 0; }

  void add_edge(const ArtifactId& from, const DependencyRef& dep) {
    // Reject self-dependency.
    if (from == dep.artifact) throw std::logic_error("self-dependency rejected");
    // Both endpoints must be registered nodes.
    if (!has_node(from)) throw std::logic_error("dependency source not a node");
    if (!has_node(dep.artifact)) throw std::logic_error("dependency target not a node");
    // Reject duplicate edges.
    auto& outs = out_[from];
    for (const auto& e : outs) if (e.artifact == dep.artifact) throw std::logic_error("duplicate dependency edge rejected");
    // Reject illegal cycles before mutating.
    std::vector<ArtifactId> path;
    if (creates_cycle(from, dep.artifact)) throw std::logic_error("dependency cycle rejected");
    outs.push_back(dep);
    in_[dep.artifact].push_back(from);
  }

  const std::vector<DependencyRef>& direct_dependencies(const ArtifactId& id) const {
    static const std::vector<DependencyRef> empty;
    auto it = out_.find(id);
    return it == out_.end() ? empty : it->second;
  }

  // Deterministic transitive closure (sorted) of all transitive dependencies.
  std::vector<ArtifactId> transitive_dependencies(const ArtifactId& id) const {
    std::set<ArtifactId> seen;
    std::vector<ArtifactId> stack;
    const auto& outs = direct_dependencies(id);
    for (const auto& o : outs) stack.push_back(o.artifact);
    while (!stack.empty()) {
      ArtifactId cur = stack.back(); stack.pop_back();
      if (seen.insert(cur).second) {
        for (const auto& o : direct_dependencies(cur)) stack.push_back(o.artifact);
      }
    }
    return std::vector<ArtifactId>(seen.begin(), seen.end());
  }

  // Deterministic transitive dependency digest: SHA-256 over the sorted set of
  // (artifact id + required generation + content digest) tuples for every node
  // transitively reachable from `id`.
  DependencyDigest dependency_digest(const ArtifactId& id) const {
    std::vector<ArtifactId> tc = transitive_dependencies(id);
    Sha256 h;
    for (const auto& x : tc) {
      h.update(x.bytes(), x.byte_size());
      // Include the required generation and digest for each direct edge that
      // references x, else a zero marker.
      bool found = false;
      for (const auto& o : direct_dependencies(id)) {
        if (o.artifact == x) {
          std::uint8_t genbuf[8];
          for (int i = 0; i < 8; ++i) genbuf[i] = static_cast<std::uint8_t>((o.generation.value() >> (56 - 8 * i)) & 0xffu);
          h.update(genbuf, 8);
          h.update(o.digest.data(), o.digest.bytes().size());
          found = true;
          break;
        }
      }
      if (!found) {
        std::uint8_t zero[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        h.update(zero, 8);
      }
    }
    return DependencyDigest(h.finish());
  }

  // Reverse dependents of a target: all nodes that transitively depend on it.
  // Deterministic: nodes are visited in sorted order.
  std::vector<ArtifactId> dependents(const ArtifactId& target) const {
    std::set<ArtifactId> result;
    for (const auto& n : nodes_) {
      for (const auto& d : transitive_dependencies(n)) {
        if (d == target) { result.insert(n); break; }
      }
    }
    return std::vector<ArtifactId>(result.begin(), result.end());
  }

  std::size_t node_count() const { return nodes_.size(); }
  std::size_t edge_count() const {
    std::size_t c = 0;
    for (const auto& [n, outs] : out_) c += outs.size();
    return c;
  }

  // Deterministic topologically sorted order (Kahn). Throws on cycle.
  std::vector<ArtifactId> topological_order() const {
    std::map<ArtifactId, int> indeg;
    for (const auto& n : nodes_) indeg[n] = 0;
    for (const auto& [n, outs] : out_) (void)n;
    for (const auto& [n, ins] : in_) indeg[n] = static_cast<int>(ins.size());
    std::vector<ArtifactId> order;
    std::vector<ArtifactId> ready;
    for (const auto& [n, d] : indeg) if (d == 0) ready.push_back(n);
    std::sort(ready.begin(), ready.end());
    while (!ready.empty()) {
      ArtifactId cur = ready.front();
      ready.erase(ready.begin());
      order.push_back(cur);
      for (const auto& o : out_.at(cur)) {
        if (--indeg[o.artifact] == 0) {
          ready.push_back(o.artifact);
          std::sort(ready.begin(), ready.end());
        }
      }
    }
    if (order.size() != nodes_.size()) throw std::logic_error("dependency cycle across full graph");
    return order;
  }

 private:
  void throw_if_full() const {
    if (nodes_.size() >= kMaxNodes) throw std::overflow_error("dependency graph node limit");
  }

  bool creates_cycle(const ArtifactId& from, const ArtifactId& to_add) const {
    // Adding edge from -> to_add creates a cycle iff to_add already reaches from.
    std::vector<ArtifactId> stack{to_add};
    std::set<ArtifactId> visited;
    while (!stack.empty()) {
      ArtifactId cur = stack.back(); stack.pop_back();
      if (cur == from) return true;
      if (visited.insert(cur).second) {
        auto it = out_.find(cur);
        if (it != out_.end()) for (const auto& o : it->second) stack.push_back(o.artifact);
      }
    }
    return false;
  }

  std::set<ArtifactId> nodes_;
  std::map<ArtifactId, std::vector<DependencyRef>> out_;
  std::map<ArtifactId, std::vector<ArtifactId>> in_;
};

}  // namespace af
