#pragma once
// Artifact Fabric - explicit, typed validation state.
#include <cstdint>
#include <string>
#include <vector>

namespace af {

enum class ValidationState : std::int32_t {
  UNVALIDATED = 0,
  VALIDATING = 1,
  INTEGRITY_OK = 2,
  VALID = 3,
  INVALID = 4,
  QUARANTINED = 5,
  INTEGRITY_FAILURE = 6,
};

inline bool is_valid_validation(std::int32_t v) { return v >= 0 && v <= 6; }

inline const char* validation_state_name(ValidationState s) {
  switch (s) {
    case ValidationState::UNVALIDATED: return "UNVALIDATED";
    case ValidationState::VALIDATING: return "VALIDATING";
    case ValidationState::INTEGRITY_OK: return "INTEGRITY_OK";
    case ValidationState::VALID: return "VALID";
    case ValidationState::INVALID: return "INVALID";
    case ValidationState::QUARANTINED: return "QUARANTINED";
    case ValidationState::INTEGRITY_FAILURE: return "INTEGRITY_FAILURE";
  }
  return "UNKNOWN";
}

// A single validation check result within a report.
struct ValidationCheck {
  std::string name;       // integrity | compatibility | dependency | producer | runtime | hardware_target | semantic
  bool ok = false;
  std::string detail;
  std::string to_string() const { return std::string(name) + "=" + (ok ? "ok" : "FAIL") + " [" + detail + "]"; }
};

// The full validation report for an artifact.
struct ValidationReport {
  ValidationState state = ValidationState::UNVALIDATED;
  std::vector<ValidationCheck> checks;
  std::string message;

  bool all_ok() const {
    for (const auto& c : checks) if (!c.ok) return false;
    return state == ValidationState::VALID || state == ValidationState::INTEGRITY_OK;
  }
};

// Reasons for quarantine.
enum class QuarantineReason : std::int32_t {
  INTEGRITY_FAILURE = 0,
  INVALID_ENUM = 1,
  MALFORMED_STATE = 2,
  DEPENDENCY_INVALID = 3,
  UNVERIFIABLE = 4,
  POLICY_VIOLATION = 5,
  OPERATOR = 6,
};

inline const char* quarantine_reason_name(QuarantineReason r) {
  switch (r) {
    case QuarantineReason::INTEGRITY_FAILURE: return "INTEGRITY_FAILURE";
    case QuarantineReason::INVALID_ENUM: return "INVALID_ENUM";
    case QuarantineReason::MALFORMED_STATE: return "MALFORMED_STATE";
    case QuarantineReason::DEPENDENCY_INVALID: return "DEPENDENCY_INVALID";
    case QuarantineReason::UNVERIFIABLE: return "UNVERIFIABLE";
    case QuarantineReason::POLICY_VIOLATION: return "POLICY_VIOLATION";
    case QuarantineReason::OPERATOR: return "OPERATOR";
  }
  return "UNKNOWN";
}

struct QuarantineRecord {
  QuarantineReason reason = QuarantineReason::INTEGRITY_FAILURE;
  std::string detection_source;
  std::int64_t timestamp_ns = 0;
  std::string detail;
};

}  // namespace af
