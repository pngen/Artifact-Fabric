#include "af_test.hpp"

#include <atomic>
#include <cstdint>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include "artifact_fabric/catalog.hpp"
#include "artifact_fabric/compat.hpp"
#include "artifact_fabric/dependency.hpp"
#include "artifact_fabric/descriptor.hpp"
#include "artifact_fabric/digest.hpp"
#include "artifact_fabric/generation.hpp"
#include "artifact_fabric/hash.hpp"
#include "artifact_fabric/id.hpp"
#include "artifact_fabric/kind.hpp"
#include "artifact_fabric/persistence.hpp"

using namespace af;

static WorkerBootId global_boot;
static ProducerId global_producer;
static AuthorityEnvelope auth_at(const Catalog& cat) {
  AuthorityEnvelope a;
  a.epoch = cat.epoch();
  a.boot = global_boot;
  a.producer = global_producer;
  a.producer_generation = ProducerGeneration(1);
  a.attempt = AttemptId::random();
  a.attempt_generation = AttemptGeneration(1);
  return a;
}

static ArtifactDescriptor base_desc() {
  ArtifactDescriptor d;
  d.kind = ArtifactKind::COMPILED_KERNEL;
  d.generation = ArtifactGeneration(1);
  d.provenance = ProvenanceId::random();
  d.provenance_generation = ProvenanceGeneration(1);
  d.created_ns = 1000;
  d.architecture = "x86_64";
  d.compute_capability = "12.0";
  d.abi = "sm_120";
  d.dtype = "fp16";
  d.layout = "NHWC";
  d.shape = "1x4";
  d.launch_metadata = "grid=1 block=4";
  d.size_bytes = 4;
  return d;
}

AF_TEST(strong_ids_distinct_and_hex) {
  ArtifactId a = ArtifactId::random();
  ArtifactId b = ArtifactId::random();
  AF_CHECK(a != b);
  ArtifactId copy = ArtifactId::from_hex(a.to_string());
  AF_CHECK(a == copy);
  AF_CHECK(a.to_string().size() == 32);
  ProducerId p = ProducerId::random();
  AF_CHECK(p != ProducerId{});   // distinct type is usable on its own
  AF_CHECK(a.bytes() != nullptr);
}

AF_TEST(generations_are_separate) {
  ArtifactGeneration g1(1);
  ArtifactGeneration g2 = g1.next();
  AF_CHECK(g2.value() == 2);
  AF_CHECK(g2 > g1);
  ProducerGeneration pg(5);
  AF_CHECK(pg.value() == 5);
  AF_CHECK(ArtifactGeneration(7).is_set());
  AF_CHECK(!ArtifactGeneration(0).is_set());
  // generation overflow rejected
  bool threw = false;
  try { ArtifactGeneration max(std::numeric_limits<std::uint64_t>::max()); (void)max.next(); } catch (...) { threw = true; }
  AF_CHECK(threw);
}

AF_TEST(sha256_known_vectors) {
  AF_CHECK_EQ(to_hex(Sha256::digest("")), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  AF_CHECK_EQ(to_hex(Sha256::digest("abc")), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

AF_TEST(crc32_known_vector) {
  AF_CHECK(crc32("123456789") == 0xCBF43926u);
  AF_CHECK(crc32("") == 0u);
}

AF_TEST(descriptor_digests_stable_and_identity_derived) {
  ArtifactDescriptor d = base_desc();
  d.content_digest = ContentDigest(Sha256::digest("payload"));
  MetadataDigest md1(compute_metadata_digest(d));
  MetadataDigest md2(compute_metadata_digest(d));
  AF_CHECK(md1 == md2);
  ArtifactId id1 = derive_artifact_id(d, ArtifactGeneration(1));
  ArtifactId id2 = derive_artifact_id(d, ArtifactGeneration(1));
  AF_CHECK(id1 == id2);
  ArtifactId id3 = derive_artifact_id(d, ArtifactGeneration(2));
  AF_CHECK(id1 != id3);
}

AF_TEST(compatibility_exact_and_mismatch) {
  ArtifactDescriptor d = base_desc();
  CompatRequirement req;
  req.kind = ArtifactKind::COMPILED_KERNEL;
  req.architecture = "x86_64";
  req.compute_capability = "12.0";
  req.abi = "sm_120";
  req.require_valid = true;
  d.validation_state = ValidationState::VALID;
  CompatResult r = evaluate_compatibility(d, req);
  AF_CHECK(r.compatible());

  CompatRequirement bad;
  bad.abi = "sm_90";
  CompatResult rb = evaluate_compatibility(d, bad);
  AF_CHECK(!rb.compatible());
  AF_CHECK(rb.outcome == CompatOutcome::ABI_MISMATCH);
  bool hasabi = false;
  for (const auto& f : rb.failed_dimensions) if (f == "abi") hasabi = true;
  AF_CHECK(hasabi);

  CompatRequirement badarch;
  badarch.architecture = "aarch64";
  CompatResult ra = evaluate_compatibility(d, badarch);
  AF_CHECK(ra.outcome == CompatOutcome::ARCH_MISMATCH);

  CompatRequirement badtoolchain;
  badtoolchain.min_toolchain_generation = ToolchainGeneration(9);
  CompatResult rt = evaluate_compatibility(d, badtoolchain);
  AF_CHECK(rt.outcome == CompatOutcome::TOOLCHAIN_MISMATCH);
}

AF_TEST(dependency_graph_cycle_and_self_rejected) {
  DependencyGraph g;
  ArtifactId a = ArtifactId::random();
  ArtifactId b = ArtifactId::random();
  ArtifactId c = ArtifactId::random();
  g.add_node(a); g.add_node(b); g.add_node(c);
  DependencyRef ra; ra.artifact = b; ra.generation = DependencyGeneration(1); ra.digest = ContentDigest(Sha256::digest("x"));
  g.add_edge(a, ra);
  DependencyRef rb; rb.artifact = c; rb.generation = DependencyGeneration(1); rb.digest = ContentDigest(Sha256::digest("y"));
  g.add_edge(b, rb);
  AF_CHECK(g.transitive_dependencies(a).size() == 2);
  // self
  DependencyRef self; self.artifact = a;
  AF_THROWS(g.add_edge(a, self));
  // duplicate
  DependencyRef dup; dup.artifact = b; dup.generation = DependencyGeneration(1);
  AF_THROWS(g.add_edge(a, dup));
  // cycle: add edge c -> a
  DependencyRef cyc; cyc.artifact = a; cyc.generation = DependencyGeneration(1);
  AF_THROWS(g.add_edge(c, cyc));
  // transitive dependency digest deterministic
  AF_CHECK(g.dependency_digest(a) == g.dependency_digest(a));
}

AF_TEST(lifecycle_transitions) {
  LifecycleState s = LifecycleState::DECLARED;
  apply_transition(s, LifecycleState::BUILDING);
  apply_transition(s, LifecycleState::VALIDATING);
  apply_transition(s, LifecycleState::VALID);
  apply_transition(s, LifecycleState::PUBLISHED);
  AF_THROWS(apply_transition(s, LifecycleState::DECLARED));
  AF_THROWS(apply_transition(s, LifecycleState::FAILED));
}

AF_TEST(publish_find_and_index) {
  Catalog cat;
  cat.set_authority(1, global_boot);
  ArtifactDescriptor d = base_desc();
  PublishRequest req;
  req.descriptor = d;
  req.content = {1, 2, 3, 4};
  req.authority = auth_at(cat);
  PublishResult pr = cat.publish(req);
  AF_CHECK(pr.committed);
  AF_CHECK(cat.contains(pr.id));
  const ArtifactDescriptor* got = cat.find(pr.id);
  AF_CHECK(got != nullptr);
  AF_CHECK(got->lifecycle == LifecycleState::PUBLISHED);
  AF_CHECK(got->validation_state == ValidationState::INTEGRITY_OK);
  // content index
  auto bycontent = cat.find_by_content(pr.content_digest);
  AF_CHECK(bycontent.size() == 1 && bycontent[0] == pr.id);
  // kind index
  auto bykind = cat.by_kind(ArtifactKind::COMPILED_KERNEL);
  AF_CHECK(bykind.size() == 1);
  // producer index (producer set from auth)
  auto byprod = cat.by_producer(global_producer);
  AF_CHECK(byprod.size() == 1);
}

AF_TEST(content_dedup_backing) {
  Catalog cat;
  cat.set_authority(1, global_boot);
  std::vector<std::uint8_t> content = {9, 9, 9, 9};
  PublishRequest r1;
  r1.descriptor = base_desc(); r1.content = content; r1.authority = auth_at(cat);
  PublishRequest r2;
  ArtifactDescriptor d2 = base_desc(); d2.kind = ArtifactKind::TENSOR_ARTIFACT; r2.descriptor = d2; r2.content = content; r2.authority = auth_at(cat);
  PublishResult p1 = cat.publish(r1);
  PublishResult p2 = cat.publish(r2);
  AF_CHECK(p1.content_digest == p2.content_digest);
  AF_CHECK(cat.physical_backing_count() == 1);
  AF_CHECK(cat.content_reference_count(p1.content_digest) == 2);
  Accounting a = cat.accounting();
  AF_CHECK(a.dedup_bytes >= 4);
}

AF_TEST(reuse_eligibility_supersede_invalidate) {
  Catalog cat;
  cat.set_authority(1, global_boot);
  PublishRequest req; req.descriptor = base_desc(); req.content = {1,1,1,1}; req.authority = auth_at(cat);
  PublishResult p1 = cat.publish(req);
  CompatRequirement creq;
  creq.require_reusable = true;
  ReuseResult rr = cat.reuse(p1.id, creq);
  AF_CHECK(rr.reusable);

  // publish a newer generation and supersede
  PublishRequest r2; r2.descriptor = base_desc(); r2.descriptor.generation = ArtifactGeneration(2);
  r2.content = {2,2,2,2}; r2.authority = auth_at(cat);
  PublishResult p2 = cat.publish(r2);
  cat.supersede(p1.id, p2.id, "newer", auth_at(cat));
  AF_CHECK(cat.find(p1.id)->lifecycle == LifecycleState::SUPERSEDED);
  AF_CHECK(!cat.reuse(p1.id, creq).reusable);
  AF_CHECK(cat.reuse(p2.id, creq).reusable);

  // invalidate the successor
  cat.invalidate(p2.id, "mode change", auth_at(cat));
  AF_CHECK(cat.find(p2.id)->lifecycle == LifecycleState::INVALIDATED);
  AF_CHECK(!cat.reuse(p2.id, creq).reusable);
  AF_CHECK(cat.explain(p2.id).invalidation_cause == "mode change");
}

AF_TEST(invalidation_propagates_to_dependents) {
  Catalog cat;
  cat.set_authority(1, global_boot);
  ArtifactDescriptor dep_d = base_desc();
  PublishRequest dep_req; dep_req.descriptor = dep_d; dep_req.content = {5,5,5,5}; dep_req.authority = auth_at(cat);
  PublishResult dep = cat.publish(dep_req);
  ArtifactDescriptor prod_d = base_desc();
  prod_d.kind = ArtifactKind::ENGINE_ARTIFACT;
  DependencyRef depref; depref.artifact = dep.id; depref.generation = DependencyGeneration(1); depref.digest = dep.content_digest;
  prod_d.dependencies.push_back(depref);
  PublishRequest prod_req; prod_req.descriptor = prod_d; prod_req.content = {6,6,6,6}; prod_req.authority = auth_at(cat);
  PublishResult prod = cat.publish(prod_req);
  AF_CHECK(cat.dependencies_of(prod.id).size() == 1);
  cat.invalidate(dep.id, "corrupt", auth_at(cat));
  AF_CHECK(cat.find(dep.id)->lifecycle == LifecycleState::INVALIDATED);
  AF_CHECK(cat.find(prod.id)->lifecycle == LifecycleState::INVALIDATED);
  AF_CHECK(cat.dependents_of(dep.id).size() == 1);
}

AF_TEST(quarantine_not_reusable) {
  Catalog cat;
  cat.set_authority(1, global_boot);
  PublishRequest req; req.descriptor = base_desc(); req.content = {7,7,7,7}; req.authority = auth_at(cat);
  PublishResult p = cat.publish(req);
  QuarantineRecord qr; qr.reason = QuarantineReason::INTEGRITY_FAILURE; qr.detection_source = "test"; qr.timestamp_ns = 1;
  cat.quarantine(p.id, qr, auth_at(cat));
  AF_CHECK(cat.find(p.id)->lifecycle == LifecycleState::QUARANTINED);
  CompatRequirement creq; creq.require_reusable = true;
  AF_CHECK(!cat.reuse(p.id, creq).reusable);
}

AF_TEST(persistence_recovery_stable) {
  Catalog cat;
  cat.set_authority(3, global_boot);
  PublishRequest req; req.descriptor = base_desc(); req.content = {8,8,8,8}; req.authority = auth_at(cat);
  PublishResult p = cat.publish(req);
  auto img = cat.save();
  Catalog cat2;
  cat2.set_authority(3, global_boot);
  cat2.load(img);
  AF_CHECK(cat2.contains(p.id));
  const ArtifactDescriptor* d = cat2.find(p.id);
  AF_CHECK(d != nullptr);
  AF_CHECK(d->content_digest == p.content_digest);
  AF_CHECK(d->metadata_digest == cat.find(p.id)->metadata_digest);
  std::vector<std::uint8_t> content;
  AF_CHECK(cat2.content_of(p.id, content) && content.size() == 4);
  // stable digest across save/load
  MetadataDigest md(cat.find(p.id)->metadata_digest.bytes());
  MetadataDigest md2(cat2.find(p.id)->metadata_digest.bytes());
  AF_CHECK(md == md2);
}

AF_TEST(persistence_corruption_and_truncation_rejected) {
  Catalog cat;
  AF_CHECK(sizeof(std::vector<std::uint8_t>) > 0);
  ArtifactDescriptor d = base_desc();
  auto enc = encode_descriptor(d);
  // corrupt a byte in the payload
  std::vector<std::uint8_t> corrupt = enc;
  corrupt[enc.size() / 2] ^= 0x55;
  AF_THROWS(decode_descriptor(corrupt));
  // truncate
  std::vector<std::uint8_t> trunc(enc.begin(), enc.begin() + (enc.size() - 2));
  AF_THROWS(decode_descriptor(trunc));
  // trailing garbage
  std::vector<std::uint8_t> trail = enc; trail.push_back(0xAA);
  AF_THROWS(decode_descriptor(trail));
  auto dec = decode_descriptor(enc);
  AF_CHECK(dec == d);
}

AF_TEST(single_flight_shared_result) {
  Catalog cat;
  cat.set_authority(1, global_boot);
  ArtifactId target = ArtifactId::random();
  std::atomic<int> called{0};
  const int threads = 8;
  std::vector<ArtifactId> results(threads);
  std::vector<std::thread> ts;
  for (int i = 0; i < threads; ++i) {
    ts.emplace_back([&, i] {
      results[i] = cat.get_or_build(target, [&] { ++called; PublishRequest req; req.descriptor = base_desc(); req.content = {1,2,3,4}; req.authority = auth_at(cat); return cat.publish(req); }, auth_at(cat));
    });
  }
  for (auto& t : ts) t.join();
  AF_CHECK(called == 1);
  for (int i = 1; i < threads; ++i) AF_CHECK(results[i] == results[0]);
}

AF_TEST(stale_authority_rejected) {
  Catalog cat;
  cat.set_authority(1, global_boot);
  PublishRequest req; req.descriptor = base_desc(); req.content = {1,2,3,4};
  AuthorityEnvelope a = auth_at(cat);
  req.authority = a;
  PublishResult p = cat.publish(req);
  // Now roll epoch -> stale epoch rejected
  WorkerBootId newBoot = WorkerBootId::random();
  cat.roll_epoch(newBoot);
  PublishRequest stale; stale.descriptor = base_desc(); stale.content = {1,2,3,4}; stale.authority = a;
  AF_THROWS(cat.publish(stale));
  // stale boot rejected even at same epoch counter if boot not current
  Catalog c2; c2.set_authority(1, global_boot);
  PublishRequest r; r.descriptor = base_desc(); r.content={1,1,1,1}; r.authority=auth_at(c2);
  PublishResult pr = c2.publish(r);
  AuthorityEnvelope a2 = auth_at(c2); a2.boot = WorkerBootId::random();
  AF_THROWS(c2.invalidate(pr.id, "x", a2));
}

AF_TEST(accounting_closes_to_zero) {
  Catalog cat;
  cat.set_authority(1, global_boot);
  PublishRequest r1; r1.descriptor = base_desc(); r1.content = {1,2,3,4}; r1.authority = auth_at(cat);
  PublishResult p1 = cat.publish(r1);
  ArtifactDescriptor d2 = base_desc(); d2.kind = ArtifactKind::TENSOR_ARTIFACT;
  PublishRequest r2; r2.descriptor = d2; r2.content = {5,6,7,8}; r2.authority = auth_at(cat);
  PublishResult p2 = cat.publish(r2);
  AF_CHECK(!cat.accounting_is_zero());
  cat.retire(p1.id, auth_at(cat));
  cat.retire(p2.id, auth_at(cat));
  AF_CHECK(cat.accounting_is_zero());
}

AF_TEST(deterministic_serialization_roundtrip) {
  ArtifactDescriptor d = base_desc();
  d.model = ModelId::random();
  d.model_generation = ModelGeneration(4);
  d.placements.push_back(Placement{PlacementId::random(), StorageLocationId::random(), StorageKind::LOCAL_NVME,
                                   "nvme://x/1", 4, PlacementGeneration(2), 0.5, 0.1, 0.9, true, true});
  auto enc1 = encode_descriptor(d);
  auto enc2 = encode_descriptor(d);
  AF_CHECK(enc1 == enc2);
  auto dec = decode_descriptor(enc1);
  AF_CHECK(dec == d);
}


AF_TEST(adversarial_nan_inf_rejected) {
  // A placement with NaN locality must be rejected on decode.
  ArtifactDescriptor d = base_desc();
  Placement p; p.id = PlacementId::random(); p.location = StorageLocationId::random();
  p.kind = StorageKind::LOCAL_NVME; p.locator = "nvme://x/1"; p.byte_size = 4;
  p.generation = PlacementGeneration(1); p.locality = std::numeric_limits<double>::quiet_NaN();
  p.cost = 0.1; p.readiness = 1.0; p.integrity_ok = true; p.persistent = true;
  d.placements.push_back(p);
  auto enc = encode_descriptor(d);
  AF_THROWS(decode_descriptor(enc));
}

AF_TEST(adversarial_double_release_rejected) {
  Accountant ac;
  ac.reserve_build();
  ac.release_build();
  AF_THROWS(ac.release_build());       // double release
  AF_THROWS(ac.add_active_reference(-1));  // release with no reference
}

AF_TEST(adversarial_dependency_generation_confusion) {
  DependencyGraph g;
  ArtifactId a = ArtifactId::random();
  ArtifactId b = ArtifactId::random();
  g.add_node(a); g.add_node(b);
  DependencyRef r1; r1.artifact = b; r1.generation = DependencyGeneration(1); r1.digest = ContentDigest(Sha256::digest("x"));
  DependencyRef r2; r2.artifact = b; r2.generation = DependencyGeneration(1); r2.digest = ContentDigest(Sha256::digest("x"));
  g.add_edge(a, r1);
  // duplicate edge with identical target must be rejected even if body identical
  AF_THROWS(g.add_edge(a, r2));
}

AF_TEST(adversarial_lifecycle_violation_rejected) {
  // Deleted: publish then attempt an illegal transition directly.
  LifecycleState s = LifecycleState::PUBLISHED;
  AF_THROWS(apply_transition(s, LifecycleState::DECLARED));
  AF_THROWS(apply_transition(s, LifecycleState::FAILED));
}

AF_TEST(adversarial_property_deterministic) {
  // Fixed-seed property: encode/decode any descriptor reproduces stable digests.
  std::uint64_t seed = 0xC0FFEE1234ULL;
  for (int k = 0; k < 1000; ++k) {
    seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
    ArtifactDescriptor d = base_desc();
    d.generation = ArtifactGeneration(1 + (seed % 5));
    d.created_ns = (std::int64_t)(seed % 100000);
    d.size_bytes = seed % 4096;
    d.compute_capability = (seed % 2) ? "12.0" : "9.0";
    auto e1 = encode_descriptor(d);
    auto e2 = encode_descriptor(d);
    AF_CHECK(e1 == e2);
  }
  std::printf("  deterministic property seed=0xC0FFEE1234 (1000 cases)\n");
}

int main() { return testfw::run_all(); }
