#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <fstream>
#include <iterator>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "artifact_fabric/catalog.hpp"
#include "artifact_fabric/compat.hpp"
#include "artifact_fabric/cuda.hpp"
#include "artifact_fabric/explain.hpp"

using namespace af;

static Catalog g_cat;
static AuthorityEnvelope g_auth;

static void ensure_authority() {
  if (g_cat.epoch() == 0) g_cat.set_authority(1, WorkerBootId::random());
  g_auth.epoch = g_cat.epoch();
  g_auth.boot = g_cat.boot();
  g_auth.producer = ProducerId::random();
  g_auth.producer_generation = ProducerGeneration(1);
  g_auth.attempt = AttemptId::random();
  g_auth.attempt_generation = AttemptGeneration(1);
}

static std::vector<std::uint8_t> read_file_bytes(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return {};
  return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
static std::vector<std::uint8_t> parse_content(const std::string& s) {
  if (s.rfind("file:", 0) == 0) return read_file_bytes(s.substr(5));
  std::vector<std::uint8_t> out;
  auto nb = [](char c)->int{ if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; if(c>='A'&&c<='F')return c-'A'+10; return -1; };
  std::string hex = s;
  if (hex.rfind("hex:", 0) == 0) hex = hex.substr(4);
  if (hex.size()%2) return out;
  for (std::size_t i=0;i<hex.size();i+=2){int h=nb(hex[i]);int l=nb(hex[i+1]);if(h<0||l<0)return {};out.push_back((std::uint8_t)((h<<4)|l));}
  return out;
}

static const char* arg(const std::vector<std::string>& v, const std::string& name) {
  for (std::size_t i=0;i+1<v.size();++i) if (v[i]==name) return v[i+1].c_str();
  return nullptr;
}

static void print_json_header(bool j) { if (j) std::printf("{"); }
static void print_json_kv(bool j, bool& first, const std::string& k, const std::string& v) {
  if (!j) return; if (!first) std::printf(","); std::printf("\"%s\":\"%s\"", k.c_str(), v.c_str()); first=false;
}
static void print_json_end(bool j) { if (j) std::printf("}\n"); }

int main(int argc, char** argv) {
  if (argc < 2) { std::printf("usage: af <subcommand> [args...] [--json]\n"); std::printf("  register|inspect|publish|validate|promote|supersede|invalidate|retire|check-reuse|show-dependencies|show-dependents|explain|snapshot|save|recover|replay|benchmark\n"); return 1; }
  std::string cmd = argv[1];
  std::vector<std::string> rest;
  for (int i=2;i<argc;++i) rest.push_back(argv[i]);
  bool json = false;
  for (const auto& r : rest) if (r == "--json") json = true;
  bool first = true;
  // Make the CLI stateful across invocations via a default state file.
  {
    std::ifstream probe("af_state.bin", std::ios::binary);
    if (probe.good()) { probe.close(); try { g_cat.load_file("af_state.bin", true); } catch (...) {} }
  }
  std::atexit([] { try { if (g_cat.epoch() != 0) g_cat.save_file("af_state.bin"); } catch (...) {} });
  ensure_authority();

  auto id_of = [&](const char* s) { return ArtifactId::from_hex(s); };

  if (cmd == "publish") {
    const char* kind = arg(rest,"--kind"); const char* content = arg(rest,"--content");
    const char* gen = arg(rest,"--gen"); const char* cc = arg(rest,"--cc"); const char* abi = arg(rest,"--abi");
    ArtifactDescriptor d;
    d.kind = (kind && std::string(kind)=="ENGINE_ARTIFACT") ? ArtifactKind::ENGINE_ARTIFACT : (kind && std::string(kind)=="TENSOR_ARTIFACT") ? ArtifactKind::TENSOR_ARTIFACT : ArtifactKind::COMPILED_KERNEL;
    if (gen) d.generation = ArtifactGeneration(std::strtoull(gen,nullptr,10)); else d.generation = ArtifactGeneration(1);
    d.provenance = ProvenanceId::random(); d.provenance_generation = ProvenanceGeneration(1);
    d.architecture = "x86_64"; if (cc) d.compute_capability = cc; else d.compute_capability = "12.0";
    if (abi) d.abi = abi; else d.abi = "sm_120";
    auto body = content ? parse_content(content) : std::vector<std::uint8_t>{1,2,3,4};
    d.size_bytes = body.size();
    PublishRequest req; req.descriptor = d; req.content = body; req.authority = g_auth;
    PublishResult pr = g_cat.publish(req);
    if (json) print_json_header(true);
    std::printf("%s %s\n", pr.committed ? "published" : "FAILED", pr.committed ? pr.id.to_string().c_str() : (pr.error.empty()?"error":pr.error).c_str());
    if (json) { print_json_kv(json,first,"id",pr.id.to_string()); print_json_kv(json,first,"digest",pr.content_digest.to_string()); }
    print_json_end(json);
    return pr.committed?0:1;
  }
  if (cmd == "inspect") {
    ArtifactId id = id_of(arg(rest,"--id"));
    const ArtifactDescriptor* d = g_cat.find(id);
    if (!d) { std::printf("not found\n"); return 1; }
    if (json) print_json_header(true);
    std::printf("artifact %s kind=%s gen=%llu lifecycle=%s validation=%s content=%s\n", d->id.to_string().c_str(), kind_name(d->kind), (unsigned long long)d->generation.value(), lifecycle_name(d->lifecycle), validation_state_name(d->validation_state), d->content_digest.to_string().c_str());
    if (json) { print_json_kv(json,first,"kind",kind_name(d->kind)); print_json_kv(json,first,"lifecycle",lifecycle_name(d->lifecycle)); print_json_kv(json,first,"validation",validation_state_name(d->validation_state)); print_json_kv(json,first,"content",d->content_digest.to_string()); }
    print_json_end(json);
    return 0;
  }
  if (cmd == "invalidate") {
    ArtifactId id = id_of(arg(rest,"--id"));
    const char* cause = arg(rest,"--cause");
    try { g_cat.invalidate(id, cause?cause:"operator", g_auth); std::printf("invalidated %s\n", id.to_string().c_str()); return 0; }
    catch (const std::exception& e) { std::printf("ERR %s\n", e.what()); return 1; }
  }
  if (cmd == "supersede") {
    ArtifactId oldid = id_of(arg(rest,"--id"));
    ArtifactId newid = id_of(arg(rest,"--id2"));
    const char* reason = arg(rest,"--reason");
    try { g_cat.supersede(oldid, newid, reason?reason:"supersede", g_auth); std::printf("superseded %s by %s\n", oldid.to_string().c_str(), newid.to_string().c_str()); return 0; }
    catch (const std::exception& e) { std::printf("ERR %s\n", e.what()); return 1; }
  }
  if (cmd == "retire") {
    ArtifactId id = id_of(arg(rest,"--id"));
    try { g_cat.retire(id, g_auth); std::printf("retired %s\n", id.to_string().c_str()); return 0; }
    catch (const std::exception& e) { std::printf("ERR %s\n", e.what()); return 1; }
  }
  if (cmd == "promote") {
    ArtifactId id = id_of(arg(rest,"--id"));
    try { g_cat.promote(id, g_auth); std::printf("promoted %s\n", id.to_string().c_str()); return 0; }
    catch (const std::exception& e) { std::printf("ERR %s\n", e.what()); return 1; }
  }
  if (cmd == "validate") {
    ArtifactId id = id_of(arg(rest,"--id"));
    ValidationReport vr; vr.state = ValidationState::VALID;
    try { g_cat.validate_artifact(id, vr, g_auth); std::printf("validated %s\n", id.to_string().c_str()); return 0; }
    catch (const std::exception& e) { std::printf("ERR %s\n", e.what()); return 1; }
  }
  if (cmd == "check-reuse") {
    ArtifactId id = id_of(arg(rest,"--id"));
    const char* cc = arg(rest,"--cc");
    CompatRequirement rq; rq.require_reusable = true; if (cc) rq.compute_capability = cc;
    ReuseResult rr = g_cat.reuse(id, rq);
    if (json) print_json_header(true);
    std::printf("reuse=%s %s\n", rr.reusable?"REUSABLE":"NOT_REUSABLE", rr.reason_text().c_str());
    if (json) { print_json_kv(json,first,"reusable",rr.reusable?"true":"false"); print_json_kv(json,first,"reason",rr.reason_text()); }
    print_json_end(json);
    return rr.reusable?0:1;
  }
  if (cmd == "show-dependencies") {
    ArtifactId id = id_of(arg(rest,"--id"));
    auto deps = g_cat.dependencies_of(id);
    if (json) print_json_header(true);
    std::printf("dependencies(%d):", (int)deps.size());
    for (auto& d : deps) std::printf(" %s", d.to_string().c_str());
    std::printf("\n");
    if (json) { bool f=true; std::printf("\"deps\":["); for (auto& d: deps){ if(!f)std::printf(","); std::printf("\"%s\"", d.to_string().c_str()); f=false; } std::printf("]"); first=false; print_json_kv(json,first,"count",std::to_string(deps.size())); }
    print_json_end(json);
    return 0;
  }
  if (cmd == "show-dependents") {
    ArtifactId id = id_of(arg(rest,"--id"));
    auto deps = g_cat.dependents_of(id);
    if (json) print_json_header(true);
    std::printf("dependents(%d):", (int)deps.size());
    for (auto& d : deps) std::printf(" %s", d.to_string().c_str());
    std::printf("\n");
    print_json_end(json);
    return 0;
  }
  if (cmd == "explain") {
    ArtifactId id = id_of(arg(rest,"--id"));
    Explanation ex = g_cat.explain(id);
    if (json) std::printf("%s\n", ex.to_json().c_str());
    else std::printf("%s", ex.to_text().c_str());
    std::printf("explain_digest %s\n", ex.digest().c_str());
    return 0;
  }
  if (cmd == "snapshot") {
    auto img = g_cat.save();
    std::printf("snapshot %d bytes artifacts=%d backing=%d\n", (int)img.size(), (int)g_cat.artifact_count(), (int)g_cat.physical_backing_count());
    return 0;
  }
  if (cmd == "save") {
    const char* path = arg(rest,"--path");
    if (!path) path = "af_state.bin";
    g_cat.save_file(path); std::printf("saved %s\n", path); return 0;
  }
  if (cmd == "recover") {
    const char* path = arg(rest,"--path");
    if (!path) path = "af_state.bin";
    g_cat.load_file(path, true); std::printf("recovered %s artifacts=%d\n", path, (int)g_cat.artifact_count()); return 0;
  }
  if (cmd == "register") {
    const char* kind = arg(rest,"--kind");
    ArtifactDescriptor d; d.kind = ArtifactKind::OTHER; if (kind && std::string(kind)=="MANIFEST") d.kind = ArtifactKind::MANIFEST;
    d.provenance = ProvenanceId::random(); d.provenance_generation = ProvenanceGeneration(1);
    d.generation = ArtifactGeneration(1);
    ArtifactId id = g_cat.register_artifact(d, g_auth);
    std::printf("registered %s\n", id.to_string().c_str()); return 0;
  }
  if (cmd == "replay") {
    // Deterministic replay: save then recover and check digests reproduce.
    auto img = g_cat.save();
    ArtifactId id = id_of(arg(rest,"--id"));
    std::string before;
    if (const ArtifactDescriptor* d = g_cat.find(id)) before = d->metadata_digest.to_string();
    Catalog tmp; tmp.set_authority(g_cat.epoch(), g_cat.boot()); tmp.load(img);
    const ArtifactDescriptor* d = tmp.find(id);
    std::string after = d ? d->metadata_digest.to_string() : "<none>";
    std::printf("replay digest_stable=%s\n", (before==after)?"true":"false");
    return (before==after)?0:1;
  }
  if (cmd == "benchmark") {
    const char* n = arg(rest,"--n");
    int iters = n ? std::atoi(n) : 1000;
    std::clock_t start = std::clock();
    for (int i=0;i<iters;++i) {
      ArtifactDescriptor d; d.kind = ArtifactKind::TENSOR_ARTIFACT; d.generation = ArtifactGeneration(1);
      d.provenance = ProvenanceId::random(); d.provenance_generation = ProvenanceGeneration(1);
      d.size_bytes = 4;
      PublishRequest req; req.descriptor = d; req.content = {1,2,3,4}; req.authority = g_auth;
      g_cat.publish(req);
    }
    std::clock_t end = std::clock();
    double ms = (end - start) * 1000.0 / CLOCKS_PER_SEC;
    std::printf("benchmark publish x%d: %.2f ms (%.2f us/op)\n", iters, ms, ms*1000.0/iters);
    return 0;
  }
  if (cmd == "cuda-proof") {
    CudaProofResult r = run_cuda_proof();
    std::printf("cuda_proof %s %s\n", r.ok?"OK":"FAIL", r.detail.c_str());
    return r.ok?0:1;
  }
  std::printf("unknown subcommand %s\n", cmd.c_str());
  return 1;
}
