#include "artifact_fabric/distributed.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace af {

// ---------------------------------------------------------------------------
// Coordinator
// ---------------------------------------------------------------------------
Coordinator::Coordinator(std::uint16_t port, const std::string& state_path)
    : port_(port), state_path_(state_path) {
  cat_.set_authority(1, WorkerBootId::random());
}

Coordinator::~Coordinator() { shutdown(); }

bool Coordinator::start() {
  if (!server_.listen("127.0.0.1", port_)) { std::fprintf(stderr, "coordinator: cannot listen\n"); return false; }
  return true;
}

void Coordinator::shutdown() {
  running_ = false;
  server_.close();
  std::lock_guard<std::mutex> lk(clients_mu_);
  for (auto& t : clients_) if (t.joinable()) t.detach();
  clients_.clear();
}

void Coordinator::run_forever() {
  while (running_) {
    std::int64_t sock = server_.accept();
    if (sock < 0) break;
    std::lock_guard<std::mutex> lk(clients_mu_);
    clients_.emplace_back([this, sock] { handle_client(sock); });
  }
}

Response Coordinator::process(const Request& req) {
  Response resp;
  switch (req.type) {
    case MessageType::HELLO:
      resp.ok = true; resp.epoch = cat_.epoch(); resp.boot = cat_.boot(); resp.message = "hello";
      break;
    case MessageType::REGISTER_PRODUCER:
      resp.ok = true; resp.artifact_id = ArtifactId::from_bytes(ProducerId::random().bytes());
      resp.epoch = cat_.epoch(); resp.boot = cat_.boot();
      resp.message = "registered";
      break;
    case MessageType::PUBLISH: {
      try {
        PublishRequest pr;
        pr.descriptor = req.descriptor;
        pr.content = req.content;
        pr.authority = req.authority;
        PublishResult out = cat_.publish(pr);
        resp.ok = out.committed; resp.artifact_id = out.id;
        resp.content_digest = out.content_digest; resp.message = "published";
        resp.epoch = cat_.epoch(); resp.boot = cat_.boot();
      } catch (const std::exception& e) { resp.ok = false; resp.error = e.what(); }
      break;
    }
    case MessageType::INVALIDATE: {
      try { cat_.invalidate(req.artifact_id, req.cause, req.authority); resp.ok = true; resp.message = "invalidated"; }
      catch (const std::exception& e) { resp.ok = false; resp.error = e.what(); }
      break;
    }
    case MessageType::SUPERSEDE: {
      try { cat_.supersede(req.artifact_id, req.artifact_id_2, req.cause, req.authority); resp.ok = true; resp.message = "superseded"; }
      catch (const std::exception& e) { resp.ok = false; resp.error = e.what(); }
      break;
    }
    case MessageType::VALIDATE: {
      try {
        ValidationReport vr; vr.state = req.validation_ok ? ValidationState::VALID : ValidationState::INVALID;
        cat_.validate_artifact(req.artifact_id, vr, req.authority); resp.ok = true; resp.message = "validated";
      } catch (const std::exception& e) { resp.ok = false; resp.error = e.what(); }
      break;
    }
    case MessageType::RETIRE: {
      try { cat_.retire(req.artifact_id, req.authority); resp.ok = true; resp.message = "retired"; }
      catch (const std::exception& e) { resp.ok = false; resp.error = e.what(); }
      break;
    }
    case MessageType::QUERY: {
      resp.ok = true;
      const ArtifactDescriptor* d = cat_.find(req.artifact_id);
      if (d) { resp.found = true; resp.descriptor = *d; }
      break;
    }
    case MessageType::REPORT: {
      // Control actions: roll_epoch / save:<path>
      if (req.cause == "roll_epoch") {
        cat_.roll_epoch(WorkerBootId::random());
        resp.ok = true; resp.epoch = cat_.epoch(); resp.boot = cat_.boot(); resp.message = "rolled";
      } else if (req.cause.rfind("save:", 0) == 0) {
        try { cat_.save_file(req.cause.substr(5)); resp.ok = true; resp.message = "saved"; }
        catch (const std::exception& e) { resp.ok = false; resp.error = e.what(); }
      } else if (req.cause == "accounting") {
        resp.ok = true; resp.message = cat_.accounting().to_string();
      } else { resp.ok = false; resp.error = "unknown report"; }
      break;
    }
    default: resp.ok = false; resp.error = "unsupported request type";
  }
  return resp;
}

void Coordinator::handle_client(std::int64_t sock) {
  TcpClient client;
  client.adopt(sock);
  while (running_) {
    Frame frame;
    if (!client.recv_frame(frame)) break;
    Request req;
    try { req = Request::decode(frame.payload); }
    catch (...) { Frame bad; bad.type = MessageType::ERROR; bad.payload = Response{false, "malformed request", {}, {}, 0, {}, false, {}, ""}.encode(); if (!client.send_frame(bad)) break; continue; }
    Response resp = process(req);
    Frame out; out.type = req.type; out.payload = resp.encode();
    if (!client.send_frame(out)) break;
  }
  client.close();
}

// ---------------------------------------------------------------------------
// WorkerClient
// ---------------------------------------------------------------------------
bool WorkerClient::connect(const std::string& host, std::uint16_t port) { return c_.connect(host, port); }
void WorkerClient::close() { c_.close(); }

Response WorkerClient::request(const Request& req) {
  Frame f; f.type = req.type; f.payload = req.encode();
  if (!c_.send_frame(f)) { Response r; r.ok = false; r.error = "send failed"; return r; }
  Frame resp;
  if (!c_.recv_frame(resp)) { Response r; r.ok = false; r.error = "recv failed"; return r; }
  try { return Response::decode(resp.payload); }
  catch (...) { Response r; r.ok = false; r.error = "malformed response"; return r; }
}
Response WorkerClient::hello() { Request r; r.type = MessageType::HELLO; return request(r); }
Response WorkerClient::register_producer() { Request r; r.type = MessageType::REGISTER_PRODUCER; return request(r); }
Response WorkerClient::publish(const ArtifactDescriptor& desc, const std::vector<std::uint8_t>& content, const AuthorityEnvelope& auth) {
  Request r; r.type = MessageType::PUBLISH; r.descriptor = desc; r.content = content; r.authority = auth; return request(r);
}
Response WorkerClient::query(const ArtifactId& id) { Request r; r.type = MessageType::QUERY; r.artifact_id = id; return request(r); }
Response WorkerClient::invalidate(const ArtifactId& id, const std::string& cause, const AuthorityEnvelope& auth) {
  Request r; r.type = MessageType::INVALIDATE; r.artifact_id = id; r.cause = cause; r.authority = auth; return request(r);
}
Response WorkerClient::supersede(const ArtifactId& old_id, const ArtifactId& new_id, const std::string& reason, const AuthorityEnvelope& auth) {
  Request r; r.type = MessageType::SUPERSEDE; r.artifact_id = old_id; r.artifact_id_2 = new_id; r.cause = reason; r.authority = auth; return request(r);
}
Response WorkerClient::validate(const ArtifactId& id, bool ok, const AuthorityEnvelope& auth) {
  Request r; r.type = MessageType::VALIDATE; r.artifact_id = id; r.validation_ok = ok; r.authority = auth; return request(r);
}
Response WorkerClient::retire(const ArtifactId& id, const AuthorityEnvelope& auth) {
  Request r; r.type = MessageType::RETIRE; r.artifact_id = id; r.authority = auth; return request(r);
}
Response WorkerClient::roll_epoch() { Request r; r.type = MessageType::REPORT; r.cause = "roll_epoch"; return request(r); }
Response WorkerClient::save(const std::string& path) { Request r; r.type = MessageType::REPORT; r.cause = "save:" + path; return request(r); }

}  // namespace af
