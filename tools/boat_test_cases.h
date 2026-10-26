#ifndef BOAT_TEST_CASES_H
#define BOAT_TEST_CASES_H

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// A validator's named cases. Each returns 0 on success (a failed CHECK may also exit). CMake's
// proteus_add_case_tests registers one CTest entry per case from --list; a slow case is
// labelled `slow` instead of `quick`.
typedef struct {
    const char *name;
    int (*run)(void);
    bool slow;
} TestCase;

static inline int test_case_run(const TestCase *test)
{
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    int result = test->run();
    clock_gettime(CLOCK_MONOTONIC, &end);
    double ms = (end.tv_sec - start.tv_sec) * 1e3 + (end.tv_nsec - start.tv_nsec) / 1e6;
    printf("case=%s ms=%.1f%s\n", test->name, ms, result ? " FAILED" : "");
    fflush(stdout);
    return result;
}

// No arguments runs every case in order and stops at the first failure; --list prints
// "name quick|slow" lines; --case NAME runs one case. Returns 2 after a usage error.
static inline int test_cases_main(const TestCase *cases, size_t count, int argc, char **argv)
{
    if (argc == 1) {
        for (size_t i = 0; i < count; ++i)
            if (test_case_run(&cases[i]))
                return 1;
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--list") == 0) {
        for (size_t i = 0; i < count; ++i)
            printf("%s %s\n", cases[i].name, cases[i].slow ? "slow" : "quick");
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--case") == 0) {
        for (size_t i = 0; i < count; ++i)
            if (strcmp(argv[2], cases[i].name) == 0)
                return test_case_run(&cases[i]);
        fprintf(stderr, "Unknown case: %s\n", argv[2]);
        return 2;
    }
    fprintf(stderr, "Usage: %s [--list | --case NAME]\n", argv[0]);
    return 2;
}

// Table entries named after their function.
#define TEST_CASE(fn) { #fn, fn, false }
#define SLOW_CASE(fn) { #fn, fn, true }
#define TEST_CASE_COUNT(cases) (sizeof(cases) / sizeof((cases)[0]))

#endif
