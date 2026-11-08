// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <library_map.hpp>

using namespace lvd::tools;

TEST_CASE("lvdsim.ini library section is parsed") {
    const auto m = LibraryMap::parse(
        "; comment\n"
        "[Version]\n"
        "work = ignored\n"
        "[Library]\n"
        "work = work\n"
        "  UniSim = libs/unisim  \n"
        "# another comment\n"
        "\n"
        "[vcom]\n"
        "work = alsoIgnored\n");
    REQUIRE(m.entries().size() == 2);
    REQUIRE(m.pathOf("work").value() == "work");
    REQUIRE(m.pathOf("unisim").value() == "libs/unisim");  // case-insensitive
    REQUIRE(m.pathOf("UNISIM").value() == "libs/unisim");
    REQUIRE_FALSE(m.pathOf("other").has_value());
}

TEST_CASE("empty and malformed input gives an empty map") {
    REQUIRE(LibraryMap::parse("").entries().empty());
    REQUIRE(LibraryMap::parse("[Library]\nnot a mapping\n=novalue\n").entries().empty());
}

TEST_CASE("set replaces an existing name regardless of case") {
    LibraryMap m;
    m.set("Work", "a");
    m.set("WORK", "b");
    REQUIRE(m.entries().size() == 1);
    REQUIRE(m.pathOf("work").value() == "b");
}

TEST_CASE("withLibraryMapping edits only the library section") {
    const std::string ini =
        "[Version]\n"
        "x = 1\n"
        "[Library]\n"
        "work = work\n"
        "[vsim]\n"
        "y = 2\n";

    SECTION("adds a new entry at the end of the section") {
        const std::string out = withLibraryMapping(ini, "unisim", "libs/unisim");
        REQUIRE(out ==
                "[Version]\nx = 1\n[Library]\nwork = work\nunisim = libs/unisim\n[vsim]\ny = 2\n");
    }
    SECTION("replaces an existing entry in place") {
        const std::string out = withLibraryMapping(ini, "WORK", "other");
        REQUIRE(out == "[Version]\nx = 1\n[Library]\nWORK = other\n[vsim]\ny = 2\n");
    }
    SECTION("creates the section when there is none") {
        REQUIRE(withLibraryMapping("[vsim]\ny = 2\n", "a", "b") == "[vsim]\ny = 2\n\n[Library]\na = b\n");
        REQUIRE(withLibraryMapping("", "a", "b") == "[Library]\na = b\n");
    }
    SECTION("keeps Windows line endings") {
        const std::string out = withLibraryMapping("[Library]\r\nwork = work\r\n", "a", "b");
        REQUIRE(out == "[Library]\r\nwork = work\r\na = b\r\n");
    }
    SECTION("round trips through parse") {
        const auto m = LibraryMap::parse(withLibraryMapping(ini, "unisim", "libs/unisim"));
        REQUIRE(m.pathOf("unisim").value() == "libs/unisim");
        REQUIRE(m.pathOf("work").value() == "work");
    }
}

TEST_CASE("resolveLibrary prefers the mapping, then a directory of that name") {
    LibraryMap m;
    m.set("mapped", "somewhere/else");
    const auto exists = [](const std::string& p) { return p == "work" || p == "mapped"; };
    REQUIRE(resolveLibrary("mapped", m, exists).value() == "somewhere/else");
    REQUIRE(resolveLibrary("work", m, exists).value() == "work");
    REQUIRE_FALSE(resolveLibrary("nothere", m, exists).has_value());
}
