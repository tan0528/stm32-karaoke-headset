/*
 * test_util.h -- 极简断言框架（不引第三方依赖）
 *
 * 故意不做成"框架"：只有计数、断言、用例分组。
 * 想看某个值到底差多少，用 CHECK_NEAR 打印出来的数就够定位了。
 */
#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <math.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern int g_checks;
extern int g_fails;

void test_run_case(const char *name, void (*fn)(void));

#define CHECK(cond)                                                         \
    do {                                                                    \
        g_checks++;                                                         \
        if (!(cond)) {                                                      \
            g_fails++;                                                      \
            printf("      x %s:%d  %s\n", __FILE__, __LINE__, #cond);       \
        }                                                                   \
    } while (0)

#define CHECK_MSG(cond, ...)                                                \
    do {                                                                    \
        g_checks++;                                                         \
        if (!(cond)) {                                                      \
            g_fails++;                                                      \
            printf("      x %s:%d  %s | ", __FILE__, __LINE__, #cond);      \
            printf(__VA_ARGS__);                                            \
            printf("\n");                                                   \
        }                                                                   \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                               \
    do {                                                                    \
        double _a = (double)(a);                                            \
        double _b = (double)(b);                                            \
        double _t = (double)(tol);                                          \
        g_checks++;                                                         \
        if (fabs(_a - _b) > _t) {                                           \
            g_fails++;                                                      \
            printf("      x %s:%d  %s=%.6g  vs  %s=%.6g  (|d|=%.3g > %.3g)\n", \
                   __FILE__, __LINE__, #a, _a, #b, _b, fabs(_a - _b), _t);  \
        }                                                                   \
    } while (0)

#define CHECK_RANGE(v, lo, hi)                                              \
    do {                                                                    \
        double _v = (double)(v);                                            \
        g_checks++;                                                         \
        if (_v < (double)(lo) || _v > (double)(hi)) {                       \
            g_fails++;                                                      \
            printf("      x %s:%d  %s=%.6g 不在 [%.6g, %.6g]\n",            \
                   __FILE__, __LINE__, #v, _v, (double)(lo), (double)(hi)); \
        }                                                                   \
    } while (0)

#define RUN(fn) test_run_case(#fn, fn)

#endif /* TEST_UTIL_H */
