#pragma once

#include <cmath>
#include <cstdio>

inline int g_checks = 0, g_failed = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) ++g_failed, std::printf("%s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                              \
    do {                                                                                                   \
        ++g_checks;                                                                                        \
        const double a_ = (a), b_ = (b);                                                                   \
        if (std::fabs(a_ - b_) > (eps)) ++g_failed, std::printf("%s:%d: %s = %g, want %g\n", __FILE__, __LINE__, #a, a_, b_); \
    } while (0)

inline int check_report(const char* name) {
    std::printf("%-8s %d checks, %d failed\n", name, g_checks, g_failed);
    return g_failed ? 1 : 0;
}
