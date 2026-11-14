// SPDX-License-Identifier: GPL-3.0-or-later
#include <lvdwave/live_reader.hpp>
#include <lvdwave/memory.hpp>

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

using namespace lvd::wave;
namespace fs = std::filesystem;

namespace {

std::string bits8(unsigned v) {
    std::string s(8, '0');
    for (int b = 0; b < 8; ++b)
        s[7 - static_cast<std::size_t>(b)] = ((v >> b) & 1u) ? '1' : '0';
    return s;
}

// top.mem: array (0 to 3) of std_logic_vector(7 downto 0), written as NVC does: every element is repeated
// whenever one of them changes. Word 1 = 0x11 at 10, word 2 = 0x22 at 20, word 1 = 0x33 at 30, word 3
// rewritten with its old value at 40.
struct RamStore {
    WaveStore st;
    ScopeId top = st.addScope(kRootScope, "top");
    SignalId clk = st.addSignal(top, "clk", SignalKind::Logic, 1);
    SignalId vec = st.addSignal(top, "data[7:0]", SignalKind::Logic, 8);
    std::vector<SignalId> mem;
    std::vector<unsigned> values = {0, 0, 0, 0};

    RamStore() {
        for (int i = 0; i < 4; ++i)
            mem.push_back(st.addSignal(top, "mem[" + std::to_string(i) + "][7:0]", SignalKind::Logic, 8));
        dump(0);
        write(10, 1, 0x11);
        write(20, 2, 0x22);
        write(30, 1, 0x33);
        write(40, 3, 0);
    }
    void dump(Time t) {
        for (std::size_t i = 0; i < 4; ++i)
            st.mutableSignal(mem[i]).appendLogic(t, bits8(values[i]));
        st.extendTimeRange(t);
    }
    void write(Time t, std::size_t word, unsigned v) {
        values[word] = v;
        dump(t);
    }
};

} // namespace

TEST_CASE("element names are split into base, index and range") {
    std::string base, range;
    std::int64_t index = 0;
    REQUIRE(parseElementName("mem[12][7:0]", base, index, range));
    CHECK(base == "mem");
    CHECK(index == 12);
    CHECK(range == "[7:0]");
    REQUIRE(parseElementName("imem[-3]", base, index, range));
    CHECK(base == "imem");
    CHECK(index == -3);
    CHECK(range.empty());
    REQUIRE(parseElementName("m[0][1][3:0]", base, index, range));
    CHECK(base == "m[0]");
    CHECK(index == 1);
    CHECK_FALSE(parseElementName("data[7:0]", base, index, range));
    CHECK_FALSE(parseElementName("clk", base, index, range));
    CHECK_FALSE(parseElementName("[3]", base, index, range));
    CHECK_FALSE(parseElementName("a[x]", base, index, range));
}

TEST_CASE("memories are found in the element signals of a scope") {
    RamStore r;
    // an integer array, declared downto, and a lone element that is not a memory
    const SignalId i3 = r.st.addSignal(r.top, "imem[3]", SignalKind::Logic, 32);
    r.st.addSignal(r.top, "imem[2]", SignalKind::Logic, 32);
    r.st.addSignal(r.top, "imem[1]", SignalKind::Logic, 32);
    const SignalId i0 = r.st.addSignal(r.top, "imem[0]", SignalKind::Logic, 32);
    r.st.addSignal(r.top, "single[5]", SignalKind::Logic, 4);
    // mixed widths are not a memory
    r.st.addSignal(r.top, "odd[0]", SignalKind::Logic, 4);
    r.st.addSignal(r.top, "odd[1]", SignalKind::Logic, 5);

    const auto mems = findMemories(r.st);
    REQUIRE(mems.size() == 2);
    CHECK(mems[0].path == "top.imem");
    CHECK(mems[0].left == 3);
    CHECK(mems[0].right == 0);
    CHECK(mems[0].low == 0);
    CHECK(mems[0].high() == 3);
    CHECK(mems[0].width == 32);
    CHECK(mems[0].words.front() == i0);
    CHECK(mems[0].words.back() == i3);
    CHECK(mems[1].path == "top.mem");
    CHECK(mems[1].name == "mem");
    CHECK(mems[1].elementRange == "[7:0]");
    CHECK(mems[1].depth() == 4);
    CHECK(mems[1].width == 8);

    CHECK(findMemories(r.st, 1).size() == 3); // single[5] counts with minWords 1
    REQUIRE(findMemory(r.st, "/top/mem"));
    CHECK(findMemory(r.st, "TOP.MEM")->depth() == 4);
    CHECK_FALSE(findMemory(r.st, "top.data"));
}

TEST_CASE("sameValue compares stored values") {
    RamStore r;
    const Signal& m0 = r.st.signal(r.mem[0]);
    CHECK(m0.sameValue(0, 4));
    const Signal& m1 = r.st.signal(r.mem[1]);
    CHECK_FALSE(m1.sameValue(0, 1));
    CHECK(m1.sameValue(1, 2));
}

TEST_CASE("a memory image gives every word at any time and the writes") {
    RamStore r;
    MemoryImage img(r.st, *findMemory(r.st, "top.mem"));
    REQUIRE(img.depth() == 4);

    CHECK(img.bits(1, 0) == "00000000");
    CHECK(img.bits(1, 15) == "00010001");
    CHECK(img.format(1, 35, Radix::Hex) == "33");
    CHECK(img.format(2, 19, Radix::Hex) == "00");
    CHECK(img.format(2, 20, Radix::Unsigned) == "34");

    // repeated values are not writes; the initial value is not either
    REQUIRE(img.writes().size() == 3);
    CHECK(img.writes()[0].time == 10);
    CHECK(img.writes()[0].word == 1);
    CHECK(img.writes()[2].time == 30);
    CHECK(img.wordsWrittenAt(30) == std::vector<std::size_t>{1});
    CHECK(img.wordsWrittenAt(40).empty());
    CHECK(img.writeCount(1, 100) == 2);
    CHECK(img.writeCount(1, 29) == 1);
    CHECK(img.lastWrite(1, 25) == Time{10});
    CHECK_FALSE(img.lastWrite(3, 100));
    CHECK_FALSE(img.lastWrite(0, 5));

    CHECK(img.lastWriteTime(35) == Time{30});
    CHECK(img.lastWriteTime(30) == Time{30});
    CHECK_FALSE(img.lastWriteTime(9));
    CHECK(img.nextWriteTime(10) == Time{20});
    CHECK(img.previousWriteTime(20) == Time{10});
    CHECK_FALSE(img.previousWriteTime(10));
    CHECK_FALSE(img.nextWriteTime(30));
}

TEST_CASE("before its first value a word has none") {
    WaveStore st;
    const ScopeId s = st.addScope(kRootScope, "t");
    const SignalId a = st.addSignal(s, "m[0]", SignalKind::Logic, 4);
    const SignalId b = st.addSignal(s, "m[1]", SignalKind::Logic, 4);
    st.mutableSignal(a).appendLogic(5, "0001");
    st.mutableSignal(b).appendLogic(7, "0010");
    MemoryImage img(st, *findMemory(st, "t.m"));
    CHECK(img.bits(0, 4).empty());
    CHECK(img.format(0, 4, Radix::Hex) == "-");
    CHECK(img.format(0, 5, Radix::Hex) == "1");
    CHECK(img.changeAt(1, 6) == kNoIndex);
}

TEST_CASE("update reads only what a live source appended") {
    RamStore r;
    MemoryImage img(r.st, *findMemory(r.st, "top.mem"));
    REQUIRE(img.writes().size() == 3);
    CHECK_FALSE(img.update());

    r.write(50, 0, 0xAA);
    r.write(50, 3, 0xBB); // two writes in one time step
    r.write(60, 2, 0x22); // same value again: no write
    REQUIRE(img.update());
    REQUIRE(img.writes().size() == 5);
    CHECK(img.wordsWrittenAt(50) == std::vector<std::size_t>{0, 3});
    CHECK(img.format(3, 55, Radix::Hex) == "BB");
    CHECK(img.lastWriteTime(1000) == Time{50});
}

TEST_CASE("writes appended out of time order across words are merged") {
    WaveStore st;
    const ScopeId s = st.addScope(kRootScope, "t");
    const SignalId a = st.addSignal(s, "m[0]", SignalKind::Logic, 4);
    const SignalId b = st.addSignal(s, "m[1]", SignalKind::Logic, 4);
    st.mutableSignal(a).appendLogic(0, "0000");
    st.mutableSignal(b).appendLogic(0, "0000");
    st.mutableSignal(a).appendLogic(30, "0001");
    MemoryImage img(st, *findMemory(st, "t.m"));
    // a later poll brings a write of word 1 at the same time and one at an earlier time step
    st.mutableSignal(b).appendLogic(20, "0010");
    st.mutableSignal(b).appendLogic(30, "0011");
    REQUIRE(img.update());
    REQUIRE(img.writes().size() == 3);
    CHECK(img.writes()[0].time == 20);
    CHECK(img.writes()[1].time == 30);
    CHECK(img.writes()[1].word == 0);
    CHECK(img.writes()[2].word == 1);
}

TEST_CASE("findWord searches the words at a time, with and without wrap") {
    RamStore r;
    MemoryImage img(r.st, *findMemory(r.st, "top.mem"));
    const auto p = ValuePattern::parse("00", Radix::Hex, SignalKind::Logic, 8);
    REQUIRE(p.ok());
    // at 35: 00 33 22 00
    CHECK(img.findWord(p.value(), 35, std::nullopt, SearchDirection::Forward) == std::size_t{0});
    CHECK(img.findWord(p.value(), 35, 0, SearchDirection::Forward) == std::size_t{3});
    CHECK_FALSE(img.findWord(p.value(), 35, 3, SearchDirection::Forward));
    CHECK(img.findWord(p.value(), 35, 3, SearchDirection::Forward, true) == std::size_t{0});
    CHECK(img.findWord(p.value(), 35, std::nullopt, SearchDirection::Backward) == std::size_t{3});
    CHECK(img.findWord(p.value(), 35, 3, SearchDirection::Backward) == std::size_t{0});
    CHECK_FALSE(img.findWord(p.value(), 35, 0, SearchDirection::Backward));
    CHECK(img.findWord(p.value(), 35, 0, SearchDirection::Backward, true) == std::size_t{3});

    const auto q = ValuePattern::parse("33", Radix::Hex, SignalKind::Logic, 8);
    CHECK(img.findWord(q.value(), 35, std::nullopt, SearchDirection::Forward) == std::size_t{1});
    CHECK_FALSE(img.findWord(q.value(), 25, std::nullopt, SearchDirection::Forward)); // time travel
    // wrapping over a memory with a single match returns it from itself
    CHECK(img.findWord(q.value(), 35, 1, SearchDirection::Forward, true) == std::size_t{1});
}

TEST_CASE("memory contents are written as a $readmem file") {
    RamStore r;
    MemoryImage img(r.st, *findMemory(r.st, "top.mem"));
    const std::string hex = img.memoryFileText(35, Radix::Hex, 2, "35 ns");
    CHECK(hex == "// top.mem [0:3], 8 bit words, at 35 ns\n"
                 "// written by lvdsim; read it with $readmemh\n"
                 "@0\n"
                 "00 33\n"
                 "22 00\n");
    const std::string bin = img.memoryFileText(10, Radix::Binary, 4);
    CHECK(bin.find("@0\n00000000 00010001 00000000 00000000\n") != std::string::npos);

    WaveStore st;
    const ScopeId s = st.addScope(kRootScope, "t");
    st.mutableSignal(st.addSignal(s, "m[4]", SignalKind::Logic, 6)).appendLogic(0, "UUUUUU");
    st.mutableSignal(st.addSignal(s, "m[5]", SignalKind::Logic, 6)).appendLogic(0, "zzzz01");
    MemoryImage u(st, *findMemory(st, "t.m"));
    CHECK(u.memoryFileText(0, Radix::Hex).find("@4\nxx zx\n") != std::string::npos);
    CHECK(u.memoryFileText(0, Radix::Binary).find("@4\nxxxxxx zzzz01\n") != std::string::npos);
}

TEST_CASE("add mem options") {
    auto r = parseAddMem({"-addressradix", "dec", "-dataradix", "binary", "-wordsperline", "4", "/top/mem"});
    // "dec" is not a radix name of libwave; unsigned is
    CHECK_FALSE(r.ok());
    r = parseAddMem(
        {"-addressradix", "unsigned", "-dataradix", "binary", "-wordsperline", "4", "/top/mem", "/a/b"});
    REQUIRE(r.ok());
    CHECK(r.value().addressRadix == Radix::Unsigned);
    CHECK(r.value().dataRadix == Radix::Binary);
    CHECK(r.value().wordsPerLine == 4u);
    CHECK(r.value().paths == std::vector<std::string>{"/top/mem", "/a/b"});
    CHECK_FALSE(parseAddMem({"-wordsperline", "0", "/m"}).ok());
    CHECK_FALSE(parseAddMem({"-dataradix"}).ok());
    CHECK_FALSE(parseAddMem({"-bogus", "/m"}).ok());
    CHECK(parseAddMem({}).value().paths.empty());
}

TEST_CASE("addresses are formatted and parsed in a radix") {
    CHECK(formatAddress(255, Radix::Hex) == "FF");
    CHECK(formatAddress(5, Radix::Hex, 4) == "0005");
    CHECK(formatAddress(5, Radix::Binary, 4) == "0101");
    CHECK(formatAddress(8, Radix::Octal) == "10");
    CHECK(formatAddress(42, Radix::Unsigned, 6) == "42");
    CHECK(formatAddress(-3, Radix::Signed) == "-3");

    CHECK(parseAddress("ff", Radix::Hex) == 255);
    CHECK(parseAddress("10", Radix::Unsigned) == 10);
    CHECK(parseAddress("0x10", Radix::Unsigned) == 16);
    CHECK(parseAddress("16#1_0#", Radix::Unsigned) == 16);
    CHECK(parseAddress("x\"20\"", Radix::Unsigned) == 32);
    CHECK(parseAddress("0b101", Radix::Hex) == 5);
    CHECK(parseAddress("-4", Radix::Signed) == -4);
    CHECK_FALSE(parseAddress("zz", Radix::Hex));
    CHECK_FALSE(parseAddress("", Radix::Hex));
    CHECK_FALSE(parseAddress("3#1#", Radix::Hex));
}

TEST_CASE("a memory from the live stream as NVC writes it with --dump-arrays") {
    const fs::path path = fs::temp_directory_path() / "lvdwave_memory_live.lvw";
    fs::remove(path);
    {
        std::ofstream f(path, std::ios::binary);
        f << "LVW 1\nTS -15\nS 12 t\n";
        for (int i = 0; i < 4; ++i)
            f << "V " << i + 1 << " 23 8 L mem[" << i << "][7:0]\nD internal\n";
        f << "V 5 1 32 L imem[1]\nD internal\nV 6 1 32 L imem[0]\nD internal\n";
        f << "V 7 23 1 L clk\nD internal\nU\nE\n#0\n";
        for (int i = 1; i <= 4; ++i)
            f << i << " L 00000000\n";
        f << "#5000000\n7 L 1\n1 L 00000001\n2 L 00000000\n3 L 00000000\n4 L 00000000\n";
        f << "#15000000\n1 L 00000001\n2 L 00000100\n3 L 00000000\n4 L 00000000\n";
    }
    WaveStore st;
    LiveReader reader(path.string());
    REQUIRE(reader.poll(st).ok());
    const auto mems = findMemories(st);
    REQUIRE(mems.size() == 2);
    MemoryImage img(st, mems[1]);
    CHECK(img.info().path == "t.mem");
    CHECK(img.format(1, 20000000, Radix::Hex) == "04");
    CHECK(img.writes().size() == 2);
    CHECK(img.wordsWrittenAt(15000000) == std::vector<std::size_t>{1});
    fs::remove(path);
}
