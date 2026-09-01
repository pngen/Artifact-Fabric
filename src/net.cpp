#include "artifact_fabric/net.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>
#include <mutex>
#include <stdexcept>

#pragma comment(lib, "ws2_32.lib")

namespace af {

static std::once_flag g_wsa_once;
void winsock_init() {
  std::call_once(g_wsa_once, [] {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) throw std::runtime_error("WSAStartup failed");
  });
}

static bool send_exact(SOCKET s, const std::uint8_t* p, std::size_t n) {
  std::size_t sent = 0;
  while (sent < n) {
    int r = ::send(s, reinterpret_cast<const char*>(p + sent), static_cast<int>(n - sent), 0);
    if (r == SOCKET_ERROR) return false;
    sent += static_cast<std::size_t>(r);
    if (r == 0) return false;
  }
  return true;
}
static bool recv_exact(SOCKET s, std::uint8_t* p, std::size_t n) {
  std::size_t got = 0;
  while (got < n) {
    int r = ::recv(s, reinterpret_cast<char*>(p + got), static_cast<int>(n - got), 0);
    if (r <= 0) return false;
    got += static_cast<std::size_t>(r);
  }
  return true;
}

TcpClient::~TcpClient() { close(); }
TcpClient::TcpClient(TcpClient&& o) noexcept : sock_(o.sock_) { o.sock_ = kInvalid; }
TcpClient& TcpClient::operator=(TcpClient&& o) noexcept {
  if (this != &o) { close(); sock_ = o.sock_; o.sock_ = kInvalid; }
  return *this;
}

bool TcpClient::connect(const std::string& host, std::uint16_t port) {
  winsock_init();
  SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return false;
  sockaddr_in sa{};
  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);
  if (inet_pton(AF_INET, host.c_str(), &sa.sin_addr) != 1) {
    // try resolver
    addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) { closesocket(s); return false; }
    sa.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
    freeaddrinfo(res);
  }
  if (::connect(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) == SOCKET_ERROR) { closesocket(s); return false; }
  sock_ = static_cast<std::int64_t>(s);
  return true;
}
void TcpClient::close() {
  if (sock_ != kInvalid) { closesocket(static_cast<SOCKET>(sock_)); sock_ = kInvalid; }
}
void TcpClient::adopt(std::int64_t sock) { close(); sock_ = sock; }

bool TcpClient::send_frame(const Frame& frame) const {
  if (sock_ == kInvalid) return false;
  auto bytes = encode_frame(frame.type, frame.payload);
  return send_exact(static_cast<SOCKET>(sock_), bytes.data(), bytes.size());
}
bool TcpClient::recv_frame(Frame& out) const {
  if (sock_ == kInvalid) return false;
  SOCKET s = static_cast<SOCKET>(sock_);
  std::uint8_t header[kFrameHeaderLen];
  if (!recv_exact(s, header, kFrameHeaderLen)) return false;
  std::uint32_t len = static_cast<std::uint32_t>(header[9]) |
                      (static_cast<std::uint32_t>(header[10]) << 8) |
                      (static_cast<std::uint32_t>(header[11]) << 16) |
                      (static_cast<std::uint32_t>(header[12]) << 24);
  if (len > kMaxFramePayload) return false;
  std::vector<std::uint8_t> full(kFrameHeaderLen + len + kFrameTrailerLen);
  std::memcpy(full.data(), header, kFrameHeaderLen);
  if (!recv_exact(s, full.data() + kFrameHeaderLen, len + kFrameTrailerLen)) return false;
  try {
    out = decode_frame(full);
    return true;
  } catch (...) { return false; }
}

TcpServer::~TcpServer() { close(); }
bool TcpServer::listen(const std::string& host, std::uint16_t port) {
  winsock_init();
  SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return false;
  BOOL no = FALSE;
  setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&no), sizeof(no));
  sockaddr_in sa{};
  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);
  if (inet_pton(AF_INET, host.c_str(), &sa.sin_addr) != 1) sa.sin_addr.s_addr = INADDR_ANY;
  if (::bind(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) == SOCKET_ERROR) { closesocket(s); return false; }
  if (::listen(s, 16) == SOCKET_ERROR) { closesocket(s); return false; }
  listener_ = static_cast<std::int64_t>(s);
  return true;
}
std::int64_t TcpServer::accept() {
  if (listener_ == kInvalid) return -1;
  SOCKET c = ::accept(static_cast<SOCKET>(listener_), nullptr, nullptr);
  if (c == INVALID_SOCKET) return -1;
  return static_cast<std::int64_t>(c);
}
void TcpServer::close() {
  if (listener_ != kInvalid) { closesocket(static_cast<SOCKET>(listener_)); listener_ = kInvalid; }
}

}  // namespace af
