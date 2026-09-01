#pragma once
// Artifact Fabric - distributed mutation authority and fencing.
#include <cstdint>
#include <stdexcept>
#include <string>

#include "generation.hpp"
#include "id.hpp"

namespace af {

// A monotonically-increasing coordinator epoch. Passing a stale epoch to a
// mutation means the operation's authority has been rolled over and the
// mutation must be rejected.
using CoordinatorEpoch = std::uint64_t;

// The authority envelope captured at a point in time. Any mutation that runs
// under an envelope whose epoch/boot/generation is no longer current must be
// rejected deterministically.
struct AuthorityEnvelope {
  CoordinatorEpoch epoch = 0;
  WorkerId worker{};
  WorkerBootId boot{};
  AttemptId attempt{};
  AttemptGeneration attempt_generation{};
  ProducerId producer{};
  ProducerGeneration producer_generation{};
  ArtifactGeneration artifact_generation{};
  DependencyGeneration dependency_generation{};
  ProvenanceGeneration provenance_generation{};
  ToolchainGeneration toolchain_generation{};
  ModelGeneration model_generation{};
  AdapterGeneration adapter_generation{};
  PlacementGeneration placement_generation{};

  bool operator==(const AuthorityEnvelope& o) const {
    return epoch == o.epoch && worker == o.worker && boot == o.boot && attempt == o.attempt &&
           attempt_generation == o.attempt_generation && producer == o.producer &&
           producer_generation == o.producer_generation && artifact_generation == o.artifact_generation &&
           dependency_generation == o.dependency_generation && provenance_generation == o.provenance_generation &&
           toolchain_generation == o.toolchain_generation && model_generation == o.model_generation &&
           adapter_generation == o.adapter_generation && placement_generation == o.placement_generation;
  }
  bool operator!=(const AuthorityEnvelope& o) const { return !(*this == o); }

  std::string to_string() const {
    return "epoch=" + std::to_string(epoch) + " worker=" + worker.to_string() + " boot=" + boot.to_string();
  }
};

// The fence a mutation must pass: current coordinator epoch and current
// worker boot id. Stale epoch or stale boot => rejected.
struct Fence {
  CoordinatorEpoch epoch = 0;
  WorkerBootId boot{};
};

// Reason codes for authority rejection.
enum class AuthorityReject : std::int32_t {
  OK = 0,
  STALE_EPOCH = 1,
  STALE_BOOT = 2,
  STALE_ATTEMPT = 3,
  STALE_ATTEMPT_GENERATION = 4,
  STALE_ARTIFACT_GENERATION = 5,
  STALE_PRODUCER_GENERATION = 6,
  STALE_DEPENDENCY_GENERATION = 7,
  STALE_PROVENANCE_GENERATION = 8,
  STALE_TOOLCHAIN_GENERATION = 9,
  STALE_MODEL_GENERATION = 10,
  STALE_ADAPTER_GENERATION = 11,
  STALE_PLACEMENT_GENERATION = 12,
  WORKER_MISMATCH = 13,
};

inline const char* authority_reject_name(AuthorityReject r) {
  switch (r) {
    case AuthorityReject::OK: return "OK";
    case AuthorityReject::STALE_EPOCH: return "STALE_EPOCH";
    case AuthorityReject::STALE_BOOT: return "STALE_BOOT";
    case AuthorityReject::STALE_ATTEMPT: return "STALE_ATTEMPT";
    case AuthorityReject::STALE_ATTEMPT_GENERATION: return "STALE_ATTEMPT_GENERATION";
    case AuthorityReject::STALE_ARTIFACT_GENERATION: return "STALE_ARTIFACT_GENERATION";
    case AuthorityReject::STALE_PRODUCER_GENERATION: return "STALE_PRODUCER_GENERATION";
    case AuthorityReject::STALE_DEPENDENCY_GENERATION: return "STALE_DEPENDENCY_GENERATION";
    case AuthorityReject::STALE_PROVENANCE_GENERATION: return "STALE_PROVENANCE_GENERATION";
    case AuthorityReject::STALE_TOOLCHAIN_GENERATION: return "STALE_TOOLCHAIN_GENERATION";
    case AuthorityReject::STALE_MODEL_GENERATION: return "STALE_MODEL_GENERATION";
    case AuthorityReject::STALE_ADAPTER_GENERATION: return "STALE_ADAPTER_GENERATION";
    case AuthorityReject::STALE_PLACEMENT_GENERATION: return "STALE_PLACEMENT_GENERATION";
    case AuthorityReject::WORKER_MISMATCH: return "WORKER_MISMATCH";
  }
  return "UNKNOWN";
}

// Exception thrown when a mutation runs under stale authority. The rejection is
// deterministic and carries the exact reason.
class AuthorityError : public std::runtime_error {
 public:
  explicit AuthorityError(AuthorityReject code, const std::string& detail)
      : std::runtime_error(std::string("authority rejected (") + authority_reject_name(code) + "): " + detail),
        code_(code) {}
  AuthorityReject code() const { return code_; }

 private:
  AuthorityReject code_;
};

}  // namespace af
