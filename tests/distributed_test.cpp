#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifdef OPAQUE
#undef OPAQUE
#endif
#ifdef INTERFACE
#undef INTERFACE
#endif
#ifdef ERROR
#undef ERROR
#endif
#ifdef DELETE
#undef DELETE
#endif
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

#include "artifact_fabric/distributed.hpp"

using namespace af;

static const char* g_pass = "[PASS]";
static const char* g_fail = "[FAIL]";
static int g_failures = 0;

static void check(bool cond, const std::string& msg) {
  if (cond) std::printf("%s %s\n", g_pass, msg.c_str());
  else { std::printf("%s %s\n", g_fail, msg.c_str()); ++g_failures; }
  std::fflush(stdout);
}

// Spawn a process. If wait, block until exit; else return the process handle.
static HANDLE spawn(const std::string& exe, const std::string& args, bool wait) {
  std::string cmdline = """ + exe + "" " + args;
  std::vector<char> cmd(cmdline.begin(), cmdline.end()); cmd.push_back('\0');
  STARTUPINFOA si{}; si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  std::string appname = exe;
  if (!CreateProcessA(appname.empty() ? nullptr : appname.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
    std::printf("%s cannot spawn %s error=%lu\n", g_fail, exe.c_str(), (unsigned long)GetLastError()); ++g_failures; return nullptr;
  }
  CloseHandle(pi.hThread);
  if (wait) { WaitForSingleObject(pi.hProcess, INFINITE); CloseHandle(pi.hProcess); return nullptr; }
  return pi.hProcess;
}

static bool wait_file(const std::string& path, int secs) {
  for (int i = 0; i < secs * 20; ++i) {
    std::ifstream f(path); if (f.good()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return false;
}
static std::string read_file(const std::string& path) {
  std::ifstream f(path); std::stringstream ss; ss << f.rdbuf(); return ss.str();
}
static std::string get_field(const std::string& line, const std::string& key) {
  auto pos = line.find(key); if (pos == std::string::npos) return "";
  pos += key.size(); std::string val; while (pos < line.size() && line[pos] != ' ') { val += line[pos++]; }
  return val;
}
static bool wait_ready(int port, int secs) {
  for (int i = 0; i < secs * 20; ++i) {
    WorkerClient c;
    if (c.connect("127.0.0.1", static_cast<std::uint16_t>(port))) { auto r = c.hello(); if (r.ok) return true; c.close(); }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return false;
}

static std::string shell_quote(const std::string& s) {
  std::string r; r.push_back('"'); r += s; r.push_back('"'); return r;
}

int main(int argc, char** argv) {
  if (argc < 4) { std::printf("%s usage: af_distributed_test <coordinator.exe> <worker.exe> <scratch-dir>\n", g_fail); return 1; }
  std::string coordExe = argv[1], workExe = argv[2], scratch = argv[3];
  int port = 24400 + (GetCurrentProcessId() % 500);
  std::string state = scratch + "/dist_state.bin";
  std::string wa = scratch + "/wa.txt", wb = scratch + "/wb.txt", rollf = scratch + "/roll.txt";
  std::string r1 = scratch + "/r1.txt", r2 = scratch + "/r2.txt", r3 = scratch + "/r3.txt";
  std::string qc = scratch + "/qc.txt", fresh = scratch + "/fresh.txt", sup = scratch + "/sup.txt";
  std::string sv = scratch + "/sv.txt", acct = scratch + "/acct.txt", qa2 = scratch + "/qa2.txt", qb2 = scratch + "/qb2.txt";

  // Clean any stale state/results from prior runs so authority starts fresh.
  DeleteFileA(state.c_str());
  for (auto p : {wa, wb, rollf, r1, r2, r3, qc, fresh, sup, sv, acct, qa2, qb2}) DeleteFileA(p.c_str());

  // Start coordinator.
  HANDLE coord = spawn(coordExe, "--port " + std::to_string(port) + " --state " + shell_quote(state), false);
  check(coord != nullptr, "spawn coordinator");
  check(wait_ready(port, 10), "coordinator ready");

  // Worker A: linger-publish (publishes then sleeps). Capture its authority.
  HANDLE waProc = spawn(workExe, "--port " + std::to_string(port) + " --mode linger-publish --content deadbeef --gen 1 --result " + shell_quote(wa), false);
  check(waProc != nullptr, "spawn worker A");
  check(wait_file(wa, 20), "worker A produced artifact");
  std::string waLine = read_file(wa);
  std::printf("  workerA result: %s\n", waLine.c_str());
  std::string artA = get_field(waLine, "id=");
  std::string oldEpoch = get_field(waLine, "epoch=");
  std::string oldBoot = get_field(waLine, "boot=");
  std::string oldProducer = get_field(waLine, "producer=");
  check(!artA.empty() && !oldBoot.empty() && !oldProducer.empty(), "captured worker A authority");

  // Worker B consumes/validates the published artifact (real OS process).
  spawn(workExe, "--port " + std::to_string(port) + " --mode query --id " + artA + " --result " + shell_quote(wb), true);
  std::string wbLine = read_file(wb);
  std::printf("  workerB query: %s\n", wbLine.c_str());
  check(wbLine.find("OK found") != std::string::npos, "worker B sees published artifact");
  std::string artADigest = get_field(wbLine, "content=");

  // Kill worker A as a real OS process.
  bool killed = TerminateProcess(waProc, 0) != 0;
  CloseHandle(waProc);
  check(killed, "killed worker A as real OS process");

  // Roll the coordinator epoch.
  spawn(workExe, "--port " + std::to_string(port) + " --mode roll --result " + shell_quote(rollf), true);
  std::string rollLine = read_file(rollf);
  std::printf("  roll: %s\n", rollLine.c_str());
  check(rollLine.find("OK") != std::string::npos, "coordinator epoch rolled");

  // Replay stale mutations (old epoch/boot/producer) -> must all be rejected.
  spawn(workExe, "--port " + std::to_string(port) + " --mode publish --epoch " + oldEpoch + " --boot " + oldBoot + " --producer " + oldProducer + " --content deadbeef --gen 1 --result " + shell_quote(r1), true);
  std::string r1l = read_file(r1);
  std::printf("  stale publish: %s\n", r1l.c_str());
  check(r1l.find("ERR") != std::string::npos, "stale publish rejected");

  spawn(workExe, "--port " + std::to_string(port) + " --mode invalidate --id " + artA + " --epoch " + oldEpoch + " --boot " + oldBoot + " --producer " + oldProducer + " --result " + shell_quote(r2), true);
  std::string r2l = read_file(r2);
  std::printf("  stale invalidate: %s\n", r2l.c_str());
  check(r2l.find("ERR") != std::string::npos, "stale invalidation rejected");

  spawn(workExe, "--port " + std::to_string(port) + " --mode supersede --id " + artA + " --id2 " + artA + " --epoch " + oldEpoch + " --boot " + oldBoot + " --producer " + oldProducer + " --result " + shell_quote(r3), true);
  std::string r3l = read_file(r3);
  std::printf("  stale supersede: %s\n", r3l.c_str());
  check(r3l.find("ERR") != std::string::npos, "stale supersession rejected");

  // Verify authoritative state is unchanged.
  spawn(workExe, "--port " + std::to_string(port) + " --mode query --id " + artA + " --result " + shell_quote(qc), true);
  std::string qcl = read_file(qc);
  std::printf("  state unchanged: %s\n", qcl.c_str());
  check(qcl.find("lifecycle=PUBLISHED") != std::string::npos || qcl.find("lifecycle=ACTIVE") != std::string::npos, "authoritative state unchanged after stale replays");

  // Fresh artifact generation under current authority.
  spawn(workExe, "--port " + std::to_string(port) + " --mode publish --gen 2 --content cafe --result " + shell_quote(fresh), true);
  std::string fl = read_file(fresh);
  std::printf("  fresh gen: %s\n", fl.c_str());
  check(fl.find("OK") != std::string::npos, "fresh generation published");
  std::string artB = get_field(fl, "id=");

  // Supersede old artifact by fresh one.
  spawn(workExe, "--port " + std::to_string(port) + " --mode supersede --id " + artA + " --id2 " + artB + " --result " + shell_quote(sup), true);
  std::string sl = read_file(sup);
  std::printf("  supersede: %s\n", sl.c_str());
  check(sl.find("OK") != std::string::npos, "supersede old->fresh");

  // Save state.
  spawn(workExe, "--port " + std::to_string(port) + " --mode save --cause " + shell_quote(state) + " --result " + shell_quote(sv), true);
  std::string svl = read_file(sv);
  check(svl.find("OK") != std::string::npos, "catalog state saved");

  // Accounting: no leaked reservations / transient bytes.
  spawn(workExe, "--port " + std::to_string(port) + " --mode accounting --result " + shell_quote(acct), true);
  std::string al = read_file(acct);
  std::printf("  accounting: %s\n", al.c_str());
  check(al.find("reservations=0") != std::string::npos, "no leaked build reservations");
  check(al.find("temp_pub=0") != std::string::npos, "no transient publication bytes");

  // Kill coordinator, then restart and recover.
  TerminateProcess(coord, 0); CloseHandle(coord);
  HANDLE coord2 = spawn(coordExe, "--port " + std::to_string(port) + " --state " + shell_quote(state), false);
  check(coord2 != nullptr, "restart coordinator");
  check(wait_ready(port, 10), "coordinator restarted and recovered");

  // Verify recovered state: old superseded, fresh active, stable digests.
  spawn(workExe, "--port " + std::to_string(port) + " --mode query --id " + artA + " --result " + shell_quote(qa2), true);
  std::string qa2l = read_file(qa2);
  std::printf("  recovered old: %s\n", qa2l.c_str());
  check(qa2l.find("lifecycle=SUPERSEDED") != std::string::npos, "recovered old artifact SUPERSEDED");
  std::string artADigest2 = get_field(qa2l, "content=");
  check(artADigest2 == artADigest && !artADigest2.empty(), "recovered content digest stable");

  spawn(workExe, "--port " + std::to_string(port) + " --mode query --id " + artB + " --result " + shell_quote(qb2), true);
  std::string qb2l = read_file(qb2);
  std::printf("  recovered fresh: %s\n", qb2l.c_str());
  check(qb2l.find("OK found") != std::string::npos, "recovered fresh artifact present");

  TerminateProcess(coord2, 0); CloseHandle(coord2);

  std::printf("----------------------------------------\n");
  std::printf("distributed proof: %s\n", g_failures == 0 ? "ALL PASS" : "FAILURES");
  return g_failures == 0 ? 0 : 1;
}
