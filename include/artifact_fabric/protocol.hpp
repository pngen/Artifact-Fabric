#pragma once
// Artifact Fabric - framed, bounded, deterministic binary protocol.
#include <cstdint>
#include <string>
#include <vector>

#include "authority.hpp"
#include "descriptor.hpp"

namespace af {

enum class MessageType : std::uint32_t {
  HELLO = 0,
  REGISTER_PRODUCER = 1,
  DECLARE_ARTIFACT = 2,
  BEGIN_BUILD = 3,
  PUBLISH = 4,
  VALIDATE = 5,
  PROMOTE = 6,
  SUPERSEDE = 7,
  INVALIDATE = 8,
  RETIRE = 9,
  QUERY = 10,
  REPORT = 11,
  ACK = 12,
  ERROR = 13,
};

// Frame layout: [magic u32][version u8][type u32][len u32][payload][crc32 u32]
// Total header = 13 bytes; trailer = 4 bytes (CRC-32 over header+payload).
constexpr std::uint32_t kFrameMagic = 0x46524146u;  // "FRAF"
constexpr std::uint8_t kFrameVersion = 1;
constexpr std::size_t kFrameHeaderLen = 13;
constexpr std::size_t kFrameTrailerLen = 4;
constexpr std::size_t kMaxFramePayload = 64u << 20;   // 64 MiB
constexpr std::size_t kMaxFrameSize = kFrameHeaderLen + kMaxFramePayload + kFrameTrailerLen;

struct Frame {
  MessageType type = MessageType::HELLO;
  std::vector<std::uint8_t> payload;
};

class ProtocolError : public std::runtime_error {
 public:
  explicit ProtocolError(const std::string& m) : std::runtime_error("protocol: " + m) {}
};

// Build a full frame (header + payload + CRC) from a payload body.
std::vector<std::uint8_t> encode_frame(MessageType type, const std::vector<std::uint8_t>& payload);
// Decode and fully validate a frame (magic, version, length bound, CRC, no
// trailing garbage). Throws ProtocolError on any violation.
Frame decode_frame(const std::vector<std::uint8_t>& bytes);

// ---------------------------------------------------------------------------
// Request / response payloads.
// ---------------------------------------------------------------------------
struct Request {
  MessageType type = MessageType::HELLO;
  WorkerBootId boot{};
  ArtifactDescriptor descriptor{};
  std::vector<std::uint8_t> content;
  AuthorityEnvelope authority{};
  ArtifactId artifact_id{};
  ArtifactId artifact_id_2{};
  std::string cause;
  bool validation_ok = false;

  std::vector<std::uint8_t> encode() const;
  static Request decode(const std::vector<std::uint8_t>& payload);
};

struct Response {
  bool ok = false;
  std::string error;
  ArtifactId artifact_id{};
  ContentDigest content_digest{};
  CoordinatorEpoch epoch = 0;
  WorkerBootId boot{};
  bool found = false;
  ArtifactDescriptor descriptor{};
  std::string message;

  std::vector<std::uint8_t> encode() const;
  static Response decode(const std::vector<std::uint8_t>& payload);
};

}  // namespace af
