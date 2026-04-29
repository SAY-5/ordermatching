// Tiny header-only test harness. We don't pull in Catch2 / GoogleTest
// for these focused tests — a couple of macros + main() loop is plenty
// and avoids a network fetch in CI.

#pragma once
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace check {

struct Case {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}

struct Reg {
    Reg(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); }
};

inline int run() {
    int failed = 0;
    for (const auto& c : registry()) {
        try {
            c.fn();
            std::printf("  ok  %s\n", c.name.c_str());
        } catch (const std::exception& e) {
            std::printf("FAIL  %s: %s\n", c.name.c_str(), e.what());
            ++failed;
        }
    }
    std::printf("%d/%zu passed\n", (int)registry().size() - failed, registry().size());
    return failed ? 1 : 0;
}

}  // namespace check

#define TEST_CASE(name)                                                      \
    static void test_##name();                                               \
    static check::Reg reg_##name{#name, &test_##name};                       \
    static void test_##name()

#define REQUIRE(cond)                                                        \
    do {                                                                     \
        if (!(cond))                                                         \
            throw std::runtime_error(std::string(#cond " failed at ") +      \
                                     __FILE__ + ":" + std::to_string(__LINE__)); \
    } while (0)

#define REQUIRE_EQ(a, b)                                                     \
    do {                                                                     \
        auto _a = (a);                                                       \
        auto _b = (b);                                                       \
        if (!(_a == _b))                                                     \
            throw std::runtime_error(std::string(#a " == " #b " failed at ") + \
                                     __FILE__ + ":" + std::to_string(__LINE__)); \
    } while (0)
