// SPDX-FileCopyrightText: 2026 Maxim Dokukin (maxdokukin.com)
// SPDX-License-Identifier: GPL-3.0-only
// xewe-os-modules/modules/web-interface/tests/unit/test_web_interface_api.cpp
//
// Host test of WebInterfaceApi.h: the CORS headers and the two answers of the catch-all, byte for
// byte. Built by test_web-interface.py with g++ -std=c++17 -Wall -Wextra -Werror.

#include "../../src/WebInterface/WebInterfaceApi.h"

#include <cstring>
#include <iostream>

namespace {

int checks = 0;
int failures = 0;

#define CHECK(expr) \
    do { \
        ++checks; \
        if (expr) { \
            std::cout << "ok   " #expr "\n"; \
        } else { \
            ++failures; \
            std::cout << "FAIL " #expr " (line " << __LINE__ << ")\n"; \
        } \
    } while (0)

namespace api = web_interface_api;

bool eq(const char* a, const char* b) {
    return a != nullptr && b != nullptr && std::strcmp(a, b) == 0;
}

bool same(const api::Answer& a, const api::Answer& b) {
    return a.code == b.code && a.type == b.type && a.body == b.body;
}

} // namespace

int main() {
    // the three headers, in the order they are sent
    CHECK(sizeof(api::CORS_HEADERS) / sizeof(api::CORS_HEADERS[0]) == 3);
    CHECK(eq(api::CORS_HEADERS[0][0], "Access-Control-Allow-Origin"));
    CHECK(eq(api::CORS_HEADERS[0][1], "*"));
    CHECK(eq(api::CORS_HEADERS[1][0], "Access-Control-Allow-Methods"));
    CHECK(eq(api::CORS_HEADERS[1][1], "POST, GET, OPTIONS, DELETE, PUT"));
    CHECK(eq(api::CORS_HEADERS[2][0], "Access-Control-Allow-Headers"));
    CHECK(eq(api::CORS_HEADERS[2][1], "Content-Type"));

    // OPTIONS: 204, the server's default type (text/html), empty body
    CHECK(api::PREFLIGHT.code == 204);
    CHECK(api::PREFLIGHT.type == nullptr);
    CHECK(eq(api::PREFLIGHT.body, ""));

    // any other method: 404 text/plain "404: Not Found" (14 bytes)
    CHECK(api::NOT_FOUND.code == 404);
    CHECK(eq(api::NOT_FOUND.type, "text/plain"));
    CHECK(eq(api::NOT_FOUND.body, "404: Not Found"));
    CHECK(std::strlen(api::NOT_FOUND.body) == 14);

    // answer() picks PREFLIGHT only for OPTIONS; usable at compile time
    constexpr api::Answer preflight = api::answer(true);
    constexpr api::Answer missing = api::answer(false);
    static_assert(preflight.code == 204, "OPTIONS answers 204");
    static_assert(missing.code == 404, "other methods answer 404");
    CHECK(same(preflight, api::PREFLIGHT));
    CHECK(same(missing, api::NOT_FOUND));
    CHECK(preflight.code == 204 && preflight.type == nullptr && eq(preflight.body, ""));
    CHECK(missing.code == 404 && eq(missing.type, "text/plain") &&
        eq(missing.body, "404: Not Found"));

    std::cout << (failures ? "FAILED" : "PASSED") << ": " << checks << " check(s), " << failures
              << " failure(s)\n";
    return failures ? 1 : 0;
}
