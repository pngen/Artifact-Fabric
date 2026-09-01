#pragma once
// Artifact Fabric - deterministic versioned binary persistence.
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "config.hpp"
#include "descriptor.hpp"
#include "hash.hpp"

namespace af {

// Schema version for the on-disk artifact image.
inline constexpr std::uint32_t kArtifactImageSchemaVersion = 1;
// Fixed magic to identify an Artifact Fabric artifact image.
inline constexpr std::uint32_t kArtifactImageMagic = 0x41464642u;  // "AFAB"

class PersistenceError : public std::runtime_error {
 public:
  explicit PersistenceError(const std::string& m) : std::runtime_error("persistence: " + m) {}
};

// ---------------------------------------------------------------------------
// Encoder : little-endian, bounded, deterministic.
// ---------------------------------------------------------------------------
class Encoder {
 public:
  void u8(std::uint8_t v) { buf_.push_back(v); }
  void u16(std::uint16_t v) { for (int i = 0; i < 2; ++i) buf_.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xffu)); }
  void u32(std::uint32_t v) { for (int i = 0; i < 4; ++i) buf_.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xffu)); }
  void u64(std::uint64_t v) { for (int i = 0; i < 8; ++i) buf_.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xffu)); }
  void i64(std::int64_t v) { u64(static_cast<std::uint64_t>(v)); }

  // Length-prefixed bytes with a hard upper bound.
  void bytes(const std::uint8_t* p, std::size_t n, std::size_t max_len = kMaxFieldBytes) {
    if (n > max_len) throw PersistenceError("field exceeds max length");
    u32(static_cast<std::uint32_t>(n));
    buf_.insert(buf_.end(), p, p + n);
  }
  void raw(const std::uint8_t* p, std::size_t n) { buf_.insert(buf_.end(), p, p + n); }

  void str(std::string_view s) { bytes(reinterpret_cast<const std::uint8_t*>(s.data()), s.size(), kMaxNameBytes); }

  template <typename Tag, std::size_t N>
  void id(const Id<Tag, N>& x) { raw(x.bytes(), N); }

  template <typename Tag>
  void digest(const Digest<Tag>& d) { raw(d.data(), 32); }

  // Presence flag for optional fields.
  void present(bool b) { u8(b ? 1 : 0); }

  std::vector<std::uint8_t>& data() { return buf_; }
  const std::vector<std::uint8_t>& data() const { return buf_; }

  // Append CRC-32 trailer and return the complete frame (payload + trailer).
  std::vector<std::uint8_t> finalize() {
    std::vector<std::uint8_t> out = buf_;
    std::uint32_t crc = crc32(out.data(), out.size());
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>((crc >> (8 * i)) & 0xffu));
    return out;
  }

 private:
  std::vector<std::uint8_t> buf_;
};

// ---------------------------------------------------------------------------
// Decoder : strict little-endian reader. Throws on truncation / overflow /
// invalid enums / trailing garbage.
// ---------------------------------------------------------------------------
class Decoder {
 public:
  explicit Decoder(std::string_view bytes) : data_(reinterpret_cast<const std::uint8_t*>(bytes.data())), size_(bytes.size()) {}

  std::uint8_t u8() {
    need(1);
    return data_[pos_++];
  }
  std::uint16_t u16() {
    need(2);
    std::uint16_t v = 0;
    for (int i = 0; i < 2; ++i) v |= static_cast<std::uint16_t>(data_[pos_ + i]) << (8 * i);
    pos_ += 2;
    return v;
  }
  std::uint32_t u32() {
    need(4);
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(data_[pos_ + i]) << (8 * i);
    pos_ += 4;
    return v;
  }
  std::uint64_t u64() {
    need(8);
    std::uint64_t v = 0;
    for (int i = 0; i < 7; ++i) v |= static_cast<std::uint64_t>(data_[pos_ + i]) << (8 * i);
    v |= static_cast<std::uint64_t>(data_[pos_ + 7]) << 56;
    pos_ += 8;
    return v;
  }
  std::int64_t i64() { return static_cast<std::int64_t>(u64()); }

  std::string str_fixed(std::size_t max = kMaxNameBytes) {
    std::size_t n = u32();
    if (n > max) throw PersistenceError("string exceeds max length");
    need(n);
    std::string s(reinterpret_cast<const char*>(data_ + pos_), n);
    pos_ += n;
    return s;
  }

  // Length-prefixed raw bytes.
  std::vector<std::uint8_t> raw_vec(std::size_t max = kMaxFieldBytes) {
    std::size_t n = u32();
    if (n > max) throw PersistenceError("bytes exceeds max length");
    need(n);
    std::vector<std::uint8_t> out(data_ + pos_, data_ + pos_ + n);
    pos_ += n;
    return out;
  }

  void raw_into(std::uint8_t* dst, std::size_t n) { need(n); for (std::size_t i = 0; i < n; ++i) dst[i] = data_[pos_ + i]; pos_ += n; }

  template <typename Tag, std::size_t N>
  Id<Tag, N> id() {
    need(N);
    Id<Tag, N> out = Id<Tag, N>::from_bytes(data_ + pos_);
    pos_ += N;
    return out;
  }

  template <typename Tag>
  Digest<Tag> digest() {
    need(32);
    sha256_t d;
    for (std::size_t i = 0; i < 32; ++i) d[i] = data_[pos_ + i];
    pos_ += 32;
    return Digest<Tag>(d);
  }

  bool present() { return u8() != 0; }

  bool at_end() const { return pos_ == size_; }
  std::size_t remaining() const { return size_ - pos_; }

  void require_end() const {
    if (pos_ != size_) throw PersistenceError("trailing garbage after decoded object");
  }

 private:
  void need(std::size_t n) const {
    if (n > size_ - pos_) throw PersistenceError("truncated / malformed input");
  }
  const std::uint8_t* data_;
  std::size_t size_;
  std::size_t pos_ = 0;
};

// Verify and strip a CRC-32 trailer. Throws on mismatch.
inline std::vector<std::uint8_t> verify_crc_frame(const std::vector<std::uint8_t>& frame) {
  if (frame.size() < 4) throw PersistenceError("frame too short for CRC");
  std::size_t payload = frame.size() - 4;
  std::uint32_t expected =
      static_cast<std::uint32_t>(frame[payload]) |
      (static_cast<std::uint32_t>(frame[payload + 1]) << 8) |
      (static_cast<std::uint32_t>(frame[payload + 2]) << 16) |
      (static_cast<std::uint32_t>(frame[payload + 3]) << 24);
  std::uint32_t actual = crc32(frame.data(), payload);
  if (expected != actual) throw PersistenceError("CRC-32 mismatch (corruption or trailing-garbage)");
  return std::vector<std::uint8_t>(frame.begin(), frame.begin() + payload);
}

// Generation / enum helpers.
template <typename Tag>
inline void write_generation(Encoder& e, const Generation<Tag>& g) { e.u64(g.value()); }
template <typename Tag>
inline Generation<Tag> read_generation(Decoder& d) { return Generation<Tag>(d.u64()); }

inline void write_lifecycle(Encoder& e, LifecycleState s) {
  if (!is_valid_lifecycle(static_cast<std::int32_t>(s))) throw PersistenceError("invalid lifecycle state");
  e.u32(static_cast<std::uint32_t>(s));
}
inline LifecycleState read_lifecycle(Decoder& d) {
  auto v = d.u32();
  if (!is_valid_lifecycle(static_cast<std::int32_t>(v))) throw PersistenceError("invalid lifecycle state");
  return static_cast<LifecycleState>(v);
}
inline void write_validation(Encoder& e, ValidationState s) {
  if (!is_valid_validation(static_cast<std::int32_t>(s))) throw PersistenceError("invalid validation state");
  e.u32(static_cast<std::uint32_t>(s));
}
inline ValidationState read_validation(Decoder& d) {
  auto v = d.u32();
  if (!is_valid_validation(static_cast<std::int32_t>(v))) throw PersistenceError("invalid validation state");
  return static_cast<ValidationState>(v);
}
inline void write_kind(Encoder& e, ArtifactKind k) {
  if (!is_valid_kind(static_cast<std::int32_t>(k))) throw PersistenceError("invalid artifact kind");
  e.u32(static_cast<std::uint32_t>(k));
}
inline ArtifactKind read_kind(Decoder& d) {
  auto v = d.u32();
  if (!is_valid_kind(static_cast<std::int32_t>(v))) throw PersistenceError("invalid artifact kind");
  return static_cast<ArtifactKind>(v);
}

// Descriptor (de)serialization. Deterministic, versioned, bounded.
void write_descriptor(Encoder& e, const ArtifactDescriptor& d);
ArtifactDescriptor read_descriptor(Decoder& d);
std::vector<std::uint8_t> encode_descriptor(const ArtifactDescriptor& d);
ArtifactDescriptor decode_descriptor(const std::vector<std::uint8_t>& bytes);

}  // namespace af
