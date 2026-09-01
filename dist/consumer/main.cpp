#include <cstdio>
#include "artifact_fabric/catalog.hpp"
#include "artifact_fabric/descriptor.hpp"
using namespace af;
int main() {
  Catalog cat; cat.set_authority(1, WorkerBootId::random());
  ArtifactDescriptor d; d.kind = ArtifactKind::COMPILED_KERNEL; d.generation = ArtifactGeneration(1);
  d.provenance = ProvenanceId::random(); d.provenance_generation = ProvenanceGeneration(1);
  d.architecture = "x86_64"; d.compute_capability = "12.0"; d.abi = "sm_120"; d.size_bytes = 4;
  PublishRequest req; req.descriptor = d; req.content = {1,2,3,4};
  AuthorityEnvelope a; a.epoch = cat.epoch(); a.boot = cat.boot(); a.producer = ProducerId::random();
  a.producer_generation = ProducerGeneration(1); req.authority = a;
  PublishResult p = cat.publish(req);
  const ArtifactDescriptor* g = cat.find(p.id);
  std::printf("downstream consumer: published %s lifecycle=%s\n", p.id.to_string().c_str(), lifecycle_name(g->lifecycle));
  return g ? 0 : 1;
}
