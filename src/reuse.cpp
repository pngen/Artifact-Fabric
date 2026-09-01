#include "artifact_fabric/reuse.hpp"

namespace af {

ReuseResult check_reuse(const ArtifactDescriptor& desc, const CompatRequirement& req, const ReuseInputs& inputs) {
  ReuseResult r;

  // Deterministic gate order: integrity, provenance, authority, lifecycle,
  // validation, placement, then compatibility.
  bool integrity_failed = false;
  if (!inputs.integrity_ok) { r.failures.push_back("integrity failure"); integrity_failed = true; }
  else if (desc.validation_state == ValidationState::INTEGRITY_FAILURE) {
    r.failures.push_back("integrity failure"); integrity_failed = true;
  }
  if (!inputs.provenance_valid) r.failures.push_back("provenance invalid");
  if (!inputs.authority_current) r.failures.push_back("authority stale");

  switch (desc.lifecycle) {
    case LifecycleState::STALE: r.failures.push_back("lifecycle stale"); break;
    case LifecycleState::INVALIDATED: r.failures.push_back("lifecycle invalidated"); break;
    case LifecycleState::SUPERSEDED: r.failures.push_back("lifecycle superseded"); break;
    case LifecycleState::QUARANTINED: r.failures.push_back("lifecycle quarantined"); break;
    case LifecycleState::EVICTED: r.failures.push_back("lifecycle evicted"); break;
    case LifecycleState::RETIRED: r.failures.push_back("lifecycle retired"); break;
    case LifecycleState::FAILED: r.failures.push_back("lifecycle failed"); break;
    default: break;
  }

  if (desc.validation_state != ValidationState::VALID &&
      desc.validation_state != ValidationState::INTEGRITY_OK && !integrity_failed)
    r.failures.push_back("not validated");

  if (!inputs.placement_available) r.failures.push_back("placement unavailable");

  auto compat = evaluate_compatibility(desc, req, inputs.dependency_fresh);
  r.compat = compat;
  if (!compat.compatible()) {
    for (const auto& d : compat.failed_dimensions) r.failures.push_back(std::string("compat ") + d);
  }

  r.reusable = r.failures.empty();
  return r;
}

ReuseResult check_reuse(const ArtifactDescriptor& desc, const CompatRequirement& req) {
  return check_reuse(desc, req, ReuseInputs{});
}

}  // namespace af
