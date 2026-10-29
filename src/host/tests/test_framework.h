// HOST-ONLY minimal test framework (no external deps).
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "host/common/status.h"

namespace q207 {
namespace test {

struct Ctx {
  int passed = 0;
  int failed = 0;
  const char* suite = "";

  void begin(const char* name) {
    suite = name;
    std::printf("-- %s\n", name);
    std::fflush(stdout);
  }

  void check(bool ok, const char* expr, const char* file, int line) {
    if (ok) {
      ++passed;
      return;
    }
    ++failed;
    std::fprintf(stderr, "FAIL [%s] %s  (%s:%d)\n", suite, expr, file, line);
  }

  void eq_u64(uint64_t a, uint64_t b, const char* what, const char* file, int line) {
    if (a == b) {
      ++passed;
      return;
    }
    ++failed;
    std::fprintf(stderr, "FAIL [%s] %s: %llu != %llu  (%s:%d)\n", suite, what,
                 static_cast<unsigned long long>(a), static_cast<unsigned long long>(b), file,
                 line);
  }

  void eq_i64(long long a, long long b, const char* what, const char* file, int line) {
    if (a == b) {
      ++passed;
      return;
    }
    ++failed;
    std::fprintf(stderr, "FAIL [%s] %s: %lld != %lld  (%s:%d)\n", suite, what, a, b, file, line);
  }

  void eq_str(const std::string& a, const std::string& b, const char* what, const char* file,
              int line) {
    if (a == b) {
      ++passed;
      return;
    }
    ++failed;
    std::fprintf(stderr, "FAIL [%s] %s:\n  got : %s\n  want: %s  (%s:%d)\n", suite, what,
                 a.c_str(), b.c_str(), file, line);
  }

  void eq_code(Code a, Code b, const char* what, const char* file, int line) {
    if (a == b) {
      ++passed;
      return;
    }
    ++failed;
    std::fprintf(stderr, "FAIL [%s] %s: got %s want %s  (%s:%d)\n", suite, what, code_name(a),
                 code_name(b), file, line);
  }
};

inline Ctx& ctx() {
  static Ctx c;
  return c;
}

// hex helpers
inline std::vector<uint8_t> unhex(const char* h) {
  std::vector<uint8_t> out;
  size_t n = std::strlen(h);
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i + 1 < n; i += 2) {
    const int hi = nib(h[i]);
    const int lo = nib(h[i + 1]);
    if (hi < 0 || lo < 0) return out;
    out.push_back(static_cast<uint8_t>((hi << 4) | lo));
  }
  return out;
}

inline std::string hex(const uint8_t* p, size_t n) {
  static const char* k = "0123456789abcdef";
  std::string out;
  out.reserve(n * 2);
  for (size_t i = 0; i < n; ++i) {
    out.push_back(k[p[i] >> 4]);
    out.push_back(k[p[i] & 0xF]);
  }
  return out;
}

inline std::string hex(const std::vector<uint8_t>& v) { return hex(v.data(), v.size()); }

}  // namespace test
}  // namespace q207

#define TCHECK(x) q207::test::ctx().check((x), #x, __FILE__, __LINE__)
#define TEQ_U64(a, b) q207::test::ctx().eq_u64((a), (b), #a " == " #b, __FILE__, __LINE__)
#define TEQ_I64(a, b) q207::test::ctx().eq_i64((a), (b), #a " == " #b, __FILE__, __LINE__)
#define TEQ_STR(a, b) q207::test::ctx().eq_str((a), (b), #a " == " #b, __FILE__, __LINE__)
#define TEQ_CODE(a, b) q207::test::ctx().eq_code((a), (b), #a " == " #b, __FILE__, __LINE__)

// Suite entry points (each defined in its own TU).
namespace q207 {
namespace test {
void suite_wire();
void suite_vectors();
void suite_tape();
void suite_decode();
}  // namespace test
}  // namespace q207
