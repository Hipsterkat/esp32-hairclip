#include "test.h"

namespace t {

std::vector<Case>& registry() {
  static std::vector<Case> cases;
  return cases;
}

static int g_failures = 0;
static const char* g_current = "";

void fail(const char* file, int line, const std::string& what) {
  ++g_failures;
  std::fprintf(stderr, "FAIL %s (%s:%d): %s\n", g_current, file, line, what.c_str());
}

}  // namespace t

int main() {
  int failedCases = 0;
  for (const t::Case& c : t::registry()) {
    const int before = t::g_failures;
    t::g_current = c.name;
    c.fn();
    if (t::g_failures != before) ++failedCases;
  }
  std::printf("%zu tests, %d failed (%d failed checks)\n", t::registry().size(), failedCases, t::g_failures);
  return t::g_failures == 0 ? 0 : 1;
}
