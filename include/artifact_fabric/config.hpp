#pragma once
// Artifact Fabric - compile-time configuration / feature flags.
// These are defined here and, if desired, overridden by the build system.
#define ARTIFACT_FABRIC_NAMESPACE af
#include <cstddef>

namespace af {
// Bounded limits used throughout the persistence and protocol layers.
constexpr std::size_t kMaxDescriptorCount = 1u << 20;   // max artifacts per catalog image
constexpr std::size_t kMaxDependencyCount = 1u << 20;   // max edges per graph
constexpr std::size_t kMaxFieldBytes = 64u << 20;       // max 64 MiB per opaque field
constexpr std::size_t kMaxNameBytes = 4096;             // max string field bytes
}  // namespace af
