// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <set>
#include <translate.hpp>

using namespace lvd::tools;
using Args = std::vector<std::string>;

namespace {

Context makeCtx(std::set<std::string> dirs = {"work"}, std::map<std::string, std::string> files = {}) {
    Context c;
    c.dirExists = [dirs](const std::string& p) { return dirs.count(p) > 0; };
    c.readFile = [files](const std::string& p) -> std::optional<std::string> {
        auto it = files.find(p);
        if (it == files.end())
            return std::nullopt;
        return it->second;
    };
    c.compatTcl = "/x/lvdcompat.tcl";
    c.tempDir = "/tmp/t";
    return c;
}

} // namespace

// ---------------------------------------------------------------------------------------------
TEST_CASE("vlib creates directories and reports existing ones") {
    auto p = planVlib({"work", "libs"}, makeCtx({"libs"}));
    REQUIRE(p.ok());
    REQUIRE(p.directories == Args{"work"});
    REQUIRE(p.keepFiles.size() == 1);
    REQUIRE(p.keepFiles[0].path == "work/_NVC_LIB"); // marks the directory as a library for nvc
    REQUIRE(p.message.find("Creating library 'work'") != std::string::npos);
    REQUIRE(p.message.find("'libs' already exists") != std::string::npos);
    REQUIRE(p.calls.empty());

    REQUIRE_FALSE(planVlib({}, makeCtx()).ok());
    REQUIRE(planVlib({"-type", "directory", "x"}, makeCtx()).directories == Args{"x"});
}

TEST_CASE("vmap records, shows and lists mappings") {
    Context c = makeCtx();
    c.libs.set("unisim", "libs/unisim");

    auto set = planVmap({"mylib", "some/dir"}, c);
    REQUIRE(set.mappings.size() == 1);
    REQUIRE(set.mappings[0] == std::make_pair(std::string("mylib"), std::string("some/dir")));

    REQUIRE(planVmap({"unisim"}, c).message == "unisim = libs/unisim\n");
    REQUIRE_FALSE(planVmap({"nothere"}, c).ok());
    REQUIRE(planVmap({}, c).message == "unisim = libs/unisim\n");
    REQUIRE(planVmap({}, makeCtx()).message == "No library mappings.\n");
    REQUIRE_FALSE(planVmap({"a", "b", "c"}, c).ok());
}

// ---------------------------------------------------------------------------------------------
TEST_CASE("vcom translates to nvc analysis") {
    auto p = planVcom({"a.vhd", "b.vhd"}, makeCtx());
    REQUIRE(p.ok());
    REQUIRE(p.warnings.empty());
    REQUIRE(p.calls.size() == 1);
    REQUIRE(p.calls[0].args == Args{"--std=08", "--work=work:work", "-a", "a.vhd", "b.vhd"});
}

TEST_CASE("vcom puts the global nvc arguments first") {
    Context c = makeCtx();
    c.nvcGlobalArgs = {"-L", "/nvc/lib"};
    REQUIRE(planVcom({"a.vhd"}, c).calls[0].args ==
            Args{"-L", "/nvc/lib", "--std=08", "--work=work:work", "-a", "a.vhd"});
}

TEST_CASE("vcom standard selection") {
    auto std = [](const Args& a) { return planVcom(a, makeCtx()).calls[0].args[0]; };
    REQUIRE(std({"-93", "a.vhd"}) == "--std=93");
    REQUIRE(std({"-2002", "a.vhd"}) == "--std=02");
    REQUIRE(std({"-2008", "a.vhd"}) == "--std=08");
    REQUIRE(std({"-2019", "a.vhd"}) == "--std=19");
    REQUIRE(std({"-2008", "-93", "a.vhd"}) == "--std=93"); // the last one wins

    auto p = planVcom({"-87", "a.vhd"}, makeCtx());
    REQUIRE(p.calls[0].args[0] == "--std=93");
    REQUIRE(p.warnings.size() == 1);

    Context c = makeCtx();
    c.defaultStd = "02";
    REQUIRE(planVcom({"a.vhd"}, c).calls[0].args[0] == "--std=02");
}

TEST_CASE("vcom libraries: -work, -work=, mappings and -L") {
    SECTION("directory named like the library") {
        auto p = planVcom({"-work", "mylib", "a.vhd"}, makeCtx({"mylib"}));
        REQUIRE(p.calls[0].args[1] == "--work=mylib:mylib");
    }
    SECTION("inline form") {
        auto p = planVcom({"-work=mylib", "a.vhd"}, makeCtx({"mylib"}));
        REQUIRE(p.ok());
        REQUIRE(p.calls[0].args[1] == "--work=mylib:mylib");
    }
    SECTION("lvdsim.ini mapping") {
        Context c = makeCtx({});
        c.libs.set("mylib", "libs/mylib");
        REQUIRE(planVcom({"-work", "mylib", "a.vhd"}, c).calls[0].args[1] == "--work=mylib:libs/mylib");
    }
    SECTION("-L adds mapped libraries and skips unknown ones and the work library") {
        Context c = makeCtx();
        c.libs.set("unisim", "/u/unisim");
        auto p = planVcom({"-L", "unisim", "-L", "nothere", "-L", "work", "a.vhd"}, c);
        REQUIRE(p.ok());
        REQUIRE(p.calls[0].args ==
                Args{"--std=08", "--work=work:work", "--map=unisim:/u/unisim", "-a", "a.vhd"});
    }
    SECTION("a missing work library is reported with the fix") {
        auto p = planVcom({"a.vhd"}, makeCtx({}));
        REQUIRE_FALSE(p.ok());
        REQUIRE(p.error.find("vlib work") != std::string::npos);
        REQUIRE(p.calls.empty());
    }
}

TEST_CASE("vcom arguments and unsupported options") {
    REQUIRE_FALSE(planVcom({}, makeCtx()).ok());
    REQUIRE_FALSE(planVcom({"-2008"}, makeCtx()).ok()); // no files
    REQUIRE_FALSE(planVcom({"-work"}, makeCtx()).ok()); // missing argument

    SECTION("known options without effect are silent") {
        auto p = planVcom({"-quiet", "-novopt", "-O0", "-suppress", "1234", "-check_synthesis", "a.vhd"},
                          makeCtx());
        REQUIRE(p.ok());
        REQUIRE(p.warnings.empty());
        REQUIRE(p.calls[0].args.back() == "a.vhd"); // -suppress consumed its argument
        REQUIRE(p.calls[0].args.size() == 4);
    }
    SECTION("-explicit relaxes operator resolution") {
        auto p = planVcom({"-explicit", "a.vhd"}, makeCtx());
        REQUIRE(p.ok());
        REQUIRE(p.warnings.empty());
        const auto& a = p.calls[0].args;
        REQUIRE(std::find(a.begin(), a.end(), "--relaxed") != a.end());
        REQUIRE(a.back() == "a.vhd");
    }
    SECTION("unknown options warn and are skipped") {
        auto p = planVcom({"-frobnicate", "a.vhd"}, makeCtx());
        REQUIRE(p.ok());
        REQUIRE(p.warnings.size() == 1);
        REQUIRE(p.warnings[0].find("-frobnicate") != std::string::npos);
    }
    SECTION("strict mode turns them into errors, by flag or by context") {
        REQUIRE_FALSE(planVcom({"-strict", "-frobnicate", "a.vhd"}, makeCtx()).ok());
        Context c = makeCtx();
        c.strict = true;
        REQUIRE_FALSE(planVcom({"-frobnicate", "a.vhd"}, c).ok());
        REQUIRE(planVcom({"-strict", "a.vhd"}, makeCtx()).ok()); // the flag itself is not passed on
    }
}

TEST_CASE("argument files given with -f are expanded") {
    auto files = std::map<std::string, std::string>{
        {"list.f", "a.vhd\n// comment\n# another\n\"dir with space/b.vhd\"\n-f more.f\n"},
        {"more.f", "-2002 c.vhd"},
    };
    auto p = planVcom({"-f", "list.f", "d.vhd"}, makeCtx({"work"}, files));
    REQUIRE(p.ok());
    REQUIRE(p.calls[0].args ==
            Args{"--std=02", "--work=work:work", "-a", "a.vhd", "dir with space/b.vhd", "c.vhd", "d.vhd"});

    auto missing = planVcom({"-f", "nope.f"}, makeCtx());
    REQUIRE_FALSE(missing.ok());
    REQUIRE(missing.error.find("nope.f") != std::string::npos);

    std::string err;
    REQUIRE(expandArgFiles({"x", "-f", "list.f"}, makeCtx({}, files).readFile, &err).size() == 5);

    auto loop = std::map<std::string, std::string>{{"a.f", "-f a.f"}};
    REQUIRE_FALSE(planVcom({"-f", "a.f"}, makeCtx({"work"}, loop)).ok()); // recursion is cut off
}

// ---------------------------------------------------------------------------------------------
TEST_CASE("vlog translates defines, include directories and -sv") {
    auto p = planVlog({"-sv", "+define+A=1+B", "+incdir+inc1+inc2", "x.sv", "y.v"}, makeCtx());
    REQUIRE(p.ok());
    REQUIRE(p.warnings.empty());
    REQUIRE(p.calls[0].args == Args{"--work=work:work", "-a", "--keywords=1800-2017", "-D", "A=1", "-D", "B",
                                    "-I", "inc1", "-I", "inc2", "x.sv", "y.v"});

    auto plain = planVlog({"x.v"}, makeCtx());
    REQUIRE(plain.calls[0].args == Args{"--work=work:work", "-a", "x.v"});
    REQUIRE_FALSE(planVlog({}, makeCtx()).ok());

    auto ign = planVlog({"+acc", "-timescale", "1ns/1ps", "-quiet", "x.v"}, makeCtx());
    REQUIRE(ign.warnings.empty());
    REQUIRE(ign.calls[0].args.back() == "x.v");
}

// ---------------------------------------------------------------------------------------------
TEST_CASE("design unit names") {
    auto a = parseDesignUnit("work.top");
    REQUIRE(a.library == "work");
    REQUIRE(a.unit == "top");
    REQUIRE(a.architecture.empty());
    auto b = parseDesignUnit("top");
    REQUIRE(b.library.empty());
    REQUIRE(b.unit == "top");
    auto c = parseDesignUnit("lib.top(rtl)");
    REQUIRE(c.library == "lib");
    REQUIRE(c.unit == "top");
    REQUIRE(c.architecture == "rtl");
}

TEST_CASE("vsim batch run with a command string") {
    auto p = planVsim({"-c", "-do", "run 100 ns; quit -f", "work.top"}, makeCtx());
    REQUIRE(p.ok());
    REQUIRE(p.warnings.empty());
    REQUIRE(p.calls.size() == 1);
    REQUIRE(p.calls[0].args == Args{"--std=08", "--work=work:work", "-e", "top", "--do", "/x/lvdcompat.tcl",
                                    "/tmp/t/lvdsim_vsim_do.tcl"});
    REQUIRE(p.files.size() == 1);
    REQUIRE(p.files[0].path == "/tmp/t/lvdsim_vsim_do.tcl");
    REQUIRE(p.files[0].contents == "run 100 ns; quit -f\n");
}

TEST_CASE("vsim uses an existing script file directly") {
    auto p = planVsim({"-c", "-do", "run.do", "top"}, makeCtx({"work"}, {{"run.do", "run -all\n"}}));
    REQUIRE(p.ok());
    REQUIRE(p.files.empty());
    REQUIRE(p.calls[0].args.back() == "run.do");
}

TEST_CASE("vsim without -do starts the interactive shell") {
    auto p = planVsim({"-c", "top"}, makeCtx());
    REQUIRE(p.calls[0].args == Args{"--std=08", "--work=work:work", "-e", "top", "-i"});
    REQUIRE(planVsim({"-batch", "top"}, makeCtx()).ok());
}

TEST_CASE("vsim generics, libraries and waveform files") {
    Context c = makeCtx({"work", "other"});
    c.libs.set("unisim", "/u/unisim");

    auto p = planVsim({"-c", "-g", "WIDTH=8", "-G", "MODE=fast", "-L", "unisim", "-lib", "other", "top"}, c);
    REQUIRE(p.ok());
    REQUIRE(p.calls[0].args == Args{"--std=08", "--work=other:other", "--map=unisim:/u/unisim", "-e",
                                    "--generic-default=*.WIDTH=8", "-g*.MODE=fast", "top", "-i"});

    auto lib = planVsim({"-c", "other.top"}, c); // library prefix on the unit wins over -lib
    REQUIRE(lib.calls[0].args[1] == "--work=other:other");

    auto wlf = planVsim({"-c", "-do", "run 1 ns", "-wlf", "out.wlf", "top"}, c);
    REQUIRE(wlf.calls[0].args[5] == "--wave=out.fst");
    auto raw = planVsim({"-c", "-do", "run 1 ns", "-wlf", "out", "top"}, c);
    REQUIRE(raw.calls[0].args[5] == "--wave=out.fst");
    auto own = planVsim({"-c", "-do", "run 1 ns", "-lvdwave", "x.fst", "top"}, c);
    REQUIRE(own.calls[0].args[5] == "--wave=x.fst");
}

TEST_CASE("vsim generic overrides joined to the option") {
    Context c = makeCtx();
    auto e = [&](Args a) {
        a.insert(a.begin(), "-c");
        a.push_back("tb");
        return planVsim(a, c);
    };
    auto generics = [](const Plan& p) {
        Args g;
        for (const std::string& a : p.calls.at(0).args)
            if (a.rfind("-g", 0) == 0 || a.rfind("--generic-default", 0) == 0)
                g.push_back(a);
        return g;
    };

    SECTION("-g and -G with the value attached are passed to the elaboration") {
        auto p = e({"-gBUG=1", "-GMODE=fast"});
        REQUIRE(p.ok());
        REQUIRE(p.warnings.empty()); // not an unsupported option any more
        REQUIRE(p.calls[0].args == Args{"--std=08", "--work=work:work", "-e", "--generic-default=*.BUG=1",
                                        "-g*.MODE=fast", "tb", "-i"});
    }
    SECTION("-gui is still the GUI option, not a generic") {
        auto p = planVsim({"-gui", "tb"}, c);
        REQUIRE(p.ok());
        REQUIRE(p.gui);
    }
    SECTION("hierarchical names become dotted instance paths below the top level") {
        REQUIRE(generics(e({"-g/tb/u/WIDTH=8"})) == Args{"-gu.WIDTH=8"});
        REQUIRE(generics(e({"-G/TB/u/sub/WIDTH=8"})) == Args{"-gu.sub.WIDTH=8"}); // top matched without case
        REQUIRE(generics(e({"-gu/WIDTH=8"})) == Args{"-gu.WIDTH=8"});             // relative to the top
        REQUIRE(generics(e({"-g/tb/BUG=2"})) == Args{"-gBUG=2"});                 // the top level itself
        REQUIRE(generics(e({"-gu.WIDTH=8"})) == Args{"-gu.WIDTH=8"});             // nvc form is kept
    }
    SECTION("a path that does not start at the top level is an error") {
        auto p = e({"-g/other/u/WIDTH=8"});
        REQUIRE_FALSE(p.ok());
        REQUIRE(p.error.find("does not start at a top-level unit") != std::string::npos);
    }
    SECTION("a path rewritten by an MSYS shell is explained") {
        auto p = e({"-gC:/Program Files/Git/tb/u/WIDTH=8"});
        REQUIRE_FALSE(p.ok());
        REQUIRE(p.error.find("MSYS_NO_PATHCONV") != std::string::npos);
    }
    SECTION("paths through generate statements name the generate iteration") {
        auto p = e({"-g/tb/gen(1)/u/WIDTH=8"});
        REQUIRE(p.ok());
        REQUIRE(generics(p) == Args{"-ggen(1).u.WIDTH=8"});
    }
    SECTION("malformed overrides are errors") {
        REQUIRE_FALSE(e({"-gBUG"}).ok());
        REQUIRE_FALSE(e({"-gBUG="}).ok());
        REQUIRE_FALSE(e({"-G=1"}).ok());
        REQUIRE(e({"-gBUG"}).error.find("-g<name>=<value>") != std::string::npos);
    }
    SECTION("values: integers, quoted strings and based literals") {
        REQUIRE(generics(e({"-gN=-42"})) == Args{"--generic-default=*.N=-42"});
        REQUIRE(generics(e({"-gR=2.5"})) == Args{"--generic-default=*.R=2.5"});
        REQUIRE(generics(e({"-gMSG=\"hello world\""})) == Args{"--generic-default=*.MSG=hello world"});
        REQUIRE(generics(e({"-gMSG=\"say \"\"hi\"\"\""})) == Args{"--generic-default=*.MSG=say \"hi\""});
        REQUIRE(generics(e({"-gMSG=plain"})) == Args{"--generic-default=*.MSG=plain"});
        REQUIRE(generics(e({"-gV=x\"A5\""})) ==
                Args{"--generic-default=*.V=x\"A5\""}); // nvc reads hex bit strings itself
        REQUIRE(generics(e({"-gN=16#FF#"})) == Args{"--generic-default=*.N=255"});
        REQUIRE(generics(e({"-gN=2#1010_0101#"})) == Args{"--generic-default=*.N=165"});
        REQUIRE(generics(e({"-gN=-8#17#"})) == Args{"--generic-default=*.N=-15"});
        REQUIRE(generics(e({"-gN=2#102#"})) ==
                Args{"--generic-default=*.N=2#102#"}); // not a valid literal: nvc reports it
        REQUIRE(generics(e({"-gN=17#1#"})) == Args{"--generic-default=*.N=17#1#"});
        REQUIRE(generics(e({"-gB=true"})) == Args{"--generic-default=*.B=true"});
    }
    SECTION("the same generic twice: -G wins over -g, otherwise the last one") {
        REQUIRE(generics(e({"-GN=1", "-gN=2"})) == Args{"-g*.N=1"});
        REQUIRE(generics(e({"-gN=2", "-GN=1"})) == Args{"-g*.N=1"});
        auto twice = e({"-gN=1", "-gn=3"});
        REQUIRE(generics(twice) == Args{"--generic-default=*.n=3"});
        REQUIRE(twice.warnings.size() == 1);
        REQUIRE(generics(e({"-g/tb/u/W=1", "-gu.W=2"})) == Args{"-gu.W=2"});
    }
    SECTION("the separated form still works") {
        REQUIRE(generics(e({"-g", "N=3", "-G", "/tb/u/W=4"})) == Args{"--generic-default=*.N=3", "-gu.W=4"});
    }
    SECTION("the GUI plan carries the same overrides") {
        Context g = makeCtx();
        g.guiWaveFile = "live.lvw";
        auto p = planVsim({"-gBUG=1", "-g/tb/u/WIDTH=8", "tb"}, g);
        REQUIRE(p.ok());
        REQUIRE(p.gui);
        REQUIRE(generics(p) == Args{"--generic-default=*.BUG=1", "-gu.WIDTH=8"});
        auto cli = planVsim({"-gBUG=1", "tb"}, c); // the command line tool hands them on unchanged
        REQUIRE(cli.guiArgs == Args{"-gBUG=1", "tb"});
    }
}

TEST_CASE("vsim elaborates several design units together") {
    Context c = makeCtx({"work", "other"});
    auto p = planVsim({"-c", "work.top", "work.glbl"}, c);
    REQUIRE(p.ok());
    REQUIRE(p.warnings.empty());
    // The order is kept: the first unit is the root of the signal paths
    REQUIRE(p.calls[0].args == Args{"--std=08", "--work=work:work", "-e", "top", "glbl", "-i"});

    auto order = planVsim({"-c", "a", "b", "GLBL"}, c);
    REQUIRE(order.ok());
    REQUIRE(order.calls[0].args == Args{"--std=08", "--work=work:work", "-e", "a", "b", "GLBL", "-i"});

    // The library prefix is optional on later units and generics still come before the unit names.
    auto g = planVsim({"-c", "-g", "N=3", "-lib", "other", "top", "other.helper"}, c);
    REQUIRE(g.ok());
    REQUIRE(g.calls[0].args ==
            Args{"--std=08", "--work=other:other", "-e", "--generic-default=*.N=3", "top", "helper", "-i"});

    auto bad2 = planVsim({"-c", "top", "other.glbl"}, c); // the first unit comes from the default library
    REQUIRE_FALSE(bad2.ok());
    REQUIRE(bad2.error.find("different libraries") != std::string::npos);

    auto bad = planVsim({"-c", "work.top", "other.glbl"}, c);
    REQUIRE_FALSE(bad.ok());
    REQUIRE(bad.error.find("different libraries") != std::string::npos);
}

TEST_CASE("vsim ignores performance and debug options without eating the design name") {
    auto p = planVsim({"-c", "-voptargs=+acc", "-t", "ps", "-quiet", "-novopt", "-sv_seed", "5", "-msgmode",
                       "both", "-assertdebug", "top"},
                      makeCtx());
    REQUIRE(p.ok());
    REQUIRE(p.warnings.empty());
    REQUIRE(p.calls[0].args[3] == "top");
}

TEST_CASE("vsim reports what it cannot do") {
    REQUIRE_FALSE(planVsim({"-c"}, makeCtx()).ok());          // no design
    REQUIRE_FALSE(planVsim({"-c", "-do"}, makeCtx()).ok());   // missing argument
    REQUIRE_FALSE(planVsim({"-c", "top"}, makeCtx({})).ok()); // no library

    auto pli = planVsim({"-c", "-pli", "x.dll", "top"}, makeCtx());
    REQUIRE(pli.ok());
    REQUIRE(pli.warnings.size() == 1);
    REQUIRE(pli.calls[0].args[3] == "top"); // the PLI argument was not mistaken for the design

    REQUIRE_FALSE(planVsim({"-strict", "-c", "-pli", "x", "top"}, makeCtx()).ok());

    auto arch = planVsim({"-c", "work.top(rtl)"}, makeCtx());
    REQUIRE(arch.ok());
    REQUIRE(arch.warnings.size() == 1); // architecture ignored

    Context noTcl = makeCtx();
    noTcl.compatTcl.clear();
    auto w = planVsim({"-c", "-do", "run 1 ns", "top"}, noTcl);
    REQUIRE(w.ok());
    REQUIRE(w.warnings.size() == 1);
    REQUIRE(w.calls[0].args.size() == 6); // no compat script argument
}

TEST_CASE("vsim SDF options become nvc elaboration options") {
    auto p = planVsim({"-c", "-sdfmax", "/tb/dut=net.sdf", "-sdfmin", "u2=b.sdo", "-sdftyp", "c.sdf", "tb"},
                      makeCtx());
    REQUIRE(p.ok());
    REQUIRE(p.warnings.empty());
    const auto& a = p.calls[0].args;
    const auto e = std::find(a.begin(), a.end(), std::string("-e"));
    REQUIRE(e != a.end());
    REQUIRE(a.end() - e >= 5);
    REQUIRE(*(e + 1) == "--sdf=max=/tb/dut=net.sdf");
    REQUIRE(*(e + 2) == "--sdf=min=u2=b.sdo");
    REQUIRE(*(e + 3) == "--sdf=typ=c.sdf");
    REQUIRE(*(e + 4) == "tb");

    // Strict mode accepts them as well
    REQUIRE(planVsim({"-strict", "-c", "-sdfmax", "x.sdf", "tb"}, makeCtx()).ok());
}

TEST_CASE("vsim without -c is a GUI session") {
    SECTION("the command line tool only validates and passes the arguments on") {
        auto p = planVsim({"-do", "run 10 ns", "-L", "unisim", "work.top"}, makeCtx());
        REQUIRE(p.ok());
        REQUIRE(p.gui);
        REQUIRE(p.calls.empty());
        REQUIRE(p.files.empty());
        REQUIRE(p.guiArgs == Args{"-do", "run 10 ns", "-L", "unisim", "work.top"});
    }
    SECTION("an invalid command line is still an error before the GUI starts") {
        REQUIRE_FALSE(planVsim({}, makeCtx()).ok());
        REQUIRE_FALSE(planVsim({"nolib.top"}, makeCtx({})).ok());
    }
    SECTION("the GUI gets an interactive shell writing a live stream and its startup scripts") {
        Context c = makeCtx();
        c.guiWaveFile = "/tmp/s/live.lvw";
        auto p = planVsim({"-do", "run 10 ns", "-g", "N=4", "top"}, c);
        REQUIRE(p.ok());
        REQUIRE(p.gui);
        REQUIRE(p.calls.size() == 1);
        REQUIRE(p.calls[0].args == Args{"--std=08", "--work=work:work", "-e", "--generic-default=*.N=4",
                                        "top", "-i", "--format=live", "--wave=/tmp/s/live.lvw"});
        REQUIRE(p.startupScripts == Args{"/x/lvdcompat.tcl", "/tmp/t/lvdsim_vsim_do.tcl"});
        REQUIRE(p.files.size() == 1); // the GUI writes the temporary script
    }
    SECTION("without -do only the compatibility script is sourced") {
        Context c = makeCtx();
        c.guiWaveFile = "live.lvw";
        auto p = planVsim({"top"}, c);
        REQUIRE(p.startupScripts == Args{"/x/lvdcompat.tcl"});
    }
    SECTION("waveform file options are ignored in GUI mode") {
        Context c = makeCtx();
        c.guiWaveFile = "live.lvw";
        auto p = planVsim({"-wlf", "x.wlf", "top"}, c);
        REQUIRE(p.warnings.size() == 1);
        REQUIRE(p.calls[0].args.back() == "--wave=live.lvw");
    }
}

TEST_CASE("the lvdlive option writes the live stream in batch mode") {
    auto p = planVsim({"-c", "-lvdlive", "run.lvw", "-do", "run 1 ns", "top"}, makeCtx());
    REQUIRE(p.ok());
    REQUIRE(p.calls[0].args == Args{"--std=08", "--work=work:work", "-e", "top", "--do", "--format=live",
                                    "--wave=run.lvw", "/x/lvdcompat.tcl", "/tmp/t/lvdsim_vsim_do.tcl"});
}

TEST_CASE("the lvdarrays option puts memories into the waveform") {
    auto p = planVsim({"-c", "-lvdlive", "run.lvw", "-lvdarrays", "-do", "run 1 ns", "top"}, makeCtx());
    REQUIRE(p.ok());
    REQUIRE(p.calls[0].args == Args{"--std=08", "--work=work:work", "-e", "top", "--do", "--dump-arrays",
                                    "--format=live", "--wave=run.lvw", "/x/lvdcompat.tcl",
                                    "/tmp/t/lvdsim_vsim_do.tcl"});
    REQUIRE(p.warnings.empty());

    SECTION("in GUI mode it goes with the live stream") {
        Context c = makeCtx();
        c.guiWaveFile = "live.lvw";
        auto g = planVsim({"-lvdarrays", "top"}, c);
        REQUIRE(g.ok());
        REQUIRE(g.calls[0].args == Args{"--std=08", "--work=work:work", "-e", "top", "-i", "--dump-arrays",
                                        "--format=live", "--wave=live.lvw"});
    }
    SECTION("without a waveform it only warns") {
        auto b = planVsim({"-c", "-lvdarrays", "top"}, makeCtx());
        REQUIRE(b.ok());
        REQUIRE(b.warnings.size() == 1);
        for (const std::string& a : b.calls[0].args)
            REQUIRE(a != "--dump-arrays");
    }
}

// ---------------------------------------------------------------------------------------------
TEST_CASE("vsim coverage options enable collection at the elaboration") {
    const std::string all = "--cover=statement,branch,expression,toggle,fsm-state,functional";
    SECTION("-coverage collects every kind into <top>.ncdb") {
        auto p = planVsim({"-c", "-coverage", "-do", "run -all", "work.Top_TB"}, makeCtx());
        REQUIRE(p.ok());
        REQUIRE(p.warnings.empty());
        REQUIRE(p.coverageFile == "top_tb.ncdb");
        REQUIRE(p.calls[0].args == Args{"--std=08", "--work=work:work", "-e", all, "--cover-file=top_tb.ncdb",
                                        "Top_TB", "--do", "/x/lvdcompat.tcl", "/tmp/t/lvdsim_vsim_do.tcl"});
    }
    SECTION("-lvdcover selects kinds by word or letter, -lvdcoverfile names the database") {
        auto p = planVsim(
            {"-c", "-coverage", "-lvdcover", "sbt", "-lvdcoverfile", "out/run1.ncdb", "-gN=1", "top"},
            makeCtx());
        REQUIRE(p.ok());
        REQUIRE(p.coverageFile == "out/run1.ncdb");
        REQUIRE(p.calls[0].args == Args{"--std=08", "--work=work:work", "-e",
                                        "--cover=statement,branch,toggle", "--cover-file=out/run1.ncdb",
                                        "--generic-default=*.N=1", "top", "-i"});
        auto w = planVsim({"-c", "-coverage", "-lvdcover=fsm,expression", "top"}, makeCtx());
        REQUIRE(w.calls[0].args[3] == "--cover=expression,fsm-state");
        auto ce = planVsim({"-c", "-coverage", "-lvdcover", "cf", "top"}, makeCtx());
        REQUIRE(ce.calls[0].args[3] == "--cover=expression,fsm-state");
    }
    SECTION("kinds that name nothing warn and collect everything") {
        auto p = planVsim({"-c", "-coverage", "-lvdcover", "zzz", "top"}, makeCtx());
        REQUIRE(p.ok());
        REQUIRE(p.warnings.size() == 1);
        REQUIRE(p.calls[0].args[3] == all);
    }
    SECTION("without -coverage nothing is collected and the lvd options only warn") {
        auto p = planVsim({"-c", "-lvdcover", "s", "-lvdcoverfile", "x.ncdb", "top"}, makeCtx());
        REQUIRE(p.ok());
        REQUIRE(p.coverageFile.empty());
        REQUIRE(p.warnings.size() == 1);
        REQUIRE(p.calls[0].args == Args{"--std=08", "--work=work:work", "-e", "top", "-i"});
    }
    SECTION("the GUI plan gets the same options") {
        Context c = makeCtx();
        c.guiWaveFile = "live.lvw";
        auto p = planVsim({"-coverage", "top", "glbl"}, c);
        REQUIRE(p.ok());
        REQUIRE(p.coverageFile == "top.ncdb");
        REQUIRE(p.calls[0].args == Args{"--std=08", "--work=work:work", "-e", all, "--cover-file=top.ncdb",
                                        "top", "glbl", "-i", "--format=live", "--wave=live.lvw"});
        auto cli =
            planVsim({"-coverage", "-lvdcover", "s", "top"}, makeCtx()); // handed on to the GUI unchanged
        REQUIRE(cli.guiArgs == Args{"-coverage", "-lvdcover", "s", "top"});
    }
    SECTION("a missing argument is an error") {
        REQUIRE_FALSE(planVsim({"-c", "-coverage", "top", "-lvdcover"}, makeCtx()).ok());
    }
}

TEST_CASE("vcom and vlog accept +cover with a note") {
    auto p = planVcom({"+cover", "a.vhd"}, makeCtx());
    REQUIRE(p.ok());
    REQUIRE(p.warnings.empty());
    REQUIRE(p.calls[0].args == Args{"--std=08", "--work=work:work", "-a", "a.vhd"}); // nothing reaches nvc
    REQUIRE(p.message.find("elaborated") != std::string::npos);
    REQUIRE(p.message.find("vsim -coverage") != std::string::npos);

    auto letters = planVcom({"+cover=sbceft", "a.vhd"}, makeCtx());
    REQUIRE(letters.ok());
    REQUIRE(letters.warnings.empty());
    REQUIRE(letters.message.find("-lvdcover sbceft") != std::string::npos);

    auto v = planVlog({"+cover=bs", "x.v"}, makeCtx());
    REQUIRE(v.ok());
    REQUIRE(v.warnings.empty());
    REQUIRE(v.calls[0].args == Args{"--work=work:work", "-a", "x.v"});
    REQUIRE(v.message.find("-lvdcover bs") != std::string::npos);

    REQUIRE(planVcom({"a.vhd"}, makeCtx()).message.empty());
    REQUIRE(planVcom({"-strict", "+cover", "a.vhd"}, makeCtx()).ok()); // known, so strict mode takes it
    auto other = planVcom({"+coverage_typo", "a.vhd"}, makeCtx());     // only +cover and +cover=...
    REQUIRE(other.warnings.size() == 1);
}
