// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <lvdwave/radix.hpp>

using namespace lvd::wave;

TEST_CASE("hex and octal grouping") {
    REQUIRE(formatBits("11110000", Radix::Hex) == "F0");
    REQUIRE(formatBits("10101", Radix::Hex) == "15");      // partial leading group
    REQUIRE(formatBits("1", Radix::Hex) == "1");
    REQUIRE(formatBits("111000101", Radix::Octal) == "705");
    REQUIRE(formatBits("1111", Radix::Octal) == "17");
}

TEST_CASE("unknown bits in digit groups") {
    REQUIRE(formatBits("1111XXXX", Radix::Hex) == "FX");
    REQUIRE(formatBits("1X10ZZZZ", Radix::Hex) == "XZ");
    REQUIRE(formatBits("UUUU0001", Radix::Hex) == "U1");
    REQUIRE(formatBits("1Z0", Radix::Octal) == "X");
}

TEST_CASE("weak values count as 0 and 1") {
    REQUIRE(formatBits("HLHL", Radix::Hex) == "A");
    REQUIRE(formatBits("HL", Radix::Unsigned) == "2");
}

TEST_CASE("binary and default radix") {
    REQUIRE(formatBits("10XZ", Radix::Binary) == "10XZ");
    REQUIRE(formatBits("1", Radix::Default) == "1");
    REQUIRE(formatBits("00001111", Radix::Default) == "0F");
}

TEST_CASE("unsigned decimal") {
    REQUIRE(formatBits("0", Radix::Unsigned) == "0");
    REQUIRE(formatBits("11111111", Radix::Unsigned) == "255");
    REQUIRE(formatBits("1X", Radix::Unsigned) == "X");
    REQUIRE(formatBits("ZZ", Radix::Unsigned) == "Z");
    // 2^64 - 1 and 2^70 exceed nothing: arbitrary width is exact
    REQUIRE(formatBits(std::string(64, '1'), Radix::Unsigned) == "18446744073709551615");
    REQUIRE(formatBits("1" + std::string(70, '0'), Radix::Unsigned) == "1180591620717411303424");
}

TEST_CASE("signed decimal") {
    REQUIRE(formatBits("0111", Radix::Signed) == "7");
    REQUIRE(formatBits("1000", Radix::Signed) == "-8");
    REQUIRE(formatBits("1111", Radix::Signed) == "-1");
    REQUIRE(formatBits("1", Radix::Signed) == "-1");
    REQUIRE(formatBits("10000000000000000000000000000000", Radix::Signed) == "-2147483648");
    REQUIRE(formatBits("1" + std::string(64, '0'), Radix::Signed) == "-18446744073709551616");
    REQUIRE(formatBits("1X00", Radix::Signed) == "X");
}

TEST_CASE("ascii") {
    REQUIRE(formatBits("0100100001101001", Radix::Ascii) == "Hi");
    REQUIRE(formatBits("00000000", Radix::Ascii) == ".");
    REQUIRE(formatBits("01X00001", Radix::Ascii) == "?");
    REQUIRE(formatBits("101000001", Radix::Ascii) == ".A");  // partial leading byte 0x01
}

TEST_CASE("empty input and radix names") {
    REQUIRE(formatBits("", Radix::Hex).empty());
    Radix r{};
    REQUIRE(parseRadix("HEX", r));
    REQUIRE(r == Radix::Hex);
    REQUIRE(parseRadix("Decimal", r));
    REQUIRE(r == Radix::Unsigned);
    REQUIRE_FALSE(parseRadix("base7", r));
    REQUIRE(std::string(radixName(Radix::Signed)) == "signed");
}

TEST_CASE("formatValue for each signal kind") {
    WaveStore st;
    const SignalId v = st.addSignal(kRootScope, "v", SignalKind::Logic, 8);
    const SignalId r = st.addSignal(kRootScope, "r", SignalKind::Real, 64);
    const SignalId e = st.addSignal(kRootScope, "e", SignalKind::String, 0);
    st.mutableSignal(v).appendLogic(0, "10100101");
    st.mutableSignal(r).appendReal(0, 3.25);
    st.mutableSignal(e).appendText(0, "RUN");
    REQUIRE(formatValue(st.signal(v), 0, Radix::Hex) == "A5");
    REQUIRE(formatValue(st.signal(v), 0, Radix::Signed) == "-91");
    REQUIRE(formatValue(st.signal(r), 0, Radix::Hex) == "3.25");
    REQUIRE(formatValue(st.signal(e), 0, Radix::Hex) == "RUN");
}
