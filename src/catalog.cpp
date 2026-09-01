#include "artifact_fabric/catalog.hpp"
#include "artifact_fabric/persistence.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <unordered_set>
#include <sstream>

namespace af {

// ---------------------------------------------------------------------------
// BuildSubscribe
// ---------------------------------------------------------------------------
bool BuildSubscribe::claim_owner() {
  std::lock_guard<std::mutex> lk(mu_);
  if (claimed_) return false;
  claimed_ = true;
  return true;
}
ArtifactId BuildSubscribe::rendezvous() {
  std::unique_lock<std::mutex> lk(mu_);
  cv_.wait(lk, [this] { return done_; });
  if (!error_.empty()) throw std::runtime_error(error_);
  return result_;
}
void BuildSubscribe::set_result(ArtifactId result) {
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (done_) return;
    result_ = result;
    done_ = true;
  }
  cv_.notify_all();
}
void BuildSubscribe::set_error(const std::string& error) {
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (done_) return;
    error_ = error;
    done_ = true;
  }
  cv_.notify_all();
}

// ---------------------------------------------------------------------------
// Authority
// ---------------------------------------------------------------------------
void Catalog::set_authority(CoordinatorEpoch epoch, const WorkerBootId& boot) {
  std::unique_lock<std::shared_mutex> lk(mu_);
  epoch_ = epoch;
  boot_ = boot;
}
CoordinatorEpoch Catalog::epoch() const { std::shared_lock<std::shared_mutex> lk(mu_); return epoch_; }
WorkerBootId Catalog::boot() const { std::shared_lock<std::shared_mutex> lk(mu_); return boot_; }
void Catalog::roll_epoch(WorkerBootId new_boot) {
  std::unique_lock<std::shared_mutex> lk(mu_);
  ++epoch_;
  boot_ = new_boot;
}

void Catalog::fence(const AuthorityEnvelope& a) const {
  std::shared_lock<std::shared_mutex> lk(mu_);
  if (a.epoch != epoch_) throw AuthorityError(AuthorityReject::STALE_EPOCH, "epoch does not match current authority");
  if (a.boot != boot_) throw AuthorityError(AuthorityReject::STALE_BOOT, "boot id does not match current authority");
  if (a.producer_generation.is_set() && a.producer_generation.value() > 1) {
    // Producer generation is monotonic; fresh work must carry the newest one.
  }
}
void Catalog::fence_epoch(CoordinatorEpoch epoch) const {
  if (epoch != epoch_) throw AuthorityError(AuthorityReject::STALE_EPOCH, "epoch does not match current");
}

// ---------------------------------------------------------------------------
// Indexing
// ---------------------------------------------------------------------------
static void append_unique(std::vector<ArtifactId>& v, const ArtifactId& id) {
  for (const auto& x : v) if (x == id) return;
  v.push_back(id);
}

void Catalog::add_to_indexes(const ArtifactDescriptor& d) {
  append_unique(content_index_[d.content_digest], d.id);
  append_unique(kind_index_[d.kind], d.id);
  append_unique(producer_index_[d.producer], d.id);
  if (d.model) append_unique(model_index_[*d.model], d.id);
  if (d.adapter) append_unique(adapter_index_[*d.adapter], d.id);
  if (d.runtime) append_unique(runtime_index_[*d.runtime], d.id);
  if (d.toolchain) append_unique(toolchain_index_[*d.toolchain], d.id);
  if (!d.architecture.empty()) append_unique(arch_index_[d.architecture], d.id);
  append_unique(lifecycle_index_[d.lifecycle], d.id);
  append_unique(validation_index_[d.validation_state], d.id);
  if (!d.provenance.is_zero()) append_unique(provenance_index_[d.provenance], d.id);
  append_unique(generation_index_[d.generation.value()], d.id);
}

void Catalog::remove_from_indexes(const ArtifactDescriptor& d) {
  auto erase_one = [](std::vector<ArtifactId>& v, const ArtifactId& id) {
    v.erase(std::remove(v.begin(), v.end(), id), v.end());
  };
  erase_one(content_index_[d.content_digest], d.id);
  erase_one(kind_index_[d.kind], d.id);
  erase_one(producer_index_[d.producer], d.id);
  if (d.model) erase_one(model_index_[*d.model], d.id);
  if (d.adapter) erase_one(adapter_index_[*d.adapter], d.id);
  if (d.runtime) erase_one(runtime_index_[*d.runtime], d.id);
  if (d.toolchain) erase_one(toolchain_index_[*d.toolchain], d.id);
  if (!d.architecture.empty()) erase_one(arch_index_[d.architecture], d.id);
  erase_one(lifecycle_index_[d.lifecycle], d.id);
  erase_one(validation_index_[d.validation_state], d.id);
  if (!d.provenance.is_zero()) erase_one(provenance_index_[d.provenance], d.id);
  erase_one(generation_index_[d.generation.value()], d.id);
}

void Catalog::rebuild_indexes_unlocked() {
  content_index_.clear(); kind_index_.clear(); producer_index_.clear();
  model_index_.clear(); adapter_index_.clear(); runtime_index_.clear();
  toolchain_index_.clear(); arch_index_.clear(); lifecycle_index_.clear();
  validation_index_.clear(); provenance_index_.clear(); generation_index_.clear();
  for (const auto& kv : artifacts_) add_to_indexes(kv.second);
}
void Catalog::rebuild_indexes() {
  std::unique_lock<std::shared_mutex> lk(mu_);
  rebuild_indexes_unlocked();
}

// ---------------------------------------------------------------------------
// Lookups
// ---------------------------------------------------------------------------
const ArtifactDescriptor* Catalog::find(ArtifactId id) const {
  std::shared_lock<std::shared_mutex> lk(mu_);
  auto it = artifacts_.find(id);
  return it == artifacts_.end() ? nullptr : &it->second;
}
bool Catalog::contains(ArtifactId id) const { return find(id) != nullptr; }
ArtifactGeneration Catalog::generation_of(ArtifactId id) const {
  auto* d = find(id);
  if (!d) throw std::out_of_range("unknown artifact");
  return d->generation;
}
static std::vector<ArtifactId> sorted_unique_ids(std::vector<ArtifactId> v) {
  std::sort(v.begin(), v.end());
  v.erase(std::unique(v.begin(), v.end()), v.end());
  return v;
}
std::vector<ArtifactId> Catalog::find_by_content(const ContentDigest& c) const {
  std::shared_lock<std::shared_mutex> lk(mu_);
  auto it = content_index_.find(c);
  return it == content_index_.end() ? std::vector<ArtifactId>{} : sorted_unique_ids(it->second);
}
std::vector<ArtifactId> Catalog::by_kind(ArtifactKind k) const {
  std::shared_lock<std::shared_mutex> lk(mu_); auto it = kind_index_.find(k); return it == kind_index_.end() ? std::vector<ArtifactId>{} : sorted_unique_ids(it->second);
}
std::vector<ArtifactId> Catalog::by_producer(const ProducerId& p) const {
  std::shared_lock<std::shared_mutex> lk(mu_); auto it = producer_index_.find(p); return it == producer_index_.end() ? std::vector<ArtifactId>{} : sorted_unique_ids(it->second);
}
std::vector<ArtifactId> Catalog::by_model(const ModelId& m) const {
  std::shared_lock<std::shared_mutex> lk(mu_); auto it = model_index_.find(m); return it == model_index_.end() ? std::vector<ArtifactId>{} : sorted_unique_ids(it->second);
}
std::vector<ArtifactId> Catalog::by_adapter(const AdapterId& a) const {
  std::shared_lock<std::shared_mutex> lk(mu_); auto it = adapter_index_.find(a); return it == adapter_index_.end() ? std::vector<ArtifactId>{} : sorted_unique_ids(it->second);
}
std::vector<ArtifactId> Catalog::by_runtime(const RuntimeId& r) const {
  std::shared_lock<std::shared_mutex> lk(mu_); auto it = runtime_index_.find(r); return it == runtime_index_.end() ? std::vector<ArtifactId>{} : sorted_unique_ids(it->second);
}
std::vector<ArtifactId> Catalog::by_toolchain(const ToolchainId& t) const {
  std::shared_lock<std::shared_mutex> lk(mu_); auto it = toolchain_index_.find(t); return it == toolchain_index_.end() ? std::vector<ArtifactId>{} : sorted_unique_ids(it->second);
}
std::vector<ArtifactId> Catalog::by_architecture(const std::string& s) const {
  std::shared_lock<std::shared_mutex> lk(mu_); auto it = arch_index_.find(s); return it == arch_index_.end() ? std::vector<ArtifactId>{} : sorted_unique_ids(it->second);
}
std::vector<ArtifactId> Catalog::by_lifecycle(LifecycleState s) const {
  std::shared_lock<std::shared_mutex> lk(mu_); auto it = lifecycle_index_.find(s); return it == lifecycle_index_.end() ? std::vector<ArtifactId>{} : sorted_unique_ids(it->second);
}
std::vector<ArtifactId> Catalog::by_validation(ValidationState s) const {
  std::shared_lock<std::shared_mutex> lk(mu_); auto it = validation_index_.find(s); return it == validation_index_.end() ? std::vector<ArtifactId>{} : sorted_unique_ids(it->second);
}
std::vector<ArtifactId> Catalog::by_provenance(const ProvenanceId& p) const {
  std::shared_lock<std::shared_mutex> lk(mu_); auto it = provenance_index_.find(p); return it == provenance_index_.end() ? std::vector<ArtifactId>{} : sorted_unique_ids(it->second);
}
std::vector<ArtifactId> Catalog::by_generation(ArtifactGeneration g) const {
  std::shared_lock<std::shared_mutex> lk(mu_); auto it = generation_index_.find(g.value()); return it == generation_index_.end() ? std::vector<ArtifactId>{} : sorted_unique_ids(it->second);
}

// ---------------------------------------------------------------------------
// Dependency graph
// ---------------------------------------------------------------------------
const DependencyGraph& Catalog::graph() const { return graph_; }
std::vector<ArtifactId> Catalog::dependencies_of(ArtifactId id) const {
  std::shared_lock<std::shared_mutex> lk(mu_);
  std::vector<ArtifactId> out;
  for (const auto& e : graph_.direct_dependencies(id)) out.push_back(e.artifact);
  std::sort(out.begin(), out.end());
  return out;
}
std::vector<ArtifactId> Catalog::dependents_of(ArtifactId id) const {
  std::shared_lock<std::shared_mutex> lk(mu_);
  return graph_.dependents(id);
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------
ArtifactId Catalog::register_artifact(const ArtifactDescriptor& desc, const AuthorityEnvelope& auth) {
  fence(auth);
  std::unique_lock<std::shared_mutex> lk(mu_);
  if (artifacts_.count(desc.id)) throw std::logic_error("duplicate artifact id on register");
  ArtifactDescriptor d = desc;
  if (d.generation.is_set() && d.generation.value() >= next_artifact_generation_.value())
    next_artifact_generation_ = d.generation.next();
  d.lifecycle = LifecycleState::DECLARED;
  d.validation_state = ValidationState::UNVALIDATED;
  artifacts_[d.id] = d;
  add_to_indexes(d);
  return d.id;
}

// ---------------------------------------------------------------------------
// Publication
// ---------------------------------------------------------------------------
PublishResult Catalog::publish(const PublishRequest& req) {
  fence(req.authority);
  ContentDigest cd = ContentDigest(Sha256::digest(req.content.data(), req.content.size()));
  ArtifactDescriptor d = req.descriptor;
  d.content_digest = cd;
  if (d.producer.is_zero()) d.producer = req.authority.producer;
  if (d.producer_generation.value() == 0) d.producer_generation = req.authority.producer_generation;
  if (d.id.is_zero()) d.id = derive_artifact_id(d, d.generation);
  if (!d.generation.is_set()) d.generation = next_artifact_generation_;

  std::unique_lock<std::shared_mutex> lk(mu_);
  if (artifacts_.count(d.id)) throw std::logic_error("duplicate artifact id on publish");
  if (req.authority.artifact_generation.is_set() && req.authority.artifact_generation < d.generation)
    throw AuthorityError(AuthorityReject::STALE_ARTIFACT_GENERATION, "attempted to publish a generation below authority");

  d.lifecycle = LifecycleState::BUILDING;

  const std::uint64_t size = d.size_bytes != 0 ? d.size_bytes : req.content.size();
  auto it = content_store_.find(cd);
  if (it == content_store_.end()) {
    content_store_[cd] = ContentRef{req.content, 1};
    accounts_.add_physical_backing();
    accounts_.add_physical_bytes(static_cast<std::int64_t>(size));
  } else {
    ++it->second.refcount;
    if (it->second.refcount > 1) accounts_.add_dedup_bytes(static_cast<std::int64_t>(size));
  }

  artifacts_[d.id] = d;
  graph_.add_node(d.id);
  for (const auto& dep : d.dependencies) {
    if (!graph_.has_node(dep.artifact)) graph_.add_node(dep.artifact);
    graph_.add_edge(d.id, dep);
  }

  auto dref = artifacts_.find(d.id);
  dref->second.dependency_digest = graph_.dependency_digest(d.id);
  dref->second.validation_state = ValidationState::INTEGRITY_OK;
  dref->second.lifecycle = LifecycleState::VALID;
  refresh_descriptor_digests(dref->second);
  dref->second.lifecycle = LifecycleState::PUBLISHED;

  accounts_.add_logical_artifact();
  accounts_.add_logical_bytes(static_cast<std::int64_t>(d.size_bytes));
  accounts_.add_active_reference();
  if (next_artifact_generation_.value() <= d.generation.value()) next_artifact_generation_ = d.generation.next();
  add_to_indexes(dref->second);

  PublishResult pr;
  pr.id = d.id;
  pr.content_digest = cd;
  pr.committed = true;
  return pr;
}

// ---------------------------------------------------------------------------
// PublishTransaction
// ---------------------------------------------------------------------------
Catalog::PublishTransaction Catalog::begin_publish() {
  PublishTransaction t;
  t.cat = this;
  return t;
}
ArtifactId Catalog::PublishTransaction::reserve(const PublishRequest& req) {
  cat->fence(req.authority);
  ArtifactDescriptor d = req.descriptor;
  if (d.generation.is_set() && d.generation.value() >= cat->next_artifact_generation_.value())
    cat->next_artifact_generation_ = d.generation.next();
  if (d.id.is_zero()) d.id = derive_artifact_id(d, d.generation);
  std::unique_lock<std::shared_mutex> lk(cat->mu_);
  if (cat->artifacts_.count(d.id)) throw std::logic_error("duplicate reserve id");
  d.lifecycle = LifecycleState::BUILDING;
  cat->accounts_.reserve_build();
  cat->accounts_.add_temp_publication_bytes(static_cast<std::int64_t>(req.content.size()));
  cat->artifacts_[d.id] = d;
  reserved_id = d.id;
  reserved_bytes = req.content.size();
  return d.id;
}
void Catalog::PublishTransaction::produce(ArtifactId id, const std::vector<std::uint8_t>& content, const AuthorityEnvelope& auth) {
  cat->fence(auth);
  std::unique_lock<std::shared_mutex> lk(cat->mu_);
  auto it = cat->artifacts_.find(id);
  if (it == cat->artifacts_.end()) throw std::logic_error("produce on unregistered artifact");
  ContentDigest cd = ContentDigest(Sha256::digest(content.data(), content.size()));
  it->second.content_digest = cd;
  it->second.lifecycle = LifecycleState::VALIDATING;
  auto cs = cat->content_store_.find(cd);
  if (cs == cat->content_store_.end()) cat->content_store_[cd] = ContentRef{content, 1};
  else ++cs->second.refcount;
  reserved_bytes = content.size();
}
void Catalog::PublishTransaction::validate_publish(ArtifactId id, const ValidationReport& report, const AuthorityEnvelope& auth) {
  cat->fence(auth);
  std::unique_lock<std::shared_mutex> lk(cat->mu_);
  auto it = cat->artifacts_.find(id);
  if (it == cat->artifacts_.end()) throw std::logic_error("validate on unregistered artifact");
  it->second.validation_state = report.state;
  it->second.lifecycle = report.all_ok() ? LifecycleState::VALID : LifecycleState::FAILED;
}
void Catalog::PublishTransaction::verify_dependencies(ArtifactId id, const AuthorityEnvelope& auth) {
  cat->fence(auth);
  std::unique_lock<std::shared_mutex> lk(cat->mu_);
  auto it = cat->artifacts_.find(id);
  if (it == cat->artifacts_.end()) throw std::logic_error("verify-deps on unregistered artifact");
  ArtifactDescriptor d = it->second;
  if (!cat->graph_.has_node(id)) cat->graph_.add_node(id);
  for (const auto& dep : d.dependencies) {
    if (!cat->graph_.has_node(dep.artifact)) cat->graph_.add_node(dep.artifact);
    cat->graph_.add_edge(id, dep);
  }
  d.dependency_digest = cat->graph_.dependency_digest(id);
  cat->artifacts_[id] = d;
}
void Catalog::PublishTransaction::commit(ArtifactId id, const AuthorityEnvelope& auth) {
  cat->fence(auth);
  std::unique_lock<std::shared_mutex> lk(cat->mu_);
  auto it = cat->artifacts_.find(id);
  if (it == cat->artifacts_.end()) throw std::logic_error("commit on unregistered artifact");
  it->second.lifecycle = LifecycleState::PUBLISHED;
  const std::uint64_t size = it->second.size_bytes != 0 ? it->second.size_bytes : reserved_bytes;
  cat->accounts_.release_build();
  cat->accounts_.add_temp_publication_bytes(-static_cast<std::int64_t>(reserved_bytes));
  cat->accounts_.add_logical_artifact();
  cat->accounts_.add_logical_bytes(static_cast<std::int64_t>(size));
  cat->accounts_.add_active_reference();
  cat->accounts_.add_physical_backing();
  cat->accounts_.add_physical_bytes(static_cast<std::int64_t>(size));
  cat->add_to_indexes(it->second);
}
void Catalog::PublishTransaction::abort(ArtifactId id) {
  std::unique_lock<std::shared_mutex> lk(cat->mu_);
  auto it = cat->artifacts_.find(id);
  if (it != cat->artifacts_.end()) {
    auto cd = it->second.content_digest;
    auto cs = cat->content_store_.find(cd);
    if (cs != cat->content_store_.end() && --cs->second.refcount == 0) cat->content_store_.erase(cd);
    cat->artifacts_.erase(id);
  }
  cat->accounts_.release_build();
  cat->accounts_.add_temp_publication_bytes(-static_cast<std::int64_t>(reserved_bytes));
}

// ---------------------------------------------------------------------------
// Lifecycle operations
// ---------------------------------------------------------------------------
void Catalog::validate_artifact(ArtifactId id, const ValidationReport& report, const AuthorityEnvelope& auth) {
  fence(auth);
  std::unique_lock<std::shared_mutex> lk(mu_);
  auto it = artifacts_.find(id);
  if (it == artifacts_.end()) throw std::out_of_range("unknown artifact");
  if (auth.artifact_generation.is_set() && auth.artifact_generation < it->second.generation)
    throw AuthorityError(AuthorityReject::STALE_ARTIFACT_GENERATION, "stale artifact generation on validate");
  remove_from_indexes(it->second);
  it->second.validation_state = report.state;
  apply_transition(it->second.lifecycle, report.all_ok() ? LifecycleState::VALID : LifecycleState::FAILED);
  add_to_indexes(it->second);
}
void Catalog::promote(ArtifactId id, const AuthorityEnvelope& auth) {
  fence(auth);
  std::unique_lock<std::shared_mutex> lk(mu_);
  auto it = artifacts_.find(id);
  if (it == artifacts_.end()) throw std::out_of_range("unknown artifact");
  if (auth.artifact_generation.is_set() && auth.artifact_generation < it->second.generation)
    throw AuthorityError(AuthorityReject::STALE_ARTIFACT_GENERATION, "stale artifact generation on promote");
  remove_from_indexes(it->second);
  apply_transition(it->second.lifecycle, LifecycleState::ACTIVE);
  add_to_indexes(it->second);
}
void Catalog::supersede(ArtifactId superseded, ArtifactId successor, const std::string& reason, const AuthorityEnvelope& auth) {
  fence(auth);
  std::unique_lock<std::shared_mutex> lk(mu_);
  auto old = artifacts_.find(superseded);
  auto neu = artifacts_.find(successor);
  if (old == artifacts_.end()) throw std::out_of_range("unknown superseded artifact");
  if (neu == artifacts_.end()) throw std::out_of_range("unknown successor artifact");
  if (old->second.lifecycle == LifecycleState::RETIRED || old->second.lifecycle == LifecycleState::EVICTED)
    throw std::logic_error("cannot supersede a retired/evicted artifact");
  if (auth.artifact_generation.is_set() && auth.artifact_generation < neu->second.generation)
    throw AuthorityError(AuthorityReject::STALE_ARTIFACT_GENERATION, "stale artifact generation on supersede");
  remove_from_indexes(old->second);
  apply_transition(old->second.lifecycle, LifecycleState::SUPERSEDED);
  add_to_indexes(old->second);
  successor_of_[superseded].push_back(successor);
  predecessor_of_[successor].push_back(superseded);
  accounts_.record_supersession();
  (void)reason;
}
void Catalog::invalidate(ArtifactId id, const std::string& cause, const AuthorityEnvelope& auth) {
  fence(auth);
  std::unique_lock<std::shared_mutex> lk(mu_);
  auto it = artifacts_.find(id);
  if (it == artifacts_.end()) throw std::out_of_range("unknown artifact");
  if (auth.artifact_generation.is_set() && auth.artifact_generation < it->second.generation)
    throw AuthorityError(AuthorityReject::STALE_ARTIFACT_GENERATION, "stale artifact generation on invalidate");
  invalidate_dependents(id, cause);
}
void Catalog::invalidate_dependents(ArtifactId id, const std::string& cause) {
  auto it = artifacts_.find(id);
  if (it != artifacts_.end() && it->second.lifecycle != LifecycleState::INVALIDATED &&
      it->second.lifecycle != LifecycleState::RETIRED && it->second.lifecycle != LifecycleState::EVICTED) {
    remove_from_indexes(it->second);
    apply_transition(it->second.lifecycle, LifecycleState::INVALIDATED);
    add_to_indexes(it->second);
    invalidation_cause_[id] = cause;
    accounts_.record_invalidation();
  }
  for (const auto& dep : graph_.dependents(id)) {
    auto dit = artifacts_.find(dep);
    if (dit != artifacts_.end() && dit->second.lifecycle != LifecycleState::INVALIDATED &&
        dit->second.lifecycle != LifecycleState::RETIRED && dit->second.lifecycle != LifecycleState::EVICTED) {
      invalidate_dependents(dep, cause);
    }
  }
}
void Catalog::quarantine(ArtifactId id, const QuarantineRecord& qr, const AuthorityEnvelope& auth) {
  fence(auth);
  std::unique_lock<std::shared_mutex> lk(mu_);
  auto it = artifacts_.find(id);
  if (it == artifacts_.end()) throw std::out_of_range("unknown artifact");
  remove_from_indexes(it->second);
  it->second.validation_state = ValidationState::QUARANTINED;
  apply_transition(it->second.lifecycle, LifecycleState::QUARANTINED);
  add_to_indexes(it->second);
  quarantine_records_[id].push_back(qr);
}
void Catalog::retire(ArtifactId id, const AuthorityEnvelope& auth) {
  fence(auth);
  std::unique_lock<std::shared_mutex> lk(mu_);
  auto it = artifacts_.find(id);
  if (it == artifacts_.end()) throw std::out_of_range("unknown artifact");
  remove_from_indexes(it->second);
  apply_transition(it->second.lifecycle, LifecycleState::RETIRED);
  auto cd = it->second.content_digest;
  auto cs = content_store_.find(cd);
  if (cs != content_store_.end()) {
    if (--cs->second.refcount == 0) {
      accounts_.add_physical_backing(-1);
      accounts_.add_physical_bytes(-static_cast<std::int64_t>(cs->second.bytes.size()));
      content_store_.erase(cd);
    }
  }
  accounts_.add_logical_artifact(-1);
  accounts_.add_logical_bytes(-static_cast<std::int64_t>(it->second.size_bytes));
  accounts_.add_active_reference(-1);
  // Retired artifacts remain in the record but are no longer reusable.
  add_to_indexes(it->second);
}

// ---------------------------------------------------------------------------
// Reuse
// ---------------------------------------------------------------------------
ReuseResult Catalog::reuse(ArtifactId id, const CompatRequirement& req) const {
  std::shared_lock<std::shared_mutex> lk(mu_);
  auto it = artifacts_.find(id);
  if (it == artifacts_.end()) {
    ReuseResult r; r.reusable = false; r.failures.push_back("unknown artifact"); return r;
  }
  const auto& d = it->second;
  ReuseInputs inputs;
  inputs.integrity_ok = d.validation_state != ValidationState::INTEGRITY_FAILURE &&
                        d.validation_state != ValidationState::QUARANTINED;
  inputs.placement_available = true;
  if (!d.placements.empty()) {
    bool any = false;
    for (const auto& p : d.placements) if (p.integrity_ok) any = true;
    inputs.placement_available = any;
  }
  inputs.provenance_valid = !d.provenance.is_zero();
  inputs.authority_current = true;
  inputs.dependency_fresh = [&](const ArtifactId& dep) {
    auto depit = artifacts_.find(dep);
    if (depit == artifacts_.end()) return false;
    auto ls = depit->second.lifecycle;
    return ls != LifecycleState::INVALIDATED && ls != LifecycleState::RETIRED &&
           ls != LifecycleState::EVICTED && ls != LifecycleState::QUARANTINED;
  };
  return check_reuse(d, req, inputs);
}

// ---------------------------------------------------------------------------
// Single-flight
// ---------------------------------------------------------------------------
ArtifactId Catalog::get_or_build(ArtifactId id, std::function<PublishResult()> producer, const AuthorityEnvelope& auth) {
  std::shared_ptr<BuildSubscribe> sub;
  {
    std::lock_guard<std::mutex> lk(flights_mu_);
    auto it = flights_.find(id);
    if (it != flights_.end()) sub = it->second;
    else { sub = std::make_shared<BuildSubscribe>(id); flights_[id] = sub; }
  }
  if (sub->claim_owner()) {
    bool success = false;
    try {
      auto pr = producer();
      if (pr.committed) { sub->set_result(pr.id); success = true; }
      else sub->set_error(pr.error.empty() ? "producer returned uncommitted result" : pr.error);
    } catch (const std::exception& e) {
      sub->set_error(e.what());
    } catch (...) {
      sub->set_error("unknown producer exception");
    }
    if (!success) {
      // Allow a later caller to retry a failed build, but never a successful one.
      std::lock_guard<std::mutex> lk(flights_mu_);
      auto it = flights_.find(id);
      if (it != flights_.end() && it->second == sub) flights_.erase(id);
    }
  }
  (void)auth;
  return sub->rendezvous();
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------
std::vector<std::uint8_t> Catalog::save() const {
  std::shared_lock<std::shared_mutex> lk(mu_);
  Encoder e;
  e.u32(kArtifactImageMagic);
  e.u32(2u);
  e.u64(epoch_);
  e.id(boot_);
  e.u64(next_artifact_generation_.value());
  std::vector<ArtifactId> ids;
  for (const auto& kv : artifacts_) ids.push_back(kv.first);
  std::sort(ids.begin(), ids.end());
  e.u32(static_cast<std::uint32_t>(ids.size()));
  for (const auto& id : ids) write_descriptor(e, artifacts_.at(id));
  std::vector<ContentDigest> cds;
  for (const auto& kv : content_store_) cds.push_back(kv.first);
  std::sort(cds.begin(), cds.end());
  e.u32(static_cast<std::uint32_t>(cds.size()));
  for (const auto& cd : cds) {
    e.digest(cd);
    const auto& r = content_store_.at(cd);
    e.u64(static_cast<std::uint64_t>(r.refcount));
    e.bytes(r.bytes.data(), r.bytes.size());
  }
  std::vector<ArtifactId> sid;
  for (const auto& kv : successor_of_) sid.push_back(kv.first);
  std::sort(sid.begin(), sid.end());
  e.u32(static_cast<std::uint32_t>(sid.size()));
  for (const auto& k : sid) {
    e.id(k);
    auto& v = successor_of_.at(k);
    e.u32(static_cast<std::uint32_t>(v.size()));
    for (const auto& x : v) e.id(x);
  }
  return e.finalize();
}

void Catalog::load(const std::vector<std::uint8_t>& image, bool /*recover*/) {
  auto payload = verify_crc_frame(image);
  Decoder d(std::string_view(reinterpret_cast<const char*>(payload.data()), payload.size()));
  auto magic = d.u32();
  if (magic != kArtifactImageMagic) throw PersistenceError("bad catalog image magic");
  auto schema = d.u32();
  if (schema != 2u) throw PersistenceError("unsupported catalog image schema");
  CoordinatorEpoch epoch = d.u64();
  WorkerBootId boot = d.id<WorkerBootIdTag, 16>();
  std::uint64_t nextgen = d.u64();
  std::uint32_t cnt = d.u32();
  std::vector<ArtifactDescriptor> loaded;
  loaded.reserve(cnt);
  for (std::uint32_t i = 0; i < cnt; ++i) loaded.push_back(read_descriptor(d));
  std::uint32_t ccount = d.u32();
  std::unordered_map<ContentDigest, ContentRef> content;
  for (std::uint32_t i = 0; i < ccount; ++i) {
    auto cd = d.digest<ContentDigestTag>();
    std::uint64_t refcount = d.u64();
    auto bytes = d.raw_vec();
    if (bytes.size() > kMaxFieldBytes) throw PersistenceError("content exceeds max length");
    content[cd] = ContentRef{std::move(bytes), static_cast<std::size_t>(refcount)};
  }
  std::uint32_t scount = d.u32();
  std::unordered_map<ArtifactId, std::vector<ArtifactId>> succ;
  for (std::uint32_t i = 0; i < scount; ++i) {
    auto k = d.id<ArtifactIdTag, 16>();
    std::uint32_t n = d.u32();
    std::vector<ArtifactId> v;
    v.reserve(n);
    for (std::uint32_t j = 0; j < n; ++j) v.push_back(d.id<ArtifactIdTag, 16>());
    succ[k] = std::move(v);
  }
  d.require_end();

  std::unique_lock<std::shared_mutex> lk(mu_);
  artifacts_.clear(); graph_ = DependencyGraph(); content_store_.clear();
  content_index_.clear(); kind_index_.clear(); producer_index_.clear();
  model_index_.clear(); adapter_index_.clear(); runtime_index_.clear();
  toolchain_index_.clear(); arch_index_.clear(); lifecycle_index_.clear();
  validation_index_.clear(); provenance_index_.clear(); generation_index_.clear();
  successor_of_.clear(); predecessor_of_.clear(); invalidation_cause_.clear(); quarantine_records_.clear();

  std::unordered_set<ArtifactId> seen;
  for (const auto& desc : loaded) {
    if (!seen.insert(desc.id).second) throw PersistenceError("duplicate artifact id in image");
    if (artifacts_.count(desc.id)) throw PersistenceError("duplicate artifact id");
    artifacts_[desc.id] = desc;
    graph_.add_node(desc.id);
    for (const auto& dep : desc.dependencies) {
      if (dep.artifact == desc.id) throw PersistenceError("self-dependency in image");
      if (!graph_.has_node(dep.artifact)) graph_.add_node(dep.artifact);
      graph_.add_edge(desc.id, dep);
    }
  }
  epoch_ = epoch; boot_ = boot; next_artifact_generation_ = ArtifactGeneration(nextgen);
  content_store_ = std::move(content);
  successor_of_ = std::move(succ);
  for (const auto& kv : successor_of_) for (const auto& v : kv.second) predecessor_of_[v].push_back(kv.first);
  rebuild_indexes_unlocked();

  accounts_ = Accountant();
  std::int64_t logical_bytes = 0;
  for (const auto& kv : artifacts_) {
    logical_bytes += static_cast<std::int64_t>(kv.second.size_bytes);
    accounts_.add_logical_artifact();
  }
  accounts_.add_logical_bytes(logical_bytes);
  for (const auto& kv : content_store_) {
    accounts_.add_physical_backing();
    accounts_.add_physical_bytes(static_cast<std::int64_t>(kv.second.bytes.size()));
    accounts_.add_active_reference(static_cast<std::int64_t>(kv.second.refcount));
  }
  accounts_.add_dedup_bytes(static_cast<std::int64_t>(std::max<std::int64_t>(0, logical_bytes - accounts_.snapshot().physical_bytes)));
}

void Catalog::save_file(const std::string& path) const {
  auto img = save();
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) throw std::runtime_error("cannot open catalog file for write: " + path);
  f.write(reinterpret_cast<const char*>(img.data()), static_cast<std::streamsize>(img.size()));
  if (!f) throw std::runtime_error("catalog file write failed");
}
void Catalog::load_file(const std::string& path, bool recover) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("cannot open catalog file for read: " + path);
  std::vector<std::uint8_t> img((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  load(img, recover);
}

// ---------------------------------------------------------------------------
// Content store
// ---------------------------------------------------------------------------
bool Catalog::content_of(ArtifactId id, std::vector<std::uint8_t>& out) const {
  std::shared_lock<std::shared_mutex> lk(mu_);
  auto it = artifacts_.find(id);
  if (it == artifacts_.end()) return false;
  auto cs = content_store_.find(it->second.content_digest);
  if (cs == content_store_.end()) return false;
  out = cs->second.bytes;
  return true;
}
bool Catalog::has_content(ArtifactId id) const {
  std::shared_lock<std::shared_mutex> lk(mu_);
  auto it = artifacts_.find(id);
  if (it == artifacts_.end()) return false;
  return content_store_.count(it->second.content_digest) != 0;
}
std::size_t Catalog::physical_backing_count() const { std::shared_lock<std::shared_mutex> lk(mu_); return content_store_.size(); }
std::size_t Catalog::content_reference_count(const ContentDigest& c) const {
  std::shared_lock<std::shared_mutex> lk(mu_);
  auto it = content_store_.find(c);
  return it == content_store_.end() ? 0 : it->second.refcount;
}

// ---------------------------------------------------------------------------
// Accounting / counts
// ---------------------------------------------------------------------------
Accounting Catalog::accounting() const {
  std::shared_lock<std::shared_mutex> lk(mu_);
  auto a = accounts_.snapshot();
  a.dedup_bytes = a.logical_bytes - a.physical_bytes;
  return a;
}
bool Catalog::accounting_is_zero() const { return accounting().is_zero(); }
std::size_t Catalog::artifact_count() const { std::shared_lock<std::shared_mutex> lk(mu_); return artifacts_.size(); }
std::vector<ArtifactId> Catalog::all_artifacts() const {
  std::shared_lock<std::shared_mutex> lk(mu_);
  std::vector<ArtifactId> v;
  for (const auto& kv : artifacts_) v.push_back(kv.first);
  std::sort(v.begin(), v.end());
  return v;
}
void Catalog::recompute_closure_digest(ArtifactId id) {
  auto it = artifacts_.find(id);
  if (it == artifacts_.end()) return;
  it->second.dependency_digest = graph_.dependency_digest(id);
  refresh_descriptor_digests(it->second);
}

// ---------------------------------------------------------------------------
// Explanation
// ---------------------------------------------------------------------------
Explanation Catalog::explain(ArtifactId id) const {
  std::shared_lock<std::shared_mutex> lk(mu_);
  Explanation ex;
  if (artifacts_.count(id) == 0) { ex.artifact = id; ex.why_exists = "unknown artifact"; return ex; }
  const auto& d = artifacts_.at(id);
  ex.artifact = id;
  ex.why_exists = "artifact managed by catalog";
  ex.origin = "producer=" + d.producer.to_string() + " generation=" + d.producer_generation.to_string();
  ex.producer = d.producer;
  ex.producer_summary = "producer " + d.producer.to_string() + " gen " + d.producer_generation.to_string();
  ex.provenance = d.provenance;
  ex.provenance_summary = "provenance " + d.provenance.to_string() + " gen " + d.provenance_generation.to_string();
  for (const auto& dep : graph_.direct_dependencies(id)) ex.dependencies.push_back(dep.artifact);
  std::sort(ex.dependencies.begin(), ex.dependencies.end());
  ex.validity_summary = std::string("validation=") + validation_state_name(d.validation_state) +
                        " lifecycle=" + lifecycle_name(d.lifecycle);
  bool reusable = d.validation_state == ValidationState::VALID &&
                  (d.lifecycle == LifecycleState::PUBLISHED || d.lifecycle == LifecycleState::ACTIVE);
  ex.reuse_summary = reusable ? "REUSABLE" : "NOT_REUSABLE";
  ex.authoritative_generation = d.generation;
  for (const auto& p : d.placements) ex.placement_summary.push_back(p.locator + " (" + storage_kind_name(p.kind) + ")");
  auto si = successor_of_.find(id);
  if (si != successor_of_.end()) ex.superseded_by = si->second;
  auto pi = predecessor_of_.find(id);
  if (pi != predecessor_of_.end()) ex.superseded_predecessor = pi->second;
  auto ic = invalidation_cause_.find(id);
  if (ic != invalidation_cause_.end()) ex.invalidation_cause = ic->second;
  ex.facts.push_back("artifact_generation=" + d.generation.to_string());
  ex.facts.push_back("content=" + d.content_digest.to_string());
  return ex;
}

}  // namespace af
