#pragma once
// Artifact Fabric - artifact kind enum (typed, extensible).
#pragma once
// Artifact Fabric - artifact kind enum (typed, extensible).
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>


namespace af {

enum class ArtifactKind : std::int32_t {
  COMPILED_KERNEL = 0,
  EXECUTION_GRAPH = 1,
  COMPILER_OUTPUT = 2,
  ENGINE_ARTIFACT = 3,
  QUANTIZED_MODEL = 4,
  ADAPTER_ARTIFACT = 5,
  TOKENIZER_ASSET = 6,
  EXECUTION_PLAN = 7,
  AUTOTUNING_RESULT = 8,
  MANIFEST = 9,
  TENSOR_ARTIFACT = 10,
  SERIALIZED_RUNTIME_STATE = 11,
  BACKEND_EXECUTABLE = 12,
  OTHER = 13,
};

// Reject invalid enum values at persistence and protocol boundaries.
inline bool is_valid_kind(std::int32_t v) { return v >= 0 && v <= 13; }
inline bool is_valid_kind(ArtifactKind k) {
  return is_valid_kind(static_cast<std::int32_t>(k));
}

inline const char* kind_name(ArtifactKind k) {
  switch (k) {
    case ArtifactKind::COMPILED_KERNEL: return "COMPILED_KERNEL";
    case ArtifactKind::EXECUTION_GRAPH: return "EXECUTION_GRAPH";
    case ArtifactKind::COMPILER_OUTPUT: return "COMPILER_OUTPUT";
    case ArtifactKind::ENGINE_ARTIFACT: return "ENGINE_ARTIFACT";
    case ArtifactKind::QUANTIZED_MODEL: return "QUANTIZED_MODEL";
    case ArtifactKind::ADAPTER_ARTIFACT: return "ADAPTER_ARTIFACT";
    case ArtifactKind::TOKENIZER_ASSET: return "TOKENIZER_ASSET";
    case ArtifactKind::EXECUTION_PLAN: return "EXECUTION_PLAN";
    case ArtifactKind::AUTOTUNING_RESULT: return "AUTOTUNING_RESULT";
    case ArtifactKind::MANIFEST: return "MANIFEST";
    case ArtifactKind::TENSOR_ARTIFACT: return "TENSOR_ARTIFACT";
    case ArtifactKind::SERIALIZED_RUNTIME_STATE: return "SERIALIZED_RUNTIME_STATE";
    case ArtifactKind::BACKEND_EXECUTABLE: return "BACKEND_EXECUTABLE";
    case ArtifactKind::OTHER: return "OTHER";
  }
  return "UNKNOWN";
}

inline ArtifactKind kind_from_int(std::int32_t v) {
  if (!is_valid_kind(v)) throw std::invalid_argument("invalid artifact kind");
  return static_cast<ArtifactKind>(v);
}

}  // namespace af
