// SPDX-License-Identifier: GPL-3.0-only
// Shared test harness: no framework. Each CHECK prints on failure; the exit code is the failure count.
#pragma once
#include <atomic>
#include <cstdio>
#include <string>

extern std::atomic<long> gAllocs;
extern int gFailures, gChecks;

#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        ++gChecks;                                                         \
        if (!(cond)) {                                                     \
            ++gFailures;                                                   \
            std::printf("FAIL %s:%d: %s — ", __FILE__, __LINE__, #cond);  \
            std::printf(__VA_ARGS__);                                      \
            std::printf("\n");                                             \
        }                                                                  \
    } while (0)

std::string readFile(const std::string& path, bool* ok = nullptr);
std::string fixture(const char* name);  // path under engine/tests/fixtures

void runFixtureTests();
