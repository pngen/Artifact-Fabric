// Artifact Fabric example include helpers.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "artifact_fabric/catalog.hpp"
#include "artifact_fabric/descriptor.hpp"
#include "artifact_fabric/digest.hpp"
#include "artifact_fabric/compat.hpp"
#include "artifact_fabric/reuse.hpp"

namespace afdemo {
using namespace af;
inline WorkerBootId boot;             // fixed boot for deterministic examples
inline AuthorityEnvelope auth_for(const Catalog& cat, ProducerId p) {
  AuthorityEnvelope a; a.epoch = cat.epoch(); a.boot = boot; a.producer = p;
  a.producer_generation = ProducerGeneration(1); a.attempt = AttemptId::random();
  a.attempt_generation = AttemptGeneration(1); return a;
}
inline ArtifactDescriptor kernel_desc(std::uint64_t gen, std::uint64_t size) {
  ArtifactDescriptor d;
  d.kind = ArtifactKind::COMPILED_KERNEL; d.generation = ArtifactGeneration(gen);
  d.provenance = ProvenanceId::random(); d.provenance_generation = ProvenanceGeneration(1);
  d.architecture = "x86_64"; d.compute_capability = "12.0"; d.abi = "sm_120";
  d.dtype = "fp16"; d.layout = "NHWC"; d.size_bytes = size; d.created_ns = 1000;
  return d;
}
}
