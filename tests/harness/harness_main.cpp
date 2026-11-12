// Runs every registered test, grouped by the file that defines it, and
// exits non-zero when any test fails.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "harness.h"

namespace meter_test {

void builder_check(bool ok, const char *what) {
  if (ok) return;
  fprintf(stderr, "test setup error: %s\n", what);
  std::abort();
}

}  // namespace meter_test

int main() {
  size_t passed = 0;
  size_t failed = 0;
  std::string current_file;
  for (const meter_test::TestCase &test : meter_test::registry()) {
    if (test.file != current_file) {
      current_file = test.file;
      const char *slash = strrchr(test.file, '/');
      printf("== %s\n", slash == nullptr ? test.file : slash + 1);
    }
    meter_test::failures.clear();
    meter_test::case_label.clear();
    try {
      test.body();
    } catch (const meter_test::PreconditionFailed &) {
      // REQUIRE has already recorded the failure.
    }
    printf("%s %s\n", meter_test::failures.empty() ? "PASS" : "FAIL", test.name);
    for (const std::string &failure : meter_test::failures) printf("    %s\n", failure.c_str());
    (meter_test::failures.empty() ? passed : failed)++;
  }
  printf("%zu passed, %zu failed\n", passed, failed);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
