// Artifact Fabric - benchmark harness. Reports workload sizes and units clearly.
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <thread>
#include <vector>

#include "artifact_fabric/catalog.hpp"
#include "artifact_fabric/compat.hpp"
#include "artifact_fabric/descriptor.hpp"
#include "artifact_fabric/hash.hpp"
#include "artifact_fabric/persistence.hpp"

using namespace af;

static double now_ms() { return std::clock() * 1000.0 / CLOCKS_PER_SEC; }

static Catalog& make_catalog(Catalog& cat, std::size_t n) {
  cat.set_authority(1, WorkerBootId::random());
  for (std::size_t i = 0; i < n; ++i) {
    ArtifactDescriptor d;
    d.kind = ArtifactKind::COMPILED_KERNEL; d.generation = ArtifactGeneration(1 + (i % 4));
    d.provenance = ProvenanceId::random(); d.provenance_generation = ProvenanceGeneration(1);
    d.architecture = "x86_64"; d.compute_capability = "12.0"; d.abi = "sm_120"; d.size_bytes = 4;
    PublishRequest r; r.descriptor = d; r.content = {(std::uint8_t)(i & 0xff),2,3,4};
    AuthorityEnvelope a; a.epoch = 1; a.boot = cat.boot(); a.producer = ProducerId::random();
    a.producer_generation = ProducerGeneration(1); r.authority = a;
    cat.publish(r);
  }
  return cat;
}


template <typename F>
static void report(const char* name, std::size_t iters, F f) {
  double t0 = now_ms();
  for (std::size_t i = 0; i < iters; ++i) f(i);
  double dt = now_ms() - t0;
  double per_us = dt * 1000.0 / (double)iters;
  std::printf("%-34s %10zu iter  %9.2f ms  %10.2f us/op  (%.0f op/s)\n", name, iters, dt, per_us, iters * 1000.0 / dt);
}

int main() {
  std::printf("Artifact Fabric benchmark\n======================\n");

  report("descriptor create + sha256", 20000, [](std::size_t i) {
    ArtifactDescriptor d;
    d.kind = ArtifactKind::COMPILED_KERNEL; d.generation = ArtifactGeneration(1);
    d.provenance = ProvenanceId::random(); d.architecture = "x86_64";
    (void)ContentDigest(Sha256::digest(&i, sizeof(i)));
  });

  report("sha256 (64 bytes)", 200000, [](std::size_t) {
    unsigned char b[64] = {0}; (void)Sha256::digest(b, 64);
  });

  std::size_t N = 2000;
  Catalog cat;
  make_catalog(cat, N);
  std::vector<ArtifactId> ids = cat.all_artifacts();
  std::printf("catalog size: %zu artifacts, %zu physical backing\n", ids.size(), cat.physical_backing_count());

  report("indexed lookup (by id)", N, [&](std::size_t i) { (void)cat.find(ids[i % ids.size()]); });
  report("indexed lookup (by kind)", N, [&](std::size_t) { (void)cat.by_kind(ArtifactKind::COMPILED_KERNEL).size(); });
  report("compatibility evaluate", N, [&](std::size_t i) {
    CompatRequirement r; r.compute_capability = "12.0"; r.abi = "sm_120";
    (void)evaluate_compatibility(*cat.find(ids[i % ids.size()]), r);
  });
  report("reuse eligibility", N, [&](std::size_t i) {
    CompatRequirement r; r.require_reusable = true;
    (void)cat.reuse(ids[i % ids.size()], r);
  });
  report("dependency traversal", N, [&](std::size_t i) { (void)cat.dependencies_of(ids[i % ids.size()]); });
  report("reverse-dependency traversal", N / 10, [&](std::size_t i) { (void)cat.dependents_of(ids[i % ids.size()]); });

  report("publication", 500, [&](std::size_t) {
    ArtifactDescriptor d; d.kind = ArtifactKind::TENSOR_ARTIFACT; d.generation = ArtifactGeneration(1);
    d.provenance = ProvenanceId::random(); d.size_bytes = 4;
    PublishRequest r; r.descriptor = d; r.content = {1,2,3,4};
    AuthorityEnvelope a; a.epoch = 1; a.boot = cat.boot(); a.producer = ProducerId::random();
    a.producer_generation = ProducerGeneration(1); r.authority = a;
    cat.publish(r);
  });

  report("serialization (descriptor encode)", 2000, [&](std::size_t i) {
    (void)encode_descriptor(*cat.find(ids[i % ids.size()]));  // uses i
  });

  auto img = cat.save();
  std::printf("catalog save image: %zu bytes\n", img.size());
  report("persistence save", 50, [&](std::size_t) { (void)cat.save(); });
  double tr0 = now_ms();
  for (int k = 0; k < 20; ++k) { Catalog c2; c2.load(img); }
  double trdt = now_ms() - tr0;
  std::printf("%-34s %10d iter  %9.2f ms  %10.2f ms/op  (recovery)\n", "persistence recover", 20, trdt, trdt / 20.0);

  report("explainability (explain)", 500, [&](std::size_t i) { (void)cat.explain(ids[i % ids.size()]).digest(); });

  {
    std::atomic<std::uint64_t> sink{0};
    std::vector<std::thread> ts;
    const int threads = 8;
    double t0 = now_ms();
    for (int t = 0; t < threads; ++t) ts.emplace_back([&] {
      for (int i = 0; i < 2000; ++i) { sink += cat.find(ids[(std::size_t)i % ids.size()])->size_bytes; }
    });
    for (auto& t : ts) t.join();
    double dt = now_ms() - t0;
    std::printf("%-34s %10d iter  %9.2f ms  %10.2f us/op  (8 threads)\n", "concurrent lookup", threads * 2000, dt, dt * 1000.0 / (threads * 2000));
    (void)sink;
  }

  {
    std::atomic<int> builds{0};
    std::vector<std::thread> ts; std::vector<ArtifactId> out(8); ArtifactId tgt = ArtifactId::random();
    double t0 = now_ms();
    AuthorityEnvelope a; a.epoch = 1; a.boot = cat.boot(); a.producer = ProducerId::random(); a.producer_generation = ProducerGeneration(1);
    for (int i = 0; i < 8; ++i) ts.emplace_back([&, i] {
      out[i] = cat.get_or_build(tgt, [&] { ++builds; ArtifactDescriptor d; d.kind=ArtifactKind::OTHER; d.generation=ArtifactGeneration(1); d.size_bytes=4;
        PublishRequest r; r.descriptor=d; r.content={9,9,9,9}; AuthorityEnvelope aa; aa.epoch=1; aa.boot=cat.boot(); aa.producer=ProducerId::random(); aa.producer_generation=ProducerGeneration(1); r.authority=aa; return cat.publish(r); }, a);
    });
    for (auto& t : ts) t.join();
    double dt = now_ms() - t0;
    std::printf("%-34s %10d iter  %9.2f ms  (builds=%d, single-flight)\n", "concurrent single-flight", 8, dt, builds.load());
    (void)out;
  }
  return 0;
}
