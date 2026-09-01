#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include "artifact_fabric/distributed.hpp"

namespace af {
int coordinator_main(int argc, char** argv) {
  std::uint16_t port = 0;
  std::string state;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
    else if (std::strcmp(argv[i], "--state") == 0 && i + 1 < argc) state = argv[++i];
  }
  if (port == 0) { std::fprintf(stderr, "usage: af_coordinator --port N [--state PATH]\n"); return 2; }
  Coordinator c(port, state);
  if (!state.empty()) {
    std::ifstream probe(state, std::ios::binary);
    bool exists = probe.good();
    probe.close();
    if (exists) {
      try { c.catalog().load_file(state, true); }
      catch (const std::exception& e) { std::fprintf(stderr, "coordinator: load failed: %s\n", e.what()); }
    }
  }
  if (!c.start()) { std::fprintf(stderr, "coordinator: bind failed\n"); return 1; }
  std::printf("COORDINATOR READY port=%u epoch=%llu\n", (unsigned)port, (unsigned long long)c.catalog().epoch());
  std::fflush(stdout);
  c.run_forever();
  return 0;
}
}  // namespace af

int main(int argc, char** argv) { return af::coordinator_main(argc, argv); }
