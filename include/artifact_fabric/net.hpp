#pragma once
// Artifact Fabric - framed TCP transport over WinSock.
#include <cstdint>
#include <string>

#include "protocol.hpp"

namespace af {

// RAII Winsock initialization (idempotent).
void winsock_init();

class TcpClient {
 public:
  TcpClient() = default;
  ~TcpClient();
  TcpClient(const TcpClient&) = delete;
  TcpClient& operator=(const TcpClient&) = delete;
  TcpClient(TcpClient&& o) noexcept;
  TcpClient& operator=(TcpClient&& o) noexcept;

  bool connect(const std::string& host, std::uint16_t port);
  void close();
  bool is_open() const { return sock_ != kInvalid; }
  bool send_frame(const Frame& frame) const;
  bool recv_frame(Frame& out) const;

  // Adopt an already-connected socket (used by the server for accepted clients).
  void adopt(std::int64_t sock);

 private:
  std::int64_t sock_ = kInvalid;
  static constexpr std::int64_t kInvalid = -1;
};

class TcpServer {
 public:
  TcpServer() = default;
  ~TcpServer();
  TcpServer(const TcpServer&) = delete;
  TcpServer& operator=(const TcpServer&) = delete;

  bool listen(const std::string& host, std::uint16_t port);
  // Accept one inbound connection (blocking). Returns a connected socket handle
  // or -1. Wrap with TcpClient::adopt().
  std::int64_t accept();
  void close();
  bool listening() const { return listener_ != kInvalid; }

 private:
  std::int64_t listener_ = kInvalid;
  static constexpr std::int64_t kInvalid = -1;
};

}  // namespace af
