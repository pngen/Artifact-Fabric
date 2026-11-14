// Artifact Fabric - regression tests for audited defects: fenced mutations
// atomic with epoch rollover, rejected lifecycle transitions leave state and
// indexes unchanged, invalidation propagates past dependents that cannot be
// invalidated, transactional publication honours the lifecycle and accounts
// shared content once, a rejected publish or load leaves no trace, and an
// aborted transaction can be retried while a committed one cannot be aborted.
#include "af_test.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "artifact_fabric/catalog.hpp"
#include "artifact_fabric/persistence.hpp"

using namespace af;

namespace {

const ProducerId& producer() {
  static const ProducerId p = ProducerId::random();
  return p;
}

AuthorityEnvelope auth_now(const Catalog& cat) {
  AuthorityEnvelope a;
  a.epoch = cat.epoch();
  a.boot = cat.boot();
  a.producer = producer();
  a.producer_generation = ProducerGeneration(1);
  return a;
}

ArtifactDescriptor desc(const std::string& shape, ArtifactKind kind = ArtifactKind::COMPILED_KERNEL) {
  ArtifactDescriptor d;
  d.kind = kind;
  d.generation = ArtifactGeneration(1);
  d.provenance = ProvenanceId::random();
  d.provenance_generation = ProvenanceGeneration(1);
  d.created_ns = 1000;
  d.architecture = "x86_64";
  d.compute_capability = "12.0";
  d.abi = "sm_120";
  d.dtype = "fp16";
  d.layout = "NHWC";
  d.shape = shape;
  d.size_bytes = 4;
  return d;
}

PublishResult publish(Catalog& cat, const ArtifactDescriptor& d, std::vector<std::uint8_t> content) {
  PublishRequest req;
  req.descriptor = d;
  req.content = std::move(content);
  req.authority = auth_now(cat);
  return cat.publish(req);
}

DependencyRef dep_on(const PublishResult& p) {
  DependencyRef r;
  r.artifact = p.id;
  r.generation = DependencyGeneration(1);
  r.digest = p.content_digest;
  return r;
}

bool contains(const std::vector<ArtifactId>& v, const ArtifactId& id) {
  return std::find(v.begin(), v.end(), id) != v.end();
}

ValidationReport report(bool ok) {
  ValidationReport r;
  r.state = ok ? ValidationState::VALID : ValidationState::INVALID;
  return r;
}

}  // namespace

AF_TEST(regression_fenced_publish_is_atomic_with_epoch_roll) {
  // Once roll_epoch() has returned, no publish fenced with the old epoch may
  // still commit.  Each publish records its epoch in `architecture`.
  Catalog cat;
  cat.set_authority(1, WorkerBootId::random());
  constexpr int kPublishers = 4, kPerPublisher = 300;
  std::atomic<bool> rolling{false};
  std::atomic<int> done{0};
  std::vector<std::pair<std::uint64_t, std::size_t>> after_roll;  // (old epoch, its artifacts right after the roll)
  std::thread roller([&] {
    constexpr std::size_t kMaxRolls = 5000;   // bounds time and memory under load
    do {
      cat.roll_epoch(WorkerBootId::random());
      const std::uint64_t old = cat.epoch() - 1;
      after_roll.emplace_back(old, cat.by_architecture("e" + std::to_string(old)).size());
      rolling.store(true);
      std::this_thread::yield();
    } while (done.load() < kPublishers && after_roll.size() < kMaxRolls);
  });
  std::vector<std::thread> publishers;
  for (int t = 0; t < kPublishers; ++t) {
    publishers.emplace_back([&, t] {
      while (!rolling.load()) std::this_thread::yield();
      const std::vector<std::uint8_t> content(64 * 1024, static_cast<std::uint8_t>(t));
      for (int i = 0; i < kPerPublisher; ++i) {
        PublishRequest req;
        req.authority = auth_now(cat);
        req.descriptor = desc(std::to_string(t) + "x" + std::to_string(i));
        req.descriptor.architecture = "e" + std::to_string(req.authority.epoch);
        req.descriptor.size_bytes = content.size();
        req.content = content;
        try { (void)cat.publish(req); } catch (const std::exception&) {}
      }
      done.fetch_add(1);
    });
  }
  for (auto& t : publishers) t.join();
  roller.join();
  std::size_t late = 0;
  for (const auto& [epoch, count] : after_roll)
    if (cat.by_architecture("e" + std::to_string(epoch)).size() != count) ++late;
  AF_CHECK(!after_roll.empty());
  AF_CHECK(late == 0);
}

AF_TEST(regression_rejected_transition_leaves_state_and_indexes) {
  Catalog cat;
  cat.set_authority(1, WorkerBootId::random());
  PublishResult p = publish(cat, desc("1x1"), {1, 2, 3, 4});
  cat.promote(p.id, auth_now(cat));
  AF_THROWS(cat.promote(p.id, auth_now(cat)));   // ACTIVE -> ACTIVE is illegal
  AF_CHECK(contains(cat.by_lifecycle(LifecycleState::ACTIVE), p.id));
  AF_CHECK(contains(cat.by_kind(ArtifactKind::COMPILED_KERNEL), p.id));
  AF_CHECK(contains(cat.find_by_content(p.content_digest), p.id));

  cat.retire(p.id, auth_now(cat));
  const Accounting before = cat.accounting();
  AF_THROWS(cat.retire(p.id, auth_now(cat)));    // RETIRED is terminal
  AF_CHECK(contains(cat.by_lifecycle(LifecycleState::RETIRED), p.id));
  AF_CHECK(cat.accounting().logical_artifacts == before.logical_artifacts);

  const ValidationState vs = cat.find(p.id)->validation_state;
  QuarantineRecord qr; qr.detection_source = "test";
  AF_THROWS(cat.quarantine(p.id, qr, auth_now(cat)));
  AF_CHECK(cat.find(p.id)->validation_state == vs);
  AF_CHECK(contains(cat.by_validation(vs), p.id));
}

AF_TEST(regression_invalidation_propagates_past_quarantined_dependent) {
  Catalog cat;
  cat.set_authority(1, WorkerBootId::random());
  PublishResult a = publish(cat, desc("a"), {1, 1, 1, 1});
  ArtifactDescriptor bd = desc("b", ArtifactKind::ENGINE_ARTIFACT); bd.dependencies.push_back(dep_on(a));
  PublishResult b = publish(cat, bd, {2, 2, 2, 2});
  ArtifactDescriptor cd = desc("c", ArtifactKind::EXECUTION_GRAPH); cd.dependencies.push_back(dep_on(b));
  PublishResult c = publish(cat, cd, {3, 3, 3, 3});
  QuarantineRecord qr; qr.detection_source = "test";
  cat.quarantine(b.id, qr, auth_now(cat));

  bool invalidate_failed = false;
  try { cat.invalidate(a.id, "dependency corrupt", auth_now(cat)); } catch (const std::exception&) { invalidate_failed = true; }
  AF_CHECK(!invalidate_failed);
  AF_CHECK(cat.find(a.id)->lifecycle == LifecycleState::INVALIDATED);
  AF_CHECK(cat.find(b.id)->lifecycle == LifecycleState::QUARANTINED);
  AF_CHECK(cat.find(c.id)->lifecycle == LifecycleState::INVALIDATED);
  AF_CHECK(contains(cat.by_lifecycle(LifecycleState::QUARANTINED), b.id));
  AF_CHECK(contains(cat.by_kind(ArtifactKind::ENGINE_ARTIFACT), b.id));
}

AF_TEST(regression_failed_validation_is_never_committed) {
  Catalog cat;
  cat.set_authority(1, WorkerBootId::random());
  auto tx = cat.begin_publish();
  PublishRequest req; req.descriptor = desc("tx"); req.content = {4, 4, 4, 4}; req.authority = auth_now(cat);
  ArtifactId id = tx.reserve(req);
  tx.produce(id, req.content, auth_now(cat));
  tx.validate_publish(id, report(false), auth_now(cat));
  AF_THROWS(tx.commit(id, auth_now(cat)));
  AF_CHECK(cat.find(id)->lifecycle == LifecycleState::FAILED);
  AF_CHECK(!contains(cat.by_lifecycle(LifecycleState::PUBLISHED), id));
  AF_CHECK(cat.accounting().logical_artifacts == 0);
  tx.abort(id);
  AF_CHECK(cat.accounting_is_zero());

  // Committing straight after the reservation skips validation: refused.
  auto tx2 = cat.begin_publish();
  PublishRequest req2; req2.descriptor = desc("tx2"); req2.content = {5, 5, 5, 5}; req2.authority = auth_now(cat);
  ArtifactId id2 = tx2.reserve(req2);
  AF_THROWS(tx2.commit(id2, auth_now(cat)));
  AF_CHECK(!contains(cat.by_lifecycle(LifecycleState::PUBLISHED), id2));
}

AF_TEST(regression_transaction_accounts_shared_content_once) {
  Catalog cat;
  cat.set_authority(1, WorkerBootId::random());
  const std::vector<std::uint8_t> content = {7, 7, 7, 7};
  PublishResult a = publish(cat, desc("shared-a"), content);
  auto tx = cat.begin_publish();
  PublishRequest req; req.descriptor = desc("shared-b", ArtifactKind::TENSOR_ARTIFACT); req.content = content; req.authority = auth_now(cat);
  ArtifactId b = tx.reserve(req);
  tx.produce(b, content, auth_now(cat));
  tx.validate_publish(b, report(true), auth_now(cat));
  tx.commit(b, auth_now(cat));
  AF_CHECK(cat.physical_backing_count() == 1);
  AF_CHECK(cat.accounting().physical_backing == 1);
  cat.retire(a.id, auth_now(cat));
  cat.retire(b, auth_now(cat));
  AF_CHECK(cat.accounting_is_zero());
}

AF_TEST(regression_physical_bytes_follow_stored_content) {
  // A declared size that differs from the stored bytes must not unbalance
  // physical accounting (retire subtracts the stored size).
  Catalog cat;
  cat.set_authority(1, WorkerBootId::random());
  ArtifactDescriptor d = desc("sized"); d.size_bytes = 4;
  PublishResult p = publish(cat, d, {1, 2, 3, 4, 5, 6, 7, 8});
  bool retire_failed = false;
  try { cat.retire(p.id, auth_now(cat)); } catch (const std::exception&) { retire_failed = true; }
  AF_CHECK(!retire_failed);
  AF_CHECK(cat.find(p.id)->lifecycle == LifecycleState::RETIRED);
  AF_CHECK(cat.accounting_is_zero());
}

AF_TEST(regression_rejected_publish_leaves_no_trace) {
  Catalog cat;
  cat.set_authority(1, WorkerBootId::random());
  PublishResult a = publish(cat, desc("base"), {1, 1, 1, 1});
  const Accounting before = cat.accounting();

  ArtifactDescriptor dup = desc("dup"); dup.dependencies = {dep_on(a), dep_on(a)};   // duplicate edge
  AF_THROWS(publish(cat, dup, {2, 2, 2, 2}));
  ArtifactDescriptor self = desc("self"); self.id = ArtifactId::random();
  DependencyRef own; own.artifact = self.id; own.generation = DependencyGeneration(1);
  self.dependencies = {own};                                                       // self-dependency
  AF_THROWS(publish(cat, self, {3, 3, 3, 3}));

  AF_CHECK(cat.artifact_count() == 1);
  AF_CHECK(cat.physical_backing_count() == 1);
  AF_CHECK(cat.by_lifecycle(LifecycleState::BUILDING).empty());
  AF_CHECK(cat.accounting().physical_bytes == before.physical_bytes);
  AF_CHECK(cat.accounting().logical_artifacts == before.logical_artifacts);
  AF_CHECK(cat.dependents_of(a.id).empty());
}

AF_TEST(regression_refused_load_leaves_catalog_unchanged) {
  Catalog cat;
  cat.set_authority(5, WorkerBootId::random());
  PublishResult p = publish(cat, desc("kept"), {9, 9, 9, 9});

  // An image holding the same artifact twice.
  ArtifactDescriptor x = desc("x"); x.id = ArtifactId::random(); x.lifecycle = LifecycleState::PUBLISHED;
  Encoder e;
  e.u32(kArtifactImageMagic);
  e.u32(2u);
  e.u64(1);
  e.id(WorkerBootId::random());
  e.u64(1);
  e.u32(2);
  write_descriptor(e, x);
  write_descriptor(e, x);
  e.u32(0);
  e.u32(0);
  AF_THROWS(cat.load(e.finalize()));

  AF_CHECK(cat.epoch() == 5);
  AF_CHECK(cat.contains(p.id));
  AF_CHECK(!cat.contains(x.id));
  std::vector<std::uint8_t> content;
  AF_CHECK(cat.content_of(p.id, content) && content.size() == 4);
  AF_CHECK(contains(cat.by_kind(ArtifactKind::COMPILED_KERNEL), p.id));
}

AF_TEST(regression_aborted_transaction_can_be_retried) {
  Catalog cat;
  cat.set_authority(1, WorkerBootId::random());
  PublishResult a = publish(cat, desc("dep"), {1, 1, 1, 1});
  PublishRequest req; req.descriptor = desc("retry"); req.descriptor.dependencies = {dep_on(a)};
  req.content = {6, 6, 6, 6}; req.authority = auth_now(cat);

  auto tx = cat.begin_publish();
  ArtifactId id = tx.reserve(req);
  tx.produce(id, req.content, auth_now(cat));
  tx.validate_publish(id, report(true), auth_now(cat));
  tx.verify_dependencies(id, auth_now(cat));
  tx.abort(id);
  AF_CHECK(!cat.contains(id));
  AF_CHECK(cat.dependents_of(a.id).empty());

  auto retry = cat.begin_publish();
  ArtifactId id2 = retry.reserve(req);
  retry.produce(id2, req.content, auth_now(cat));
  retry.validate_publish(id2, report(true), auth_now(cat));
  bool retry_failed = false;
  try { retry.verify_dependencies(id2, auth_now(cat)); retry.commit(id2, auth_now(cat)); }
  catch (const std::exception&) { retry_failed = true; }
  AF_CHECK(!retry_failed);
  AF_CHECK(cat.find(id2) != nullptr && cat.find(id2)->lifecycle == LifecycleState::PUBLISHED);

  // A committed publication cannot be aborted.
  AF_THROWS(retry.abort(id2));
  AF_CHECK(cat.contains(id2));
  AF_CHECK(cat.find(id2)->lifecycle == LifecycleState::PUBLISHED);
}

int main() { return testfw::run_all(); }
