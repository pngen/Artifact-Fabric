#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include "artifact_fabric/distributed.hpp"
#include "artifact_fabric/digest.hpp"
#include "artifact_fabric/kind.hpp"

namespace af {

static std::vector<std::uint8_t> hex_bytes(const std::string& s) {
  std::vector<std::uint8_t> out;
  if (s.size() % 2) return out;
  auto nb = [](char c) -> int { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; if (c >= 'A' && c <= 'F') return c - 'A' + 10; return -1; };
  for (std::size_t i = 0; i < s.size(); i += 2) {
    int hi = nb(s[i]); int lo = nb(s[i + 1]);
    if (hi < 0 || lo < 0) return {};
    out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
  }
  return out;
}

static void write_result(const std::string& path, const std::string& content) {
  if (!path.empty()) {
    std::ofstream f(path, std::ios::trunc);
    if (f) f << content;
  }
  std::printf("%s\n", content.c_str());
  std::fflush(stdout);
}

static std::string response_line(const char* tag, const Response& r) {
  return std::string(tag) + " " + (r.ok ? "OK" : "ERR") + " " +
         (r.ok ? (r.message.empty() ? r.error : r.message) : r.error);
}

int worker_main(int argc, char** argv) {
  std::uint16_t port = 0;
  std::string mode = "hello", boothex, producerhex, contenthex, arch = "x86_64", abi = "sm_120", cc = "12.0";
  std::string aid, aid2, cause, kind = "COMPILED_KERNEL", result;
  std::uint64_t epoch = 0, gen = 0, pgen = 0;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : std::string(); };
    if (a == "--port") port = static_cast<std::uint16_t>(std::atoi(next().c_str()));
    else if (a == "--mode") mode = next();
    else if (a == "--boot") boothex = next();
    else if (a == "--producer") producerhex = next();
    else if (a == "--epoch") epoch = std::strtoull(next().c_str(), nullptr, 10);
    else if (a == "--content") contenthex = next();
    else if (a == "--arch") arch = next();
    else if (a == "--abi") abi = next();
    else if (a == "--cc") cc = next();
    else if (a == "--kind") kind = next();
    else if (a == "--gen") gen = std::strtoull(next().c_str(), nullptr, 10);
    else if (a == "--pgen") pgen = std::strtoull(next().c_str(), nullptr, 10);
    else if (a == "--id") aid = next();
    else if (a == "--id2") aid2 = next();
    else if (a == "--cause") cause = next();
    else if (a == "--result") result = next();
  }
  if (port == 0) { write_result(result, "ERR missing-port"); return 2; }

  WorkerClient client;
  if (!client.connect("127.0.0.1", port)) { write_result(result, "ERR connect"); return 1; }

  if (mode == "register") { write_result(result, response_line("REGISTER", client.register_producer())); return 0; }
  if (mode == "hello") { write_result(result, response_line("HELLO", client.hello())); return 0; }
  if (mode == "roll") {
    Response r = client.roll_epoch();
    std::string line = std::string("ROLL ") + (r.ok ? "OK" : "ERR") + " epoch=" + std::to_string(r.epoch) +
                       " boot=" + r.boot.to_string();
    write_result(result, line); return r.ok ? 0 : 1;
  }
  if (mode == "save") {
    Response r = client.save(cause);
    write_result(result, response_line("SAVE", r)); return r.ok ? 0 : 1;
  }
  if (mode == "accounting") {
    // Direct REPORT request to avoid adding a public method.
    Request rr; rr.type = MessageType::REPORT; rr.cause = "accounting";
    Frame rf; rf.type = MessageType::REPORT; rf.payload = rr.encode();
    if (client.send_raw_frame(rf)) {
      Frame back;
      if (client.recv_raw_frame(back)) {
        try { Response res = Response::decode(back.payload); write_result(result, "ACCOUNTING " + res.message); return 0; }
        catch (...) { write_result(result, "ACCOUNTING ERR malformed"); return 1; }
      }
    }
    write_result(result, "ACCOUNTING ERR recv"); return 1;
  }
  if (mode == "query") {
    ArtifactId id = ArtifactId::from_hex(aid);
    Response r = client.query(id);
    std::string line = "QUERY " + std::string(r.ok && r.found ? "OK found" : "OK notfound");
    if (r.ok && r.found) {
      line += " lifecycle="; line += lifecycle_name(r.descriptor.lifecycle);
      line += " kind="; line += kind_name(r.descriptor.kind);
      line += " gen="; line += std::to_string(r.descriptor.generation.value());
      line += " content="; line += r.descriptor.content_digest.to_string();
    }
    write_result(result, line); return 0;
  }

  AuthorityEnvelope auth;
  auth.epoch = epoch;
  if (!boothex.empty()) auth.boot = WorkerBootId::from_hex(boothex);
  if (!producerhex.empty()) auth.producer = ProducerId::from_hex(producerhex);
  auth.worker = WorkerId::random();
  auth.attempt = AttemptId::random();
  auth.attempt_generation = AttemptGeneration(1);
  auth.producer_generation = pgen ? ProducerGeneration(pgen) : ProducerGeneration(1);
  auto reg = client.register_producer();
  if (!reg.ok) { write_result(result, "ERR register " + reg.error); return 1; }
  if (auth.epoch == 0) auth.epoch = reg.epoch;
  if (auth.boot.is_zero()) auth.boot = reg.boot;
  if (auth.producer.is_zero()) auth.producer = ProducerId::from_bytes(reg.artifact_id.bytes());

  if (mode == "publish") {
    ArtifactDescriptor d;
    d.kind = (kind == "TENSOR_ARTIFACT") ? ArtifactKind::TENSOR_ARTIFACT : (kind == "ENGINE_ARTIFACT") ? ArtifactKind::ENGINE_ARTIFACT : ArtifactKind::COMPILED_KERNEL;
    d.generation = ArtifactGeneration(gen ? gen : 1);
    d.provenance = ProvenanceId::random();
    d.provenance_generation = ProvenanceGeneration(1);
    d.architecture = arch; d.abi = abi; d.compute_capability = cc;
    ContentDigest cd = ContentDigest(Sha256::digest(contenthex.data(), contenthex.size()));
    d.size_bytes = contenthex.empty() ? 0 : contenthex.size() / 2;
    auto content = hex_bytes(contenthex);
    Response r = client.publish(d, content, auth);
    std::string line = response_line("PUBLISH", r);
    if (r.ok && !r.artifact_id.is_zero()) line += " id=" + r.artifact_id.to_string();
    if (r.ok && !r.content_digest.is_zero()) line += " digest=" + r.content_digest.to_string();
    write_result(result, line); return r.ok ? 0 : 1;
  }
  if (mode == "invalidate") {
    ArtifactId id = ArtifactId::from_hex(aid);
    Response r = client.invalidate(id, cause.empty() ? "invalidate" : cause, auth);
    write_result(result, response_line("INVALIDATE", r)); return r.ok ? 0 : 1;
  }
  if (mode == "supersede") {
    ArtifactId oldid = ArtifactId::from_hex(aid);
    ArtifactId newid = ArtifactId::from_hex(aid2);
    Response r = client.supersede(oldid, newid, cause.empty() ? "supersede" : cause, auth);
    write_result(result, response_line("SUPERSEDE", r)); return r.ok ? 0 : 1;
  }
  if (mode == "validate") {
    ArtifactId id = ArtifactId::from_hex(aid);
    Response r = client.validate(id, true, auth);
    write_result(result, response_line("VALIDATE", r)); return r.ok ? 0 : 1;
  }
  if (mode == "retire") {
    ArtifactId id = ArtifactId::from_hex(aid);
    Response r = client.retire(id, auth);
    write_result(result, response_line("RETIRE", r)); return r.ok ? 0 : 1;
  }
  if (mode == "linger-publish") {
    ArtifactDescriptor d;
    d.kind = ArtifactKind::COMPILED_KERNEL;
    d.generation = ArtifactGeneration(gen ? gen : 1);
    d.provenance = ProvenanceId::random();
    d.provenance_generation = ProvenanceGeneration(1);
    d.architecture = arch; d.abi = abi; d.compute_capability = cc;
    d.size_bytes = contenthex.empty() ? 0 : contenthex.size() / 2;
    auto content = hex_bytes(contenthex);
    Response r = client.publish(d, content, auth);
    std::string line = response_line("PUBLISH", r);
    if (r.ok && !r.artifact_id.is_zero()) line += " id=" + r.artifact_id.to_string();
    if (r.ok) line += " epoch=" + std::to_string(r.epoch) + " boot=" + r.boot.to_string() +
                      " producer=" + auth.producer.to_string() + " pgen=" + std::to_string(auth.producer_generation.value());
    write_result(result, line);
    for (;;) { std::this_thread::sleep_for(std::chrono::milliseconds(1000)); }
  }
  write_result(result, "ERR unknown-mode");
  return 1;
}

}  // namespace af

int main(int argc, char** argv) { return af::worker_main(argc, argv); }
