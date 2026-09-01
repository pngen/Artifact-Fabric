#pragma once
// Artifact Fabric - guarded artifact lifecycle state machine.
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace af {

enum class LifecycleState : std::int32_t {
  DECLARED = 0,
  BUILDING = 1,
  VALIDATING = 2,
  VALID = 3,
  PUBLISHED = 4,
  ACTIVE = 5,
  STALE = 6,
  INVALIDATED = 7,
  SUPERSEDED = 8,
  QUARANTINED = 9,
  EVICTED = 10,
  RETIRED = 11,
  FAILED = 12,
};

inline bool is_valid_lifecycle(std::int32_t v) { return v >= 0 && v <= 12; }
inline bool is_valid_lifecycle(LifecycleState s) { return is_valid_lifecycle(static_cast<std::int32_t>(s)); }

inline const char* lifecycle_name(LifecycleState s) {
  switch (s) {
    case LifecycleState::DECLARED: return "DECLARED";
    case LifecycleState::BUILDING: return "BUILDING";
    case LifecycleState::VALIDATING: return "VALIDATING";
    case LifecycleState::VALID: return "VALID";
    case LifecycleState::PUBLISHED: return "PUBLISHED";
    case LifecycleState::ACTIVE: return "ACTIVE";
    case LifecycleState::STALE: return "STALE";
    case LifecycleState::INVALIDATED: return "INVALIDATED";
    case LifecycleState::SUPERSEDED: return "SUPERSEDED";
    case LifecycleState::QUARANTINED: return "QUARANTINED";
    case LifecycleState::EVICTED: return "EVICTED";
    case LifecycleState::RETIRED: return "RETIRED";
    case LifecycleState::FAILED: return "FAILED";
  }
  return "UNKNOWN";
}

// Explicit, deterministic transition table. Invalid transitions are rejected.
inline bool can_transition(LifecycleState from, LifecycleState to) {
  switch (from) {
    case LifecycleState::DECLARED:
      return to == LifecycleState::BUILDING || to == LifecycleState::FAILED || to == LifecycleState::RETIRED;
    case LifecycleState::BUILDING:
      return to == LifecycleState::VALIDATING || to == LifecycleState::FAILED || to == LifecycleState::QUARANTINED;
    case LifecycleState::VALIDATING:
      return to == LifecycleState::VALID || to == LifecycleState::FAILED || to == LifecycleState::QUARANTINED;
    case LifecycleState::VALID:
      return to == LifecycleState::PUBLISHED || to == LifecycleState::ACTIVE || to == LifecycleState::INVALIDATED ||
             to == LifecycleState::SUPERSEDED || to == LifecycleState::QUARANTINED || to == LifecycleState::STALE ||
             to == LifecycleState::RETIRED;
    case LifecycleState::PUBLISHED:
      return to == LifecycleState::ACTIVE || to == LifecycleState::INVALIDATED || to == LifecycleState::SUPERSEDED ||
             to == LifecycleState::QUARANTINED || to == LifecycleState::STALE || to == LifecycleState::RETIRED;
    case LifecycleState::ACTIVE:
      return to == LifecycleState::STALE || to == LifecycleState::INVALIDATED || to == LifecycleState::SUPERSEDED ||
             to == LifecycleState::QUARANTINED || to == LifecycleState::RETIRED || to == LifecycleState::EVICTED;
    case LifecycleState::STALE:
      return to == LifecycleState::INVALIDATED || to == LifecycleState::RETIRED || to == LifecycleState::ACTIVE;
    case LifecycleState::INVALIDATED:
      return to == LifecycleState::RETIRED || to == LifecycleState::QUARANTINED || to == LifecycleState::STALE;
    case LifecycleState::SUPERSEDED:
      return to == LifecycleState::RETIRED || to == LifecycleState::QUARANTINED || to == LifecycleState::STALE;
    case LifecycleState::QUARANTINED:
      return to == LifecycleState::RETIRED || to == LifecycleState::VALIDATING || to == LifecycleState::EVICTED;
    case LifecycleState::EVICTED:
      return to == LifecycleState::RETIRED;
    case LifecycleState::RETIRED:
    case LifecycleState::FAILED:
      return false;  // terminal
  }
  return false;
}

// Apply a transition, throwing on illegal transition.
inline void apply_transition(LifecycleState& s, LifecycleState to) {
  if (!can_transition(s, to)) {
    throw std::logic_error(std::string("illegal lifecycle transition: ") + lifecycle_name(s) + " -> " + lifecycle_name(to));
  }
  s = to;
}

}  // namespace af
