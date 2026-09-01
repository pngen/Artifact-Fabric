#pragma once
// Artifact Fabric - placement and storage location metadata.
// Artifact authority is never blurred with physical location.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "generation.hpp"
#include "id.hpp"

namespace af {

enum class StorageKind : std::int32_t {
  LOCAL_FILESYSTEM = 0,
  LOCAL_NVME = 1,
  HOST_MEMORY = 2,
  DEVICE_MEMORY = 3,
  SHARED_STORAGE = 4,
  OBJECT_STORE = 5,
  EXTERNAL = 6,
  OPAQUE = 7,
};

inline bool is_valid_storage_kind(std::int32_t v) { return v >= 0 && v <= 7; }

inline const char* storage_kind_name(StorageKind k) {
  switch (k) {
    case StorageKind::LOCAL_FILESYSTEM: return "LOCAL_FILESYSTEM";
    case StorageKind::LOCAL_NVME: return "LOCAL_NVME";
    case StorageKind::HOST_MEMORY: return "HOST_MEMORY";
    case StorageKind::DEVICE_MEMORY: return "DEVICE_MEMORY";
    case StorageKind::SHARED_STORAGE: return "SHARED_STORAGE";
    case StorageKind::OBJECT_STORE: return "OBJECT_STORE";
    case StorageKind::EXTERNAL: return "EXTERNAL";
    case StorageKind::OPAQUE: return "OPAQUE";
  }
  return "UNKNOWN";
}

// A record describing one physical/backing placement of an artifact.
struct Placement {
  PlacementId id{};
  StorageLocationId location{};
  StorageKind kind = StorageKind::LOCAL_FILESYSTEM;
  std::string locator;      // path / URI / key / handle
  std::uint64_t byte_size = 0;
  PlacementGeneration generation{};
  double locality = 0.0;    // 0..1 higher = co-located with consumer
  double cost = 0.0;        // relative cost of access/materialization
  double readiness = 1.0;   // 0..1
  bool integrity_ok = true;
  bool persistent = false;  // survives process restart
};

// Deterministic selection scoring for placement ranking.
inline double placement_score(const Placement& p, double locality_weight, double cost_weight, double readiness_weight) {
  return p.locality * locality_weight - p.cost * cost_weight + p.readiness * readiness_weight;
}

}  // namespace af
