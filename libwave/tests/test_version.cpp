// SPDX-License-Identifier: GPL-3.0-or-later
#include <lvdwave/version.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("version string is non-empty and dotted") {
    auto v = lvd::wave::version();
    REQUIRE(!v.empty());
    REQUIRE(v.find('.') != std::string_view::npos);
}

TEST_CASE("version equals the CMake project version") {
    REQUIRE(lvd::wave::version() == LVD_EXPECTED_VERSION);
}
