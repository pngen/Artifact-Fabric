#include "artifact_fabric/protocol.hpp"

#include "artifact_fabric/persistence.hpp"

namespace af {

std::vector<std::uint8_t> encode_frame(MessageType type, const std::vector<std::uint8_t>& payload) {
  if (payload.size() > kMaxFramePayload) throw ProtocolError("payload exceeds max");
  Encoder e;
  e.u32(kFrameMagic);
  e.u8(kFrameVersion);
  e.u32(static_cast<std::uint32_t>(type));
  e.u32(static_cast<std::uint32_t>(payload.size()));
  e.raw(payload.data(), payload.size());
  auto out = e.finalize();  // appends CRC-32
  return out;
}

Frame decode_frame(const std::vector<std::uint8_t>& bytes) {
  if (bytes.size() < kFrameHeaderLen + kFrameTrailerLen) throw ProtocolError("frame too short");
  auto payload = verify_crc_frame(bytes);
  Decoder d(std::string_view(reinterpret_cast<const char*>(payload.data()), payload.size()));
  auto magic = d.u32();
  if (magic != kFrameMagic) throw ProtocolError("bad frame magic");
  auto version = d.u8();
  if (version != kFrameVersion) throw ProtocolError("unsupported protocol version");
  auto type = d.u32();
  auto len = d.u32();
  if (len > kMaxFramePayload) throw ProtocolError("payload exceeds max");
  if (payload.size() != kFrameHeaderLen + len) throw ProtocolError("malformed frame payload length");
  Frame f;
  f.type = static_cast<MessageType>(type);
  bool valid_type = false;
  for (std::uint32_t m = 0; m <= 13; ++m) if (m == type) { valid_type = true; break; }
  if (!valid_type) throw ProtocolError("unknown message type");
  f.payload.assign(payload.begin() + kFrameHeaderLen, payload.end());
  return f;
}

std::vector<std::uint8_t> Request::encode() const {
  Encoder e;
  e.u32(static_cast<std::uint32_t>(type));
  e.u64(authority.epoch);
  e.id(authority.worker);
  e.id(authority.boot);
  e.id(authority.attempt);
  e.id(authority.producer);
  write_generation(e, authority.attempt_generation);
  write_generation(e, authority.producer_generation);
  write_generation(e, authority.artifact_generation);
  write_generation(e, authority.dependency_generation);
  write_generation(e, authority.provenance_generation);
  write_generation(e, authority.toolchain_generation);
  write_generation(e, authority.model_generation);
  write_generation(e, authority.adapter_generation);
  write_generation(e, authority.placement_generation);
  e.id(artifact_id);
  e.id(artifact_id_2);
  e.str(cause);
  e.present(validation_ok);
  write_descriptor(e, descriptor);
  e.bytes(content.data(), content.size());
  return e.finalize();
}

Request Request::decode(const std::vector<std::uint8_t>& payload) {
  auto body = verify_crc_frame(payload);
  Decoder d(std::string_view(reinterpret_cast<const char*>(body.data()), body.size()));
  Request r;
  auto type = d.u32();
  if (type > 13) throw ProtocolError("unknown request type");
  r.type = static_cast<MessageType>(type);
  r.authority.epoch = d.u64();
  r.authority.worker = d.id<WorkerIdTag, 16>();
  r.authority.boot = d.id<WorkerBootIdTag, 16>();
  r.authority.attempt = d.id<AttemptIdTag, 16>();
  r.authority.producer = d.id<ProducerIdTag, 16>();
  r.authority.attempt_generation = read_generation<AttemptGenerationTag>(d);
  r.authority.producer_generation = read_generation<ProducerGenerationTag>(d);
  r.authority.artifact_generation = read_generation<ArtifactGenerationTag>(d);
  r.authority.dependency_generation = read_generation<DependencyGenerationTag>(d);
  r.authority.provenance_generation = read_generation<ProvenanceGenerationTag>(d);
  r.authority.toolchain_generation = read_generation<ToolchainGenerationTag>(d);
  r.authority.model_generation = read_generation<ModelGenerationTag>(d);
  r.authority.adapter_generation = read_generation<AdapterGenerationTag>(d);
  r.authority.placement_generation = read_generation<PlacementGenerationTag>(d);
  r.artifact_id = d.id<ArtifactIdTag, 16>();
  r.artifact_id_2 = d.id<ArtifactIdTag, 16>();
  r.cause = d.str_fixed();
  r.validation_ok = d.present();
  r.descriptor = read_descriptor(d);
  r.content = d.raw_vec();
  d.require_end();
  return r;
}

std::vector<std::uint8_t> Response::encode() const {
  Encoder e;
  e.present(ok);
  e.str(error);
  e.id(artifact_id);
  e.digest(content_digest);
  e.u64(epoch);
  e.id(boot);
  e.present(found);
  e.str(message);
  write_descriptor(e, descriptor);
  return e.finalize();
}

Response Response::decode(const std::vector<std::uint8_t>& payload) {
  auto body = verify_crc_frame(payload);
  Decoder d(std::string_view(reinterpret_cast<const char*>(body.data()), body.size()));
  Response r;
  r.ok = d.present();
  r.error = d.str_fixed();
  r.artifact_id = d.id<ArtifactIdTag, 16>();
  r.content_digest = d.digest<ContentDigestTag>();
  r.epoch = d.u64();
  r.boot = d.id<WorkerBootIdTag, 16>();
  r.found = d.present();
  r.message = d.str_fixed();
  r.descriptor = read_descriptor(d);
  d.require_end();
  return r;
}

}  // namespace af
