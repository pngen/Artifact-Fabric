#pragma once
// Artifact Fabric - coordinator + worker distributed runtime.
// Real coordinator and worker OS processes communicate over framed TCP.
#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "catalog.hpp"
#include "net.hpp"
#include "protocol.hpp"

namespace af {

// The coordinator: owns the authoritative catalog and fences every mutation.
class Coordinator {
 public:
  Coordinator(std::uint16_t port, const std::string& state_path);
  ~Coordinator();
  Coordinator(const Coordinator&) = delete;
  Coordinator& operator=(const Coordinator&) = delete;

  bool start();
  void shutdown();
  void run_forever();

  Catalog& catalog() { return cat_; }
  std::uint16_t port() const { return port_; }

 private:
  void handle_client(std::int64_t sock);
  Response process(const Request& req);

  std::uint16_t port_;
  std::string state_path_;
  Catalog cat_;
  TcpServer server_;
  std::vector<std::thread> clients_;
  std::atomic<bool> running_{true};
  std::mutex clients_mu_;
};

// A worker client used by worker processes and proof drivers.
class WorkerClient {
 public:
  bool connect(const std::string& host, std::uint16_t port);
  void close();
  bool is_open() const { return c_.is_open(); }

  Response hello();
  Response register_producer();
  Response publish(const ArtifactDescriptor& desc, const std::vector<std::uint8_t>& content,
                   const AuthorityEnvelope& auth);
  Response query(const ArtifactId& id);
  Response invalidate(const ArtifactId& id, const std::string& cause, const AuthorityEnvelope& auth);
  Response supersede(const ArtifactId& old_id, const ArtifactId& new_id, const std::string& reason,
                     const AuthorityEnvelope& auth);
  Response validate(const ArtifactId& id, bool ok, const AuthorityEnvelope& auth);
  Response retire(const ArtifactId& id, const AuthorityEnvelope& auth);
  Response roll_epoch();
  Response save(const std::string& path);
  bool send_raw_frame(const Frame& f) { return c_.send_frame(f); }
  bool recv_raw_frame(Frame& f) { return c_.recv_frame(f); }

 private:
  Response request(const Request& req);

  TcpClient c_;
};

// Coordinator process entry point (used by tools/af_coordinator).
int coordinator_main(int argc, char** argv);
// Scripted worker process entry point (used by tools/af_worker).
int worker_main(int argc, char** argv);

}  // namespace af
