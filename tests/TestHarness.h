#pragma once

// Minimal dependency-free test harness, so the parser tests build without Qt.

#include <functional>
#include <iostream>
#include <utility>
#include <vector>

namespace test {

struct Case {
    const char *name;
    std::function<void()> fn;
};

inline std::vector<Case> &registry()
{
    static std::vector<Case> cases;
    return cases;
}

inline int &failures()
{
    static int count = 0;
    return count;
}

struct Registrar {
    Registrar(const char *name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

} // namespace test

#define TEST_CASE(name)                                       \
    static void name();                                       \
    static const test::Registrar name##_registrar(#name, name); \
    static void name()

#define CHECK(expr)                                                                       \
    do {                                                                                  \
        if (!(expr)) {                                                                    \
            ++test::failures();                                                           \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #expr "\n"; \
        }                                                                                 \
    } while (false)
