// SPDX-License-Identifier: GPL-3.0-or-later
// The coverage export reader. data/coverage/cov_tb.xml is real output of
//   nvc -a cov_design.vhd -e --cover=all cov_tb -r
//   nvc --cover-export --format=xml --relative=<data/coverage> -o cov_tb.xml cov_tb.ncdb
// for which nvc's own report printed: statement 31/41, branch 19/30, toggle 38/76, expression 12/20,
// FSM state 4/8, average 104/175.
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <coverage.hpp>
#include <fstream>
#include <sstream>

using namespace lvd::tools::cov;
using Args = std::vector<std::string>;

namespace {

std::string readFixture(const char* name) {
    std::ifstream in(std::string(LVD_COVERAGE_DATA) + "/" + name, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

const Database& fixture() {
    static const ParseResult r = parseExportXml(readFixture("cov_tb.xml"));
    REQUIRE(r.ok());
    return r.db;
}

std::int64_t total(const Counts& c, Kind k) {
    return c.total[static_cast<int>(k)];
}
std::int64_t hit(const Counts& c, Kind k) {
    return c.hit[static_cast<int>(k)];
}

} // namespace

TEST_CASE("coverage export: totals agree with nvc's report") {
    const Database& db = fixture();
    REQUIRE_FALSE(db.empty());
    CHECK(db.library == "WORK");
    REQUIRE(db.files.size() == 1);
    CHECK(db.files[0] == "cov_design.vhd");

    const Counts c = db.totals();
    CHECK(hit(c, Kind::Statement) == 31);
    CHECK(total(c, Kind::Statement) == 41);
    CHECK(hit(c, Kind::Branch) == 19);
    CHECK(total(c, Kind::Branch) == 30);
    CHECK(hit(c, Kind::Toggle) == 38);
    CHECK(total(c, Kind::Toggle) == 76);
    CHECK(hit(c, Kind::Expression) == 12);
    CHECK(total(c, Kind::Expression) == 20);
    CHECK(hit(c, Kind::State) == 4);
    CHECK(total(c, Kind::State) == 8);
    CHECK_FALSE(c.has(Kind::Functional));
    CHECK(c.percent(Kind::Functional) < 0);
    CHECK(c.allHit() == 104);
    CHECK(c.allTotal() == 175);
    CHECK_THAT(c.overall(), Catch::Matchers::WithinAbs(59.43, 0.01));
}

TEST_CASE("coverage export: instance hierarchy") {
    const Database& db = fixture();
    const std::vector<int> tops = db.topInstances();
    REQUIRE(tops.size() == 1);
    const Scope& top = db.scopes[static_cast<std::size_t>(tops[0])];
    CHECK(top.name == "COV_TB");
    CHECK(top.blockName == "COV_TB-TEST");
    CHECK(top.line == 59);
    CHECK(displayPath(db, tops[0]) == "/cov_tb");

    const std::vector<int> kids = db.childInstances(tops[0]);
    REQUIRE(kids.size() == 2);
    CHECK(db.scopes[static_cast<std::size_t>(kids[0])].name == "U_BUSY");
    CHECK(db.scopes[static_cast<std::size_t>(kids[1])].name == "U_IDLE");
    CHECK(db.scopes[static_cast<std::size_t>(kids[1])].blockName == "COV_COUNTER-RTL");

    CHECK(db.findScope("/cov_tb/u_busy") == kids[0]);
    CHECK(db.findScope("WORK.COV_TB.U_IDLE") == kids[1]);
    CHECK(db.findScope("cov_tb.u_idle") == kids[1]);
    CHECK(db.findScope("/cov_tb/nothing") == -1);
    CHECK(displayPath(db, kids[1]) == "/cov_tb/u_idle");

    // The recursive counts of the top are the database totals; its own exclude both counters.
    const Counts all = db.counts(tops[0], true);
    CHECK(all.allTotal() == db.totals().allTotal());
    const Counts self = db.counts(tops[0], false);
    const Counts busy = db.counts(kids[0], false);
    const Counts idle = db.counts(kids[1], false);
    Counts sum = self;
    sum.add(busy);
    sum.add(idle);
    for (Kind k : kAllKinds) {
        CHECK(total(sum, k) == total(all, k));
        CHECK(hit(sum, k) == hit(all, k));
    }
    // The idle counter never counts: its counting statements are not reached.
    CHECK(busy.percent(Kind::Statement) > idle.percent(Kind::Statement));
    CHECK(hit(idle, Kind::State) == 1);
    CHECK(total(idle, Kind::State) == 4);
}

TEST_CASE("coverage export: lines of an instance") {
    const Database& db = fixture();
    const int busy = db.findScope("/cov_tb/u_busy");
    REQUIRE(busy >= 0);
    CHECK(db.mainFile(busy) == 0);
    const auto lines = lineCoverage(db, busy, 0);

    // when ERROR_STATE => state <= IDLE; is never reached
    REQUIRE(lines.count(41) == 1);
    CHECK(lines.at(41).uncovered());
    REQUIRE(lines.count(42) == 1);
    CHECK(lines.at(42).uncovered());
    CHECK(lines.at(42).hasStatement);
    CHECK(lines.at(42).hits == 0);
    CHECK(lines.at(42).missed[static_cast<int>(Kind::Statement)] == 1);

    // cnt <= cnt + 1 ran 18 times
    REQUIRE(lines.count(35) == 1);
    CHECK(lines.at(35).covered());
    CHECK(lines.at(35).hits == 18);

    // wrap <= ... : the statement runs, one expression bin (1_0) never happens
    REQUIRE(lines.count(48) == 1);
    const LineCoverage& wrap = lines.at(48);
    CHECK_FALSE(wrap.covered());
    CHECK_FALSE(wrap.uncovered());
    CHECK(wrap.missed[static_cast<int>(Kind::Expression)] == 1);

    // The FSM state variable: ERROR_STATE never entered
    REQUIRE(lines.count(19) == 1);
    CHECK(lines.at(19).missed[static_cast<int>(Kind::State)] == 1);

    // Nothing of the instance on lines of the test bench, and nothing for another file.
    CHECK(lines.count(78) == 0);
    CHECK(lineCoverage(db, busy, 5).empty());

    // The recursive view of the top includes the counter lines (both instances: 46 + 46 hits max 46).
    const auto topLines = lineCoverage(db, db.findScope("/cov_tb"), 0, true);
    CHECK(topLines.count(42) == 1);
    CHECK(topLines.count(78) == 1);
    CHECK(lineCoverage(db, db.findScope("/cov_tb"), 0, false).count(42) == 0);
}

TEST_CASE("coverage export: toggles per bit and FSM states") {
    const Database& db = fixture();
    const int busy = db.findScope("/cov_tb/u_busy");
    const std::vector<ToggleSignal> t = toggleSignals(db, busy);
    auto find = [&](const std::string& n) {
        return std::find_if(t.begin(), t.end(), [&](const ToggleSignal& s) { return s.name == n; });
    };
    const auto cnt = find("CNT");
    REQUIRE(cnt != t.end());
    REQUIRE(cnt->bits.size() == 4);
    CHECK(cnt->bits[0].index == "(3)");
    CHECK(cnt->bits[0].rise == 1);
    CHECK(cnt->bits[3].index == "(0)");
    CHECK(cnt->bits[3].rise == 9);
    CHECK(cnt->coveredBits() == 4);
    CHECK(cnt->line == 20);

    const auto en = find("EN");
    REQUIRE(en != t.end());
    REQUIRE(en->bits.size() == 1);
    CHECK(en->bits[0].index.empty());
    CHECK(en->bits[0].rise == 1);
    CHECK(en->bits[0].fall == 0);
    CHECK_FALSE(en->bits[0].covered());

    // In the idle instance the enable is tied to '0': nvc marks its toggles unreachable.
    const std::vector<ToggleSignal> idle = toggleSignals(db, db.findScope("/cov_tb/u_idle"));
    const auto ien =
        std::find_if(idle.begin(), idle.end(), [](const ToggleSignal& s) { return s.name == "EN"; });
    REQUIRE(ien != idle.end());
    CHECK(ien->bits[0].unreachable);

    const std::vector<StateVariable> st = stateVariables(db, busy);
    REQUIRE(st.size() == 1);
    CHECK(st[0].name == "STATE");
    REQUIRE(st[0].states.size() == 4);
    CHECK(st[0].states[0].first == "IDLE");
    CHECK(st[0].states[0].second == 2);
    CHECK(st[0].states[3].first == "ERROR_STATE");
    CHECK(st[0].states[3].second == 0);
}

TEST_CASE("coverage export: file and line are inherited from the parent scope") {
    const std::string xml = R"(<?xml version="1.0"?>
<!-- a comment -->
<scope name="LIB">
  <scope name="TOP" block_name="TOP-A" file="tb.vhd" line="3">
    <scope name="U1" block_name="SUB-RTL" line="9">
      <scope name="_P0" file="sub &amp; more.vhd" line="20">
        <scope name="_S0">
          <statement hier="LIB.TOP.U1._P0._S0" data="4"/>
        </scope>
        <scope name="_S1" line="22">
          <statement hier="LIB.TOP.U1._P0._S1" data="0"/>
          <future-element some="thing"/>
        </scope>
      </scope>
    </scope>
    <scope name="_P0" line="5">
      <statement hier="LIB.TOP._P0" data="1"/>
    </scope>
  </scope>
</scope>
)";
    const ParseResult r = parseExportXml(xml);
    REQUIRE(r.ok());
    const Database& db = r.db;
    REQUIRE(db.files.size() == 2);
    CHECK(db.files[1] == "sub & more.vhd");
    REQUIRE(db.bins.size() == 3);
    CHECK(db.bins[0].file == 1);
    CHECK(db.bins[0].line == 20);
    CHECK(db.bins[1].line == 22);
    CHECK(db.bins[2].file == 0);
    CHECK(db.bins[2].line == 5);
    const int u1 = db.findScope("/top/u1");
    REQUIRE(u1 >= 0);
    CHECK(db.scopes[static_cast<std::size_t>(u1)].file == 0); // the instantiation is in tb.vhd
    CHECK(db.mainFile(u1) == 1);
    CHECK(db.instanceBins(u1).size() == 2);
    const auto lines = lineCoverage(db, u1, 1);
    CHECK(lines.at(20).covered());
    CHECK(lines.at(22).uncovered());
}

TEST_CASE("coverage export: malformed input is an error") {
    CHECK_FALSE(parseExportXml("").ok());
    CHECK_FALSE(parseExportXml("hello").ok());
    CHECK_FALSE(parseExportXml(R"(<scope name="W"><scope name="T">)").ok());
    CHECK_FALSE(parseExportXml(R"(<scope name="W"><statement hier="W.X" data="many"/></scope>)").ok());
    CHECK_FALSE(parseExportXml(R"(<statement hier="W.X" data="1"/>)").ok());
    CHECK_FALSE(parseExportXml(R"(<scope name="W"></scope></scope>)").ok());
    CHECK_FALSE(parseExportXml(R"(<scope name="W" line=3></scope>)").ok());
    const std::string full = readFixture("cov_tb.xml");
    CHECK_FALSE(parseExportXml(std::string_view(full).substr(0, full.size() / 2)).ok());
    // An empty database (no design units with coverage) is not an error.
    const ParseResult empty = parseExportXml(R"(<?xml version="1.0"?><scope name="WORK"></scope>)");
    REQUIRE(empty.ok());
    CHECK(empty.db.empty());
    CHECK(empty.db.topInstances().empty());
}

TEST_CASE("coverage: elaboration options go right after -e") {
    Args a = {"-L", "lib", "--std=2008", "--work=work:work", "-e", "top", "-i", "--wave=x.fst"};
    REQUIRE(addElaborationOptions(a, {Kind::Statement, Kind::Toggle}, "/d/top.ncdb"));
    CHECK(a == Args{"-L", "lib", "--std=2008", "--work=work:work", "-e", "--cover=statement,toggle",
                    "--cover-file=/d/top.ncdb", "top", "-i", "--wave=x.fst"});
    // again: replaced, not added twice
    REQUIRE(addElaborationOptions(a, {}, "/d/other.ncdb"));
    CHECK(std::count_if(a.begin(), a.end(),
                        [](const std::string& s) { return s.rfind("--cover", 0) == 0; }) == 2);
    CHECK(a[5] == "--cover=statement,branch,expression,toggle,fsm-state,functional");
    CHECK(a[6] == "--cover-file=/d/other.ncdb");

    Args noElab = {"-a", "x.vhd"};
    CHECK_FALSE(addElaborationOptions(noElab, {}, "x.ncdb"));
    CHECK(noElab == Args{"-a", "x.vhd"});
    CHECK(elaborationOptions({Kind::State}, "") == Args{"--cover=fsm-state"});
}

TEST_CASE("coverage: kinds from words and letters") {
    CHECK(parseKinds("statement,branch") == std::vector<Kind>{Kind::Statement, Kind::Branch});
    CHECK(parseKinds("fsm-state, toggle") == std::vector<Kind>{Kind::Toggle, Kind::State});
    CHECK(parseKinds("sbceft") ==
          std::vector<Kind>{Kind::Statement, Kind::Branch, Kind::Expression, Kind::Toggle, Kind::State});
    CHECK(parseKinds("all").size() == static_cast<std::size_t>(kKindCount));
    CHECK(parseKinds("nonsense,xyz").empty());
    CHECK(parseKinds("").empty());
}

TEST_CASE("coverage: nvc calls for export, merge and report") {
    CHECK(exportArgs({"a.ncdb", "b.ncdb"}, "out.xml") ==
          Args{"--cover-export", "--format=xml", "-o", "out.xml", "a.ncdb", "b.ncdb"});
    CHECK(mergeArgs({"a.ncdb", "b.ncdb"}, "m.ncdb") ==
          Args{"--cover-merge", "-o", "m.ncdb", "a.ncdb", "b.ncdb"});
    CHECK(reportArgs({"a.ncdb"}, "html") == Args{"--cover-report", "-o", "html", "a.ncdb"});
    CHECK(displayName("U_BUSY") == "u_busy");
    CHECK(displayName("\\Odd Name\\") == "\\Odd Name\\");
}

// data/coverage/cov_tb.json is the JSON export of the same database as cov_tb.xml (the XML written by the
// same kernel build was byte-identical to cov_tb.xml):
//   nvc --cover-export --format=json --relative=<data/coverage> -o cov_tb.json cov_tb.ncdb
TEST_CASE("coverage export: the JSON export gives the database of the XML export") {
    const Database& x = fixture();
    const ParseResult r = parseExportJson(readFixture("cov_tb.json"));
    REQUIRE(r.ok());
    const Database& j = r.db;
    CHECK(j.library == x.library);
    CHECK(j.files == x.files);
    CHECK(j.mask != 0);
    REQUIRE(j.scopes.size() == x.scopes.size());
    for (std::size_t i = 0; i < x.scopes.size(); ++i) {
        INFO("scope " << i << " " << x.scopes[i].path);
        CHECK(j.scopes[i].name == x.scopes[i].name);
        CHECK(j.scopes[i].path == x.scopes[i].path);
        CHECK(j.scopes[i].blockName == x.scopes[i].blockName);
        CHECK(j.scopes[i].parent == x.scopes[i].parent);
        CHECK(j.scopes[i].instance == x.scopes[i].instance);
        CHECK(j.scopes[i].children == x.scopes[i].children);
        CHECK(j.scopes[i].bins == x.scopes[i].bins);
        CHECK(j.scopes[i].file == x.scopes[i].file);
    }
    REQUIRE(j.bins.size() == x.bins.size());
    int columns = 0;
    for (std::size_t i = 0; i < x.bins.size(); ++i) {
        INFO("bin " << x.bins[i].hier);
        CHECK(j.bins[i].kind == x.bins[i].kind);
        CHECK(j.bins[i].hier == x.bins[i].hier);
        CHECK(j.bins[i].label == x.bins[i].label);
        CHECK(j.bins[i].hits == x.bins[i].hits);
        CHECK(j.bins[i].unreachable == x.bins[i].unreachable);
        CHECK(j.bins[i].scope == x.bins[i].scope);
        CHECK(j.bins[i].threshold == 1);
        CHECK(j.bins[i].line > 0);
        columns += j.bins[i].column > 0 ? 1 : 0;
    }
    CHECK(columns == static_cast<int>(j.bins.size()));

    const Counts c = j.totals();
    CHECK(c.allHit() == 104);
    CHECK(c.allTotal() == 175);
    CHECK(hit(c, Kind::Toggle) == 38);
    CHECK(total(c, Kind::Toggle) == 76);
}

TEST_CASE("coverage export: parseExport reads either format") {
    const ParseResult x = parseExport(readFixture("cov_tb.xml"));
    REQUIRE(x.ok());
    CHECK(x.db.mask == 0);
    const ParseResult j = parseExport("\n  " + readFixture("cov_tb.json"));
    REQUIRE(j.ok());
    CHECK(j.db.mask != 0);
    CHECK(j.db.bins.size() == x.db.bins.size());
}

TEST_CASE("coverage export: JSON escapes, flags and thresholds") {
    // As the kernel writes extended identifiers: quotes and backslashes escaped, ISO-8859-1 as \u00XX
    const std::string json =
        R"({"v":1,"mask":7,"kinds":["statement","toggle"],"files":["a b.vhd"],"scopes":[)"
        R"({"name":"WORK","parent":-1,"file":-1,"line":0},)"
        R"({"name":"TOP","parent":0,"block":"TOP-TEST","file":0,"line":3},)"
        R"({"name":"\\y\"q\\","parent":1,"file":0,"line":5},)"
        R"({"name":"\\caf\u00e9\\","parent":1,"file":0,"line":6}],"bins":[)"
        R"({"scope":2,"kind":"toggle","hier":"WORK.TOP.\\y\"q\\.BIN_0_TO_1","file":0,"line":5,"column":12,)"
        R"("hits":0,"threshold":1,"flags":0,"excluded":false,"user_excluded":false,"unreachable":true},)"
        R"({"scope":1,"kind":"statement","hier":"WORK.TOP._P0._S0","file":0,"line":9,"column":4,)"
        R"("hits":2,"threshold":3,"flags":0,"excluded":true,"user_excluded":true,"unreachable":false},)"
        R"({"scope":1,"kind":"future","hier":"WORK.TOP.X","hits":1}]})";
    const ParseResult r = parseExportJson(json);
    REQUIRE(r.ok());
    const Database& db = r.db;
    REQUIRE(db.scopes.size() == 4);
    CHECK(db.scopes[2].name == "\\y\"q\\");
    CHECK(db.scopes[3].name == std::string("\\caf\xe9\\"));
    CHECK(db.scopes[2].path == "WORK.TOP.\\y\"q\\");
    CHECK(db.scopes[1].isInstance());
    CHECK(db.files[0] == "a b.vhd");
    REQUIRE(db.bins.size() == 2); // the unknown kind is skipped
    CHECK(db.bins[0].unreachable);
    CHECK_FALSE(db.bins[0].hit());
    CHECK(db.bins[0].label == "BIN_0_TO_1");
    CHECK(db.bins[0].column == 12);
    CHECK(db.bins[1].threshold == 3);
    CHECK_FALSE(db.bins[1].hit()); // 2 hits, 3 needed
    CHECK(db.bins[1].excluded);
    CHECK(db.bins[1].userExcluded);
    CHECK(db.bins[1].line == 9);
}

TEST_CASE("coverage export: malformed JSON is an error") {
    CHECK_FALSE(parseExportJson("").ok());
    CHECK_FALSE(parseExportJson("{").ok());
    CHECK_FALSE(parseExportJson("[]").ok());
    CHECK_FALSE(parseExportJson(R"({"v":2,"files":[],"scopes":[],"bins":[]})").ok());
    CHECK_FALSE(parseExportJson(R"({"v":1,"files":[],"bins":[]})").ok());
    CHECK_FALSE(parseExportJson(R"({"v":1,"files":[],"scopes":[],"bins":[]})").ok()); // no library scope
    CHECK_FALSE(parseExportJson(R"({"v":1,"files":[],"scopes":[{"name":"W","parent":0}],"bins":[]})").ok());
    CHECK_FALSE(parseExportJson(R"({"v":1,"files":[],"scopes":[{"name":"W","parent":-1}],"bins":[{"scope":1,)"
                                R"("kind":"statement","hier":"W.X","hits":1}]})")
                    .ok());
    CHECK_FALSE(
        parseExportJson(R"({"v":1,"files":[],"scopes":[{"name":"W","parent":-1}],"bins":[]} x)").ok());
    CHECK_FALSE(parseExportJson(R"({"v":1,"files":["\u12"],"scopes":[],"bins":[]})").ok());
    const ParseResult ok =
        parseExportJson(R"({"v":1,"files":[],"scopes":[{"name":"W","parent":-1}],"bins":[]})");
    REQUIRE(ok.ok());
    CHECK(ok.db.empty());
    CHECK(ok.db.library == "W");
}

TEST_CASE("coverage: nvc call for the JSON export") {
    CHECK(exportArgs({"a.ncdb"}, "out.json", ExportFormat::Json) ==
          Args{"--cover-export", "--format=json", "-o", "out.json", "a.ncdb"});
}

TEST_CASE("coverage export: excluded bins count as covered, as in nvc's report") {
    // mask 4 = toggle; 4 | 2048 adds exclude-unreachable, which excludes toggles that cannot happen.
    auto make = [](int mask) {
        return std::string(R"({"v":1,"mask":)") + std::to_string(mask) +
               R"(,"files":["a.vhd"],"scopes":[{"name":"WORK","parent":-1},)"
               R"({"name":"TOP","parent":0,"block":"TOP-A","file":0,"line":1},)"
               R"({"name":"S","parent":1,"file":0,"line":2},{"name":"T","parent":1,"file":0,"line":3}],"bins":[)"
               R"({"scope":2,"kind":"toggle","hier":"WORK.TOP.S.BIN_0_TO_1","line":2,"hits":0,"unreachable":true},)"
               R"({"scope":2,"kind":"toggle","hier":"WORK.TOP.S.BIN_1_TO_0","line":2,"hits":0,"unreachable":true},)"
               R"({"scope":3,"kind":"toggle","hier":"WORK.TOP.T.BIN_0_TO_1","line":3,"hits":0,"excluded":true},)"
               R"({"scope":3,"kind":"toggle","hier":"WORK.TOP.T.BIN_1_TO_0","line":3,"hits":4}]})";
    };
    {
        const ParseResult r = parseExportJson(make(4));
        REQUIRE(r.ok());
        CHECK(hit(r.db.totals(), Kind::Toggle) == 2); // the excluded rise of T and its fall
        CHECK_FALSE(r.db.bins[0].covered());
        CHECK(r.db.bins[2].covered());
        CHECK_FALSE(r.db.bins[2].hit());
        const auto t = toggleSignals(r.db, 1);
        REQUIRE(t.size() == 2);
        CHECK_FALSE(t[0].bits[0].covered()); // cannot toggle, not excluded
        CHECK(t[1].bits[0].covered());       // rise excluded, fall hit
        const auto lines = lineCoverage(r.db, 1, 0);
        CHECK(lines.at(3).covered());
        CHECK(lines.at(2).uncovered());
    }
    {
        const ParseResult r = parseExportJson(make(4 | 2048));
        REQUIRE(r.ok());
        CHECK(hit(r.db.totals(), Kind::Toggle) == 4);
        CHECK(r.db.bins[0].excluded);
        CHECK(toggleSignals(r.db, 1)[0].bits[0].covered());
        CHECK(lineCoverage(r.db, 1, 0).at(2).covered());
    }
}
