// HOST-ONLY test runner.  Prints the pass/fail count and exits non-zero on failure.
#include <cstdio>
#include <exception>

#include "host/tests/test_framework.h"

int main() {
  auto& c = q207::test::ctx();
  std::printf("Q2-07 host slice :: unit tests (HOST-ONLY, not a device result)\n");
  try {
    q207::test::suite_wire();
    q207::test::suite_vectors();
    q207::test::suite_tape();
    q207::test::suite_decode();
    q207::test::suite_object_model();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "UNCAUGHT EXCEPTION: %s\n", e.what());
    return 2;
  } catch (...) {
    std::fprintf(stderr, "UNCAUGHT EXCEPTION (unknown)\n");
    return 2;
  }
  std::printf("\nTESTS PASSED: %d   FAILED: %d\n", c.passed, c.failed);
  return c.failed == 0 ? 0 : 1;
}
