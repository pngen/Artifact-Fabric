#pragma once
// Artifact Fabric - the artifact catalog : identity, provenance, compatibility,
// lifecycle, publication, supersession, invalidation, placement, reuse,
// single-flight production, accounting and authority fencing.
#include <cstdint>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "accounting.hpp"
#include "authority.hpp"
#include "compat.hpp"
#include "dependency.hpp"
#include "descriptor.hpp"
#include "explain.hpp"
#include "reuse.hpp"
#include "placement.hpp"
#include "validation.hpp"

namespace af {

struct PublishRequest {
  ArtifactDescriptor descriptor;
  std::vector<std::uint8_t> content;
  AuthorityEnvelope authority;
};

struct PublishResult {
  ArtifactId id{};
  ContentDigest content_digest{};
  std::string error;
  bool committed = false;
};

class BuildSubscribe {
 public:
  explicit BuildSubscribe(const ArtifactId& id) : id_(id) {}
  const ArtifactId& id() const { return id_; }
  bool claim_owner();
  ArtifactId rendezvous();
  void set_result(ArtifactId result);
  void set_error(const std::string& error);
  bool done() const { return done_; }

 private:
  ArtifactId id_;
  std::mutex mu_;
  std::condition_variable cv_;
  bool done_ = false;
  bool claimed_ = false;
  ArtifactId result_{};
  std::string error_;
};

class Catalog {
 public:
  Catalog() = default;
  Catalog(const Catalog&) = delete;
  Catalog& operator=(const Catalog&) = delete;

  void set_authority(CoordinatorEpoch epoch, const WorkerBootId& boot);
  CoordinatorEpoch epoch() const;
  WorkerBootId boot() const;
  void roll_epoch(WorkerBootId new_boot);

  ArtifactId register_artifact(const ArtifactDescriptor& desc, const AuthorityEnvelope& auth);
  PublishResult publish(const PublishRequest& req);

  struct PublishTransaction {
    ArtifactId reserve(const PublishRequest& req);
    void produce(ArtifactId id, const std::vector<std::uint8_t>& content, const AuthorityEnvelope& auth);
    void validate_publish(ArtifactId id, const ValidationReport& report, const AuthorityEnvelope& auth);
    void verify_dependencies(ArtifactId id, const AuthorityEnvelope& auth);
    void commit(ArtifactId id, const AuthorityEnvelope& auth);
    void abort(ArtifactId id);
    Catalog* cat = nullptr;
    ArtifactId reserved_id{};
    std::uint64_t reserved_bytes = 0;
  };
  PublishTransaction begin_publish();

  void validate_artifact(ArtifactId, const ValidationReport&, const AuthorityEnvelope&);
  void promote(ArtifactId, const AuthorityEnvelope&);
  void supersede(ArtifactId superseded, ArtifactId successor, const std::string& reason, const AuthorityEnvelope&);
  void invalidate(ArtifactId, const std::string& cause, const AuthorityEnvelope&);
  void retire(ArtifactId, const AuthorityEnvelope&);
  void quarantine(ArtifactId, const QuarantineRecord&, const AuthorityEnvelope&);

  const ArtifactDescriptor* find(ArtifactId) const;
  bool contains(ArtifactId) const;
  ArtifactGeneration generation_of(ArtifactId) const;
  std::vector<ArtifactId> find_by_content(const ContentDigest&) const;
  std::vector<ArtifactId> by_kind(ArtifactKind) const;
  std::vector<ArtifactId> by_producer(const ProducerId&) const;
  std::vector<ArtifactId> by_model(const ModelId&) const;
  std::vector<ArtifactId> by_adapter(const AdapterId&) const;
  std::vector<ArtifactId> by_runtime(const RuntimeId&) const;
  std::vector<ArtifactId> by_toolchain(const ToolchainId&) const;
  std::vector<ArtifactId> by_architecture(const std::string&) const;
  std::vector<ArtifactId> by_lifecycle(LifecycleState) const;
  std::vector<ArtifactId> by_validation(ValidationState) const;
  std::vector<ArtifactId> by_provenance(const ProvenanceId&) const;
  std::vector<ArtifactId> by_generation(ArtifactGeneration) const;

  const DependencyGraph& graph() const;
  std::vector<ArtifactId> dependencies_of(ArtifactId) const;
  std::vector<ArtifactId> dependents_of(ArtifactId) const;

  ReuseResult reuse(ArtifactId, const CompatRequirement&) const;
  ArtifactId get_or_build(ArtifactId id, std::function<PublishResult()> producer, const AuthorityEnvelope& auth);

  std::vector<std::uint8_t> save() const;
  void load(const std::vector<std::uint8_t>& image, bool recover = false);
  void save_file(const std::string& path) const;
  void load_file(const std::string& path, bool recover = false);

  bool content_of(ArtifactId, std::vector<std::uint8_t>& out) const;
  bool has_content(ArtifactId) const;
  std::size_t physical_backing_count() const;
  std::size_t content_reference_count(const ContentDigest&) const;

  Accounting accounting() const;
  bool accounting_is_zero() const;
  Explanation explain(ArtifactId) const;
  std::size_t artifact_count() const;
  std::vector<ArtifactId> all_artifacts() const;
  void rebuild_indexes();

 private:
  friend struct PublishTransaction;
  void fence(const AuthorityEnvelope& auth) const;
  void fence_epoch(CoordinatorEpoch epoch) const;
  void add_to_indexes(const ArtifactDescriptor& d);
  void remove_from_indexes(const ArtifactDescriptor& d);
  void recompute_closure_digest(ArtifactId id);
  void invalidate_dependents(ArtifactId id, const std::string& cause);
  void rebuild_indexes_unlocked();

  mutable std::shared_mutex mu_;
  std::unordered_map<ArtifactId, ArtifactDescriptor> artifacts_;
  DependencyGraph graph_;

  CoordinatorEpoch epoch_ = 0;
  WorkerBootId boot_{};
  ArtifactGeneration next_artifact_generation_{};

  std::unordered_map<ContentDigest, std::vector<ArtifactId>> content_index_;
  std::unordered_map<ArtifactKind, std::vector<ArtifactId>> kind_index_;
  std::unordered_map<ProducerId, std::vector<ArtifactId>> producer_index_;
  std::unordered_map<ModelId, std::vector<ArtifactId>> model_index_;
  std::unordered_map<AdapterId, std::vector<ArtifactId>> adapter_index_;
  std::unordered_map<RuntimeId, std::vector<ArtifactId>> runtime_index_;
  std::unordered_map<ToolchainId, std::vector<ArtifactId>> toolchain_index_;
  std::unordered_map<std::string, std::vector<ArtifactId>> arch_index_;
  std::unordered_map<LifecycleState, std::vector<ArtifactId>> lifecycle_index_;
  std::unordered_map<ValidationState, std::vector<ArtifactId>> validation_index_;
  std::unordered_map<ProvenanceId, std::vector<ArtifactId>> provenance_index_;
  std::unordered_map<std::uint64_t, std::vector<ArtifactId>> generation_index_;
  std::unordered_map<ArtifactId, std::vector<ArtifactId>> dependents_cache_;

  struct ContentRef {
    std::vector<std::uint8_t> bytes;
    std::size_t refcount = 0;
  };
  std::unordered_map<ContentDigest, ContentRef> content_store_;

  Accountant accounts_;

  std::unordered_map<ArtifactId, std::shared_ptr<BuildSubscribe>> flights_;
  std::mutex flights_mu_;

  std::unordered_map<ArtifactId, std::vector<ArtifactId>> successor_of_;
  std::unordered_map<ArtifactId, std::vector<ArtifactId>> predecessor_of_;
  std::unordered_map<ArtifactId, std::vector<QuarantineRecord>> quarantine_records_;
  std::unordered_map<ArtifactId, std::string> invalidation_cause_;
  std::unordered_map<ArtifactId, std::shared_ptr<BuildSubscribe>> inflight_;
};

}  // namespace af
