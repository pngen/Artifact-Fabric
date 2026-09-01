#pragma once
// Artifact Fabric - exact accounting of logical and physical state.
#include <cstdint>
#include <stdexcept>
#include <string>

namespace af {

// Snapshot of all counters. Counters never go negative; double-release and
// leaked reservations are rejected.
struct Accounting {
  std::int64_t logical_artifacts = 0;
  std::int64_t physical_backing = 0;
  std::int64_t logical_bytes = 0;
  std::int64_t physical_bytes = 0;
  std::int64_t dedup_bytes = 0;           // bytes that are shared between artifacts
  std::int64_t active_references = 0;
  std::int64_t build_reservations = 0;
  std::int64_t temporary_publication_bytes = 0;
  std::int64_t evictions = 0;
  std::int64_t invalidations = 0;
  std::int64_t supersessions = 0;

  bool is_zero() const {
    return logical_artifacts == 0 && physical_backing == 0 && logical_bytes == 0 && physical_bytes == 0 &&
           dedup_bytes == 0 && active_references == 0 && build_reservations == 0 &&
           temporary_publication_bytes == 0 && evictions == 0 && invalidations == 0 && supersessions == 0;
  }

  std::string to_string() const {
    return "artifacts=" + std::to_string(logical_artifacts) + " backing=" + std::to_string(physical_backing) +
           " logical_bytes=" + std::to_string(logical_bytes) + " physical_bytes=" + std::to_string(physical_bytes) +
           " dedup=" + std::to_string(dedup_bytes) + " refs=" + std::to_string(active_references) +
           " reservations=" + std::to_string(build_reservations) + " temp_pub=" + std::to_string(temporary_publication_bytes);
  }
};

// A guard that can wrap a counter adjustment with strict validation.
class Accountant {
 public:
  Accounting snapshot() const { return state_; }
  bool is_zero() const { return state_.is_zero(); }

  void add_logical_artifact(std::int64_t n = 1) { state_.logical_artifacts += n; }
  void add_physical_backing(std::int64_t n = 1) { state_.physical_backing += n; }
  void add_logical_bytes(std::int64_t n) {
    if (state_.logical_bytes + n < 0) throw std::logic_error("negative logical bytes");
    state_.logical_bytes += n;
  }
  void add_physical_bytes(std::int64_t n) {
    if (state_.physical_bytes + n < 0) throw std::logic_error("negative physical bytes");
    state_.physical_bytes += n;
  }
  void add_dedup_bytes(std::int64_t n) {
    if (state_.dedup_bytes + n < 0) throw std::logic_error("negative dedup bytes");
    state_.dedup_bytes += n;
  }
  void add_active_reference(std::int64_t n = 1) {
    if (state_.active_references + n < 0) throw std::logic_error("negative active references (double release)");
    state_.active_references += n;
  }
  void reserve_build() { ++state_.build_reservations; }
  void release_build() {
    if (state_.build_reservations <= 0) throw std::logic_error("release with no reservation (leaked/mismatched reservation)");
    --state_.build_reservations;
  }
  void add_temp_publication_bytes(std::int64_t n) {
    if (state_.temporary_publication_bytes + n < 0) throw std::logic_error("negative temp publication bytes");
    state_.temporary_publication_bytes += n;
  }
  void record_eviction() { ++state_.evictions; }
  void record_invalidation() { ++state_.invalidations; }
  void record_supersession() { ++state_.supersessions; }

 private:
  Accounting state_;
};

}  // namespace af
