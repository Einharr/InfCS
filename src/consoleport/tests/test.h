// A minimal test harness: TEST(name) { CHECK(...); }
#pragma once
#include <cstdio>
#include <string>
#include <vector>
#include <functional>

namespace cp { namespace test {

struct Case { const char* name; std::function<void()> fn; };
inline std::vector<Case>& cases() { static std::vector<Case> c; return c; }
inline int& failures() { static int f = 0; return f; }
inline int& checks() { static int c = 0; return c; }
struct Reg { Reg(const char* n, std::function<void()> f) { cases().push_back(Case{n, f}); } };

inline int run(int argc, char** argv)
{
    // Unbuffered: a test that dies must not take the earlier output with it.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const char* only = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    for (Case& c : cases()) {
        if (only && std::string(c.name).find(only) == std::string::npos) continue;
        int before = failures(); c.fn(); ++ran;
        std::printf("%s %s\n", failures() == before ? "ok  " : "FAIL", c.name);
    }
    std::printf("\n%d tests, %d checks, %d failures\n", ran, checks(), failures());
    return failures() ? 1 : 0;
}

}} // namespace cp::test

#define TEST(name) static void test_##name(); static cp::test::Reg reg_##name(#name, test_##name); static void test_##name()
#define CHECK(cond) do { ++cp::test::checks(); if (!(cond)) { ++cp::test::failures(); std::printf("  %s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { ++cp::test::checks(); auto _a = (a); auto _b = (b); if (!(_a == _b)) { ++cp::test::failures(); std::printf("  %s:%d: %s == %s  (%lld != %lld)\n", __FILE__, __LINE__, #a, #b, (long long)_a, (long long)_b); } } while (0)
