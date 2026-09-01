#pragma once
// Artifact Fabric - SHA-256 and CRC-32 (IEEE 802.3) primitives.
// Header-only, dependency-free, deterministic.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace af {

inline constexpr std::size_t kSha256Len = 32;
using sha256_t = std::array<unsigned char, kSha256Len>;

// ---------------------------------------------------------------------------
// SHA-256 (FIPS 180-4)
// ---------------------------------------------------------------------------
class Sha256 {
 public:
  Sha256() { init(); }

  void init() {
    state_[0] = 0x6a09e667u; state_[1] = 0xbb67ae85u;
    state_[2] = 0x3c6ef372u; state_[3] = 0xa54ff53au;
    state_[4] = 0x510e527fu; state_[5] = 0x9b05688cu;
    state_[6] = 0x1f83d9abu; state_[7] = 0x5be0cd19u;
    total_len_ = 0;
    buff_len_ = 0;
  }

  void update(const unsigned char* data, std::size_t len) {
    for (std::size_t i = 0; i < len; ++i) {
      buffer_[buff_len_++] = data[i];
      if (buff_len_ == 64) {
        transform(buffer_.data());
        buff_len_ = 0;
      }
    }
    total_len_ += len;
  }

  void update(std::string_view v) {
    update(reinterpret_cast<const unsigned char*>(v.data()), v.size());
  }

  void update(const void* ptr, std::size_t len) {
    update(static_cast<const unsigned char*>(ptr), len);
  }

  // Finalize and copy digest. The object can be reused via init().
  sha256_t finish() {
    std::uint64_t bit_len = static_cast<std::uint64_t>(total_len_) * 8u;
    // Append 0x80 then zeros until len % 64 == 56, then 8-byte big-endian length.
    unsigned char pad = 0x80;
    update(&pad, 1);
    unsigned char zero = 0x00;
    while (buff_len_ != 56) update(&zero, 1);
    unsigned char len_arr[8];
    for (int i = 0; i < 8; ++i) len_arr[i] = static_cast<unsigned char>((bit_len >> (56 - 8 * i)) & 0xffu);
    update(len_arr, 8);
    sha256_t out{};
    for (int i = 0; i < 8; ++i) {
      out[i * 4]     = static_cast<unsigned char>(state_[i] >> 24);
      out[i * 4 + 1] = static_cast<unsigned char>(state_[i] >> 16);
      out[i * 4 + 2] = static_cast<unsigned char>(state_[i] >> 8);
      out[i * 4 + 3] = static_cast<unsigned char>(state_[i]);
    }
    return out;
  }

  static sha256_t digest(const void* ptr, std::size_t len) {
    Sha256 h;
    h.update(ptr, len);
    return h.finish();
  }
  static sha256_t digest(std::string_view v) {
    return digest(v.data(), v.size());
  }

 private:
  static std::uint32_t rotr(std::uint32_t x, std::uint32_t n) { return (x >> n) | (x << (32 - n)); }

  void transform(const unsigned char* p) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<std::uint32_t>(p[i * 4]) << 24) |
             (static_cast<std::uint32_t>(p[i * 4 + 1]) << 16) |
             (static_cast<std::uint32_t>(p[i * 4 + 2]) << 8) |
             static_cast<std::uint32_t>(p[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
      std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    constexpr std::uint32_t K[64] = {
      0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
      0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
      0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
      0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
      0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
      0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
      0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
      0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
    };
    for (int i = 0; i < 64; ++i) {
      std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      std::uint32_t ch = (e & f) ^ (~e & g);
      std::uint32_t t1 = h + S1 + ch + K[i] + w[i];
      std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      std::uint32_t t2 = S0 + maj;
      h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
    state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
  }

  std::uint32_t state_[8];
  std::array<unsigned char, 64> buffer_{};
  std::size_t buff_len_ = 0;
  std::uint64_t total_len_ = 0;
};

inline std::string to_hex(const unsigned char* data, std::size_t len) {
  static const char* hexd = "0123456789abcdef";
  std::string out;
  out.reserve(len * 2);
  for (std::size_t i = 0; i < len; ++i) {
    out.push_back(hexd[data[i] >> 4]);
    out.push_back(hexd[data[i] & 0x0f]);
  }
  return out;
}

inline std::string to_hex(const sha256_t& d) { return to_hex(d.data(), d.size()); }

// ---------------------------------------------------------------------------
// CRC-32 (IEEE 802.3, polynomial 0xEDB88320)
// ---------------------------------------------------------------------------
inline std::uint32_t crc32(const void* ptr, std::size_t len, std::uint32_t seed = 0xffffffffu) {
  static const std::array<std::uint32_t, 256> table = [] {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
      t[i] = c;
    }
    return t;
  }();
  const auto* p = static_cast<const unsigned char*>(ptr);
  std::uint32_t c = seed;
  for (std::size_t i = 0; i < len; ++i) c = table[(c ^ p[i]) & 0xffu] ^ (c >> 8);
  return c ^ 0xffffffffu;
}

inline std::uint32_t crc32(std::string_view v) { return crc32(v.data(), v.size()); }

}  // namespace af
