#pragma once

#include <cstdio>

inline int tests_passed = 0;
inline int tests_failed = 0;

#define TEST(name) \
    static void test_##name(); \
    struct test_reg_##name { \
        test_reg_##name() { \
            printf("  %-50s", #name); \
            try { test_##name(); printf(" \033[32mPASS\033[0m\n"); tests_passed++; } \
            catch (...) { printf(" \033[31mFAIL\033[0m\n"); tests_failed++; } \
        } \
    } test_instance_##name; \
    static void test_##name()

#define EXPECT(cond) do { if (!(cond)) { printf("\n    FAILED: %s (line %d)", #cond, __LINE__); throw 0; } } while(0)
