// Minimal unit-test harness for the host-side tests (no dependencies).
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace t {

struct Case {
  const char* name;
  void (*fn)();
};

std::vector<Case>& registry();
void fail(const char* file, int line, const std::string& what);

inline std::string show(const std::string& s) { return "\"" + s + "\""; }
inline std::string show(const char* s) { return s ? show(std::string(s)) : "(null)"; }
inline std::string show(bool b) { return b ? "true" : "false"; }
template <typename T>
std::string show(const T& v) {
  return std::to_string(v);
}

}  // namespace t

#define TEST(name)                                                                      \
  static void name();                                                                   \
  static const bool name##_registered = (t::registry().push_back({#name, name}), true); \
  static void name()

#define CHECK(cond)                                                \
  do {                                                             \
    if (!(cond)) t::fail(__FILE__, __LINE__, "CHECK(" #cond ")"); \
  } while (0)

#define CHECK_EQ(a, b)                                                                                    \
  do {                                                                                                    \
    const auto va_ = (a);                                                                                 \
    const auto vb_ = (b);                                                                                 \
    if (!(va_ == vb_))                                                                                    \
      t::fail(__FILE__, __LINE__, "CHECK_EQ(" #a ", " #b "): " + t::show(va_) + " vs " + t::show(vb_)); \
  } while (0)

#define CHECK_STR(a, b)                                                                                   \
  do {                                                                                                    \
    const std::string va_ = (a);                                                                          \
    const std::string vb_ = (b);                                                                          \
    if (va_ != vb_)                                                                                       \
      t::fail(__FILE__, __LINE__, "CHECK_STR(" #a ", " #b "): " + t::show(va_) + " vs " + t::show(vb_)); \
  } while (0)
