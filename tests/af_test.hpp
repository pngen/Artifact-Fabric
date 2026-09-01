#pragma once
// Artifact Fabric - minimal deterministic test framework (no external deps).
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace testfw {

struct TestFailure : public std::runtime_error {
  explicit TestFailure(const std::string& m) : std::runtime_error(m) {}
};
struct TestCase { std::string name; std::function<void()> fn; };
inline std::vector<TestCase>& registry() { static std::vector<TestCase> r; return r; }
inline std::string& current() { static std::string c; return c; }

struct Registrar {
  Registrar(const char* name, std::function<void()> f) { registry().push_back({name, f}); }
};

inline int run_all() {
  int pass = 0, fail = 0, run = 0;
  for (auto& t : registry()) {
    ++run;
    current() = t.name;
    std::printf("[RUN ] %s\n", t.name.c_str());
    std::fflush(stdout);
    try {
      t.fn();
      ++pass;
      std::printf("[PASS] %s\n", t.name.c_str());
    } catch (const TestFailure& e) {
      ++fail;
      std::printf("[FAIL] %s : %s\n", t.name.c_str(), e.what());
    } catch (const std::exception& e) {
      ++fail;
      std::printf("[FAIL] %s (exception): %s\n", t.name.c_str(), e.what());
    } catch (...) {
      ++fail;
      std::printf("[FAIL] %s (unknown exception)\n", t.name.c_str());
    }
    std::fflush(stdout);
  }
  std::printf("----------------------------------------\n");
  std::printf("tests run: %d  passed: %d  failed: %d\n", run, pass, fail);
  return fail == 0 ? 0 : 1;
}

}  // namespace testfw

// Register and define a test. Use: AF_TEST(name) { ... checks ... }
#define AF_TEST(name) \
  static void af_test_##name(); \
  static ::testfw::Registrar af_reg_##name(#name, af_test_##name); \
  static void af_test_##name()

#define AF_CHECK(cond) \
  do { if (!(cond)) { \
    char buf[1024]; std::snprintf(buf, sizeof(buf), "CHECK failed in %s: %s", ::testfw::current().c_str(), #cond); \
    throw ::testfw::TestFailure(buf); \
  } } while (0)

#define AF_CHECK_MSG(cond, msg) \
  do { if (!(cond)) { \
    std::string m(msg); throw ::testfw::TestFailure(m); \
  } } while (0)

#define AF_CHECK_EQ(a, b) \
  do { if (!((a) == (b))) { \
    std::string m = std::string("EQ failed (") + #a + ") != (" + #b + ")"; \
    throw ::testfw::TestFailure(m); \
  } } while (0)

#define AF_EQ(a, b) AF_CHECK_EQ((a), (b))

#define AF_THROWS(expr) \
  do { bool threw = false; try { (void)(expr); } catch (...) { threw = true; } \
    AF_CHECK_MSG(threw, "expected exception from: " #expr); } while (0)
