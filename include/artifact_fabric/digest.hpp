#pragma once
// Artifact Fabric - digest types.
// Distinct digest categories are separately typed; the canonical content digest
// is the SHA-256 of the canonical content encoding.
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include "hash.hpp"
#include "id.hpp"

namespace af {

struct ContentDigestTag {};
struct MetadataDigestTag {};
struct CompatibilityDigestTag {};
struct DependencyDigestTag {};

// A typed, fixed 256-bit digest.
template <typename Tag>
class Digest {
 public:
  Digest() { raw_.fill(0); }
  explicit Digest(const sha256_t& r) : raw_(r) {}

  static Digest from_hex(std::string_view hex) { return Digest(parse_hex(hex)); }
  static Digest hash(const void* p, std::size_t n) { return Digest(Sha256::digest(p, n)); }
  static Digest hash(std::string_view v) { return Digest(Sha256::digest(v)); }

  const sha256_t& bytes() const { return raw_; }
  const unsigned char* data() const { return raw_.data(); }

  bool is_zero() const {
    for (auto c : raw_) if (c != 0) return false;
    return true;
  }

  std::string to_string() const { return to_hex(raw_); }

  bool operator==(const Digest& o) const { return raw_ == o.raw_; }
  bool operator!=(const Digest& o) const { return raw_ != o.raw_; }
  bool operator<(const Digest& o) const { return raw_ < o.raw_; }

  std::uint64_t hash() const { return xor_half(raw_.data(), raw_.size()); }

 private:
  static sha256_t parse_hex(std::string_view hex) {
    if (hex.size() < 64) throw std::invalid_argument("digest hex too short");
    sha256_t r;
    auto nib = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      throw std::invalid_argument("non-hex digest character");
    };
    for (std::size_t i = 0; i < r.size(); ++i)
      r[i] = static_cast<unsigned char>((nib(hex[i * 2]) << 4) | nib(hex[i * 2 + 1]));
    return r;
  }
  sha256_t raw_{};
};

using ContentDigest = Digest<ContentDigestTag>;
using MetadataDigest = Digest<MetadataDigestTag>;
using CompatibilityDigest = Digest<CompatibilityDigestTag>;
using DependencyDigest = Digest<DependencyDigestTag>;

// The logical content id is the content digest (content-addressed identity).
inline ArtifactContentId content_id_of(const ContentDigest& d) {
  return ArtifactContentId::from_digest(d.bytes());
}
// Full 256-bit content identity.
inline FullContentId full_content_id_of(const ContentDigest& d) {
  return FullContentId::from_digest(d.bytes());
}

}  // namespace af

namespace std {
template <typename Tag>
struct hash<af::Digest<Tag>> {
  std::size_t operator()(const af::Digest<Tag>& d) const noexcept {
    return static_cast<std::size_t>(d.hash());
  }
};
}  // namespace std
