// SPDX-License-Identifier: GPL-3.0-or-later
// The readers of the fork's -json replies, fed with canned lines (docs/protocol.md, schema version 1).
#include "sim/debug_commands.hpp"
#include "sim/parsers/json_replies.hpp"

#include <QtTest>

using namespace lvd::gui;

class TestJsonReplies : public QObject {
    Q_OBJECT
private slots:
    void runReplyPaused() {
        const auto r = parseRunReply("{\"v\":1,\"now\":20000000,\"delta\":0,\"status\":\"paused\"}");
        QVERIFY2(r.ok(), r.ok() ? "" : r.error().c_str());
        QCOMPARE(r.value().nowFs, qint64(20000000));
        QCOMPARE(r.value().delta, qint64(0));
        QVERIFY(r.value().status == RunStatus::Paused);
        QVERIFY(!r.value().breakpoint.has_value());
    }

    void runReplyFinished() {
        const auto r = parseRunReply("{\"v\":1,\"now\":123,\"delta\":4,\"status\":\"finished\"}");
        QVERIFY(r.ok());
        QVERIFY(r.value().status == RunStatus::Finished);
        QCOMPARE(r.value().delta, qint64(4));
    }

    void runReplyWithBreakpoint() {
        const auto r = parseRunReply("{\"v\":1,\"now\":50000000,\"delta\":0,\"status\":\"breakpoint\","
                                     "\"breakpoint\":{\"id\":3,\"time\":50000000}}");
        QVERIFY(r.ok());
        QVERIFY(r.value().status == RunStatus::Breakpoint);
        QVERIFY(r.value().breakpoint.has_value());
        QCOMPARE(r.value().breakpoint->id, 3);
        QCOMPARE(r.value().breakpoint->timeFs, qint64(50000000));
    }

    void runReplyInterrupted() {
        const auto r =
            parseRunReply("{\"v\":1,\"now\":3842735000000,\"delta\":1,\"status\":\"interrupted\"}");
        QVERIFY(r.ok());
        QVERIFY(r.value().status == RunStatus::Interrupted);
        QCOMPARE(r.value().nowFs, qint64(3842735000000LL));
        QVERIFY(!r.value().breakpoint.has_value());
    }

    void breakpointStatusNeedsTheBreakpoint() {
        QVERIFY(!parseRunReply("{\"v\":1,\"now\":1,\"delta\":0,\"status\":\"breakpoint\"}").ok());
    }

    void timesBeyondInt32() {
        const auto r = parseRunReply("{\"v\":1,\"now\":9000000000000000,\"delta\":0,\"status\":\"paused\"}");
        QVERIFY(r.ok());
        QCOMPARE(r.value().nowFs, qint64(9000000000000000LL));
    }

    void unknownMembersAreIgnored() {
        const auto r = parseRunReply(
            "{\"v\":1,\"future\":{\"a\":[1,2]},\"now\":1,\"delta\":0,\"status\":\"paused\",\"x\":null}");
        QVERIFY(r.ok());
    }

    void unknownVersionIsAnError() {
        const auto r = parseRunReply("{\"v\":2,\"now\":1,\"delta\":0,\"status\":\"paused\"}");
        QVERIFY(!r.ok());
        QVERIFY(QString::fromStdString(r.error()).contains("version"));
    }

    void notJsonIsAnError() {
        for (const char* text : {"", "paused", "{\"v\":1,", "[1,2]", "{\"now\":1}", "{\"v\":\"1\"}",
                                 "{\"v\":1.5}", "{\"v\":1} trailing"}) {
            QVERIFY2(!parseReply(QString::fromLatin1(text)).ok(), text);
            QVERIFY2(!parseRunReply(QString::fromLatin1(text)).ok(), text);
            QVERIFY2(!parseProcessesReply(QString::fromLatin1(text)).ok(), text);
        }
    }

    void missingOrWrongMembers() {
        QVERIFY(!parseRunReply("{\"v\":1,\"now\":1,\"status\":\"paused\"}").ok());
        QVERIFY(!parseRunReply("{\"v\":1,\"now\":1.5,\"delta\":0,\"status\":\"paused\"}").ok());
        QVERIFY(!parseRunReply("{\"v\":1,\"now\":\"1\",\"delta\":0,\"status\":\"paused\"}").ok());
        QVERIFY(!parseRunReply("{\"v\":1,\"now\":1,\"delta\":0,\"status\":\"sleeping\"}").ok());
        QVERIFY(!parseProcessesReply("{\"v\":1,\"names\":[]}").ok());
        QVERIFY(!parseBreakpointList("{\"v\":1}").ok());
        QVERIFY(!parseBreakpointList("{\"v\":1,\"breakpoints\":[{\"id\":1}]}").ok());
        QVERIFY(!parseDeleted("{\"v\":1,\"gone\":1}").ok());
    }

    void processList() {
        const auto r = parseProcessesReply(
            "{\"v\":1,\"processes\":[{\"name\":\"p\",\"path\":\"/uut/"
            "p\",\"state\":\"waiting\",\"file\":\"a.vhd\",\"line\":9},"
            "{\"name\":\"_p0\",\"path\":\"/_p0\",\"state\":\"ready\",\"file\":null,\"line\":null}]}");
        QVERIFY2(r.ok(), r.ok() ? "" : r.error().c_str());
        QCOMPARE(r.value().size(), 2);
        QCOMPARE(r.value()[0].name, QString("p"));
        QCOMPARE(r.value()[0].path, QString("/uut/p"));
        QCOMPARE(r.value()[0].state, QString("waiting"));
        QCOMPARE(r.value()[0].file, QString("a.vhd"));
        QCOMPARE(r.value()[0].line, 9);
        QCOMPARE(r.value()[1].file, QString()); // null: location unknown
        QCOMPARE(r.value()[1].line, 0);
        QCOMPARE(r.value()[1].state, QString("ready"));
    }

    void emptyProcessList() {
        const auto r = parseProcessesReply("{\"v\":1,\"processes\":[]}");
        QVERIFY(r.ok());
        QVERIFY(r.value().isEmpty());
    }

    void unicodeAndEscapesInNames() {
        const auto r = parseProcessesReply(
            "{\"v\":1,\"processes\":[{\"name\":\"p\\\"q\",\"path\":\"/"
            "a\\\\b\",\"state\":\"waiting\",\"file\":\"d:\\\\x\\u00e9.vhd\",\"line\":1}]}");
        QVERIFY(r.ok());
        QCOMPARE(r.value()[0].name, QString("p\"q"));
        QCOMPARE(r.value()[0].path, QString("/a\\b"));
        QCOMPARE(r.value()[0].file, QString::fromUtf8("d:\\x\xc3\xa9.vhd"));
    }

    void breakpoints() {
        const auto list = parseBreakpointList(
            "{\"v\":1,\"breakpoints\":[{\"id\":2,\"time\":1000},{\"id\":1,\"time\":5000}]}");
        QVERIFY(list.ok());
        QCOMPARE(list.value().size(), 2);
        QCOMPARE(list.value()[0].id, 2);
        QCOMPARE(list.value()[1].timeFs, qint64(5000));
        QVERIFY(parseBreakpointList("{\"v\":1,\"breakpoints\":[]}").value().isEmpty());

        const auto added = parseBreakpointAdded("{\"v\":1,\"id\":7,\"time\":250000000}");
        QVERIFY(added.ok());
        QCOMPARE(added.value().id, 7);
        QCOMPARE(added.value().timeFs, qint64(250000000));

        const auto del = parseDeleted("{\"v\":1,\"deleted\":3}");
        QVERIFY(del.ok());
        QCOMPARE(del.value(), 3);
    }

    void decimalTimesBecomeWholeFemtoseconds() {
        QCOMPARE(timeToFemtoseconds("50", "ns").value(), qint64(50000000));
        QCOMPARE(timeToFemtoseconds("0.5", "us").value(), qint64(500000000));
        QCOMPARE(timeToFemtoseconds(" 2.50 ", "NS").value(), qint64(2500000));
        QCOMPARE(timeToFemtoseconds(".5", "ps").value(), qint64(500));
        QCOMPARE(timeToFemtoseconds("007", "fs").value(), qint64(7));
        QCOMPARE(timeToFemtoseconds("1.", "ms").value(), qint64(1000000000000));
        QVERIFY(!timeToFemtoseconds("0.5", "fs").has_value()); // finer than a femtosecond
        QVERIFY(!timeToFemtoseconds("1.0005", "ps").has_value());
        QVERIFY(!timeToFemtoseconds("5", "sec").has_value()); // unit not offered
        QVERIFY(!timeToFemtoseconds("", "ns").has_value());
        QVERIFY(!timeToFemtoseconds(".", "ns").has_value());
        QVERIFY(!timeToFemtoseconds("1e3", "ns").has_value());
        QVERIFY(!timeToFemtoseconds("-1", "ns").has_value());
        QVERIFY(!timeToFemtoseconds("99999999999", "ms").has_value()); // overflow
        QCOMPARE(timeToFemtoseconds("0", "ns").value(), qint64(0));
    }

    void timeFormatting() {
        QCOMPARE(formatFemtoseconds(0), QString("0 fs"));
        QCOMPARE(formatFemtoseconds(1), QString("1 fs"));
        QCOMPARE(formatFemtoseconds(50000000), QString("50 ns"));
        QCOMPARE(formatFemtoseconds(1500000), QString("1500 ps"));
        QCOMPARE(formatFemtoseconds(2000000000), QString("2 us"));
        QCOMPARE(formatFemtoseconds(3000000000000), QString("3 ms"));
        QCOMPARE(formatFemtoseconds(1234), QString("1234 fs"));
    }
    // ---- describe: the optional port mode -----------------------------------------------------------
    void describeWithModes() {
        const auto r = parseDescribeReply(
            R"({"v":1,"objects":[{"name":"clk","path":"/dut/clk","kind":"signal","type":"BIT","mode":"in"},)"
            R"({"name":"q","path":"/dut/q","kind":"signal","type":"BIT","mode":"out"},)"
            R"({"name":"io","path":"/dut/io","kind":"signal","type":"BIT","mode":"INOUT"},)"
            R"({"name":"b","path":"/dut/b","kind":"signal","type":"BIT","mode":"buffer"},)"
            R"({"name":"s","path":"/dut/s","kind":"signal","type":"BIT","mode":"internal"},)"
            R"({"name":"l","path":"/dut/l","kind":"signal","type":"BIT","mode":"linkage"},)"
            R"({"name":"dut","path":"/dut","kind":"region"}]})");
        QVERIFY2(r.ok(), r.ok() ? "" : r.error().c_str());
        const auto& v = r.value();
        QCOMPARE(v.size(), 7);
        QCOMPARE(v[0].mode, PortMode::In);
        QCOMPARE(v[1].mode, PortMode::Out);
        QCOMPARE(v[2].mode, PortMode::InOut);
        QCOMPARE(v[3].mode, PortMode::Buffer);
        QCOMPARE(v[4].mode, PortMode::Internal);
        QCOMPARE(v[5].mode, PortMode::Unknown); // linkage: not shown as a direction
        QCOMPARE(v[6].mode, PortMode::Unknown);
        QCOMPARE(v[6].kind, QString("region"));
        QCOMPARE(v[0].type, QString("BIT"));
        QCOMPARE(v[0].path, QString("/dut/clk"));
    }

    void describeWithoutModeIsStillValid() { // kernels that do not report it
        const auto r = parseDescribeReply(
            R"({"v":1,"objects":[{"name":"x","path":"/x","kind":"signal","type":"INTEGER"}]})");
        QVERIFY(r.ok());
        QCOMPARE(r.value().size(), 1);
        QCOMPARE(r.value()[0].mode, PortMode::Unknown);
    }

    // ---- describe -location (kernel PR 10): every value is a string; numbers are taken too ----------
    void describeWithLocations() {
        const auto r = parseDescribeReply(
            R"j({"v":1,"objects":[{"name":"clk","path":"/uut/clk","kind":"signal","type":"BIT","mode":"in",)j"
            R"j("file":"src/sub.vhd","line":"3","column":"12"},)j"
            R"j({"name":"uut","path":"/uut/","kind":"region","unit":"work.sub(rtl)","unit_file":"src/sub.vhd",)j"
            R"j("unit_line":"8","inst_file":"top.vhd","inst_line":"53"},)j"
            R"j({"name":"top","path":"/","kind":"region","unit":"work.top(test)","unit_file":"top.vhd","unit_line":41},)j"
            R"j({"name":"width","path":"/uut/width","kind":"generic","type":"NATURAL","file":"src/sub.vhd","line":2,"column":15}]})j");
        QVERIFY2(r.ok(), r.ok() ? "" : r.error().c_str());
        const auto& v = r.value();
        QCOMPARE(v.size(), 4);
        QCOMPARE(v[0].file, QString("src/sub.vhd"));
        QCOMPARE(v[0].line, 3);
        QCOMPARE(v[0].column, 12);
        QCOMPARE(v[0].mode, PortMode::In);
        QVERIFY(v[0].hasLocation());
        QCOMPARE(v[1].unit, QString("work.sub(rtl)"));
        QCOMPARE(v[1].unitFile, QString("src/sub.vhd"));
        QCOMPARE(v[1].unitLine, 8);
        QCOMPARE(v[1].instFile, QString("top.vhd"));
        QCOMPARE(v[1].instLine, 53);
        QVERIFY(v[1].hasLocation());
        QCOMPARE(v[2].unitLine, 41); // a number
        QVERIFY(v[2].instFile.isEmpty());
        QCOMPARE(v[2].instLine, 0);
        QCOMPARE(v[3].kind, QString("generic"));
        QCOMPARE(v[3].line, 2);
        QCOMPARE(v[3].column, 15);
    }

    void describeWithoutLocationsHasNone() { // an older kernel, or no -location
        const auto r = parseDescribeReply(
            R"j({"v":1,"objects":[{"name":"clk","path":"/clk","kind":"signal","type":"BIT","mode":"in"},)j"
            R"j({"name":"t","path":"/","kind":"region"},)j"
            R"j({"name":"x","path":"/x","kind":"signal","type":"BIT","file":"a.vhd","line":"zero"}]})j");
        QVERIFY(r.ok());
        QVERIFY(!r.value()[0].hasLocation());
        QVERIFY(!r.value()[1].hasLocation());
        QCOMPARE(r.value()[2].line, 0); // not a number: no location
        QVERIFY(!r.value()[2].hasLocation());
    }

    void describeNeedsObjectsAndPaths() {
        QVERIFY(!parseDescribeReply(R"({"v":1})").ok());
        QVERIFY(!parseDescribeReply(R"({"v":1,"objects":[{"name":"x"}]})").ok());
    }

    void portModeNames() {
        for (PortMode m :
             {PortMode::In, PortMode::Out, PortMode::InOut, PortMode::Buffer, PortMode::Internal})
            QCOMPARE(portModeFromString(portModeName(m)), m);
        QVERIFY(portModeName(PortMode::Unknown).isEmpty());
        QCOMPARE(portModeFromString("what"), PortMode::Unknown);
    }

    // ---- source debugger: line breakpoints, stops, stack, locals (lines printed by the kernel) --------
    void lineBreakpoints() {
        const auto added = parseLineBreakpointAdded(R"({"v":1,"id":2,"file":"tcldbg1.vhd","line":30})");
        QVERIFY2(added.ok(), added.ok() ? "" : added.error().c_str());
        QCOMPARE(added.value().id, 2);
        QCOMPARE(added.value().file, QString("tcldbg1.vhd"));
        QCOMPARE(added.value().line, 30);
        QCOMPARE(added.value().hits, 0);
        QVERIFY(!parseBreakpointAdded(R"({"v":1,"id":2,"file":"tcldbg1.vhd","line":30})").ok());
        QVERIFY(!parseLineBreakpointAdded(R"({"v":1,"id":7,"time":250000000})").ok());

        // A mixed list: each reader takes its own kind
        const QString mixed =
            R"({"v":1,"breakpoints":[{"id":4,"time":15000000},{"id":1,"file":"tcldbg1.vhd","line":50,"hits":0},)"
            R"({"id":2,"file":"C:/src/a b.vhd","line":30,"hits":2}]})";
        const auto times = parseBreakpointList(mixed);
        QVERIFY2(times.ok(), times.ok() ? "" : times.error().c_str());
        QCOMPARE(times.value().size(), 1);
        QCOMPARE(times.value()[0].id, 4);
        QCOMPARE(times.value()[0].timeFs, qint64(15000000));
        const auto lines = parseLineBreakpointList(mixed);
        QVERIFY2(lines.ok(), lines.ok() ? "" : lines.error().c_str());
        QCOMPARE(lines.value().size(), 2);
        QCOMPARE(lines.value()[0].id, 1);
        QCOMPARE(lines.value()[0].line, 50);
        QCOMPARE(lines.value()[1].file, QString("C:/src/a b.vhd"));
        QCOMPARE(lines.value()[1].hits, 2);
        QVERIFY(parseLineBreakpointList(R"({"v":1,"breakpoints":[]})").value().isEmpty());

        QVERIFY(!parseLineBreakpointList(R"({"v":1})").ok());
        QVERIFY(!parseLineBreakpointList(R"({"v":1,"breakpoints":[{"id":1,"line":3}]})").ok());
        QVERIFY(!parseLineBreakpointList(R"({"v":1,"breakpoints":[{"id":1,"file":"a.vhd","line":0}]})").ok());
        QVERIFY(!parseLineBreakpointList(R"({"v":1,"breakpoints":[{"file":"a.vhd","line":2}]})").ok());
    }

    void stopAtBreakpoint() {
        const QString line =
            R"({"v":1,"now":6000000,"delta":0,"status":"stopped","stop":{"reason":"breakpoint","id":1,)"
            R"("process":"/pulser","frame":{"level":0,"kind":"procedure","name":"toggle",)"
            "\"symbol\":\"WORK.TCLDBG1.TOGGLE(sJIIQS)\",\"file\":\"D:/x/tcldbg1.vhd\",\"line\":30}}}";
        QVERIFY(isStopReply(line));
        QVERIFY(!parseRunReply(line).ok()); // the run reader does not know the status
        const auto r = parseStopReply(line);
        QVERIFY2(r.ok(), r.ok() ? "" : r.error().c_str());
        QCOMPARE(r.value().nowFs, qint64(6000000));
        QCOMPARE(r.value().delta, qint64(0));
        QVERIFY(r.value().atBreakpoint);
        QCOMPARE(r.value().breakpointId, 1);
        QCOMPARE(r.value().process, QString("/pulser"));
        QCOMPARE(r.value().frame.kind, QString("procedure"));
        QCOMPARE(r.value().frame.name, QString("toggle"));
        QCOMPARE(r.value().frame.symbol, QString("WORK.TCLDBG1.TOGGLE(sJIIQS)"));
        QCOMPARE(r.value().frame.file, QString("D:/x/tcldbg1.vhd"));
        QCOMPARE(r.value().frame.line, 30);
    }

    void stopAfterStep() {
        const auto r = parseStopReply(
            R"({"v":1,"now":10000000,"delta":0,"status":"stopped","stop":{"reason":"step","process":"/counter",)"
            R"("frame":{"level":0,"kind":"process","name":"/counter","symbol":"WORK.DEBUG1.COUNTER","file":"d.vhd","line":44}}})");
        QVERIFY2(r.ok(), r.ok() ? "" : r.error().c_str());
        QVERIFY(!r.value().atBreakpoint);
        QCOMPARE(r.value().breakpointId, 0);
        QCOMPARE(r.value().frame.kind, QString("process"));
        QCOMPARE(r.value().frame.line, 44);
    }

    void stopBySystemTask() {
        // Recorded from nvc -i (change set 36) with a Verilog $stop at 10 fs.
        const QString line =
            R"({"v":1,"now":10,"delta":0,"status":"stopped","stop":{"reason":"stop","process":"/initial#4#2",)"
            R"("frame":{"level":0,"kind":"process","name":"/initial#4#2","symbol":"WORK.VSTOP2#0.initial#4#2",)"
            R"("file":"vstop2.v","line":6}}})";
        QVERIFY(isStopReply(line));
        QVERIFY(!parseRunReply(line).ok());
        const auto r = parseStopReply(line);
        QVERIFY2(r.ok(), r.ok() ? "" : r.error().c_str());
        QVERIFY(r.value().reason == StopReason::Stop);
        QVERIFY(!r.value().atBreakpoint);
        QCOMPARE(r.value().breakpointId, 0);
        QCOMPARE(r.value().nowFs, qint64(10));
        QCOMPARE(r.value().process, QString("/initial#4#2"));
        QCOMPARE(r.value().frame.file, QString("vstop2.v"));
        QCOMPARE(r.value().frame.line, 6);

        // A $stop reply is accepted without a frame (null or missing); breakpoint and step still need one.
        for (const char* tail : {R"(}})", R"(,"frame":null}})"}) {
            const auto n = parseStopReply(
                QString(
                    R"({"v":1,"now":5,"delta":1,"status":"stopped","stop":{"reason":"stop","process":"/p")") +
                tail);
            QVERIFY2(n.ok(), n.ok() ? "" : n.error().c_str());
            QVERIFY(n.value().reason == StopReason::Stop);
            QVERIFY(n.value().frame.file.isEmpty());
            QCOMPARE(n.value().frame.line, 0);
            QCOMPARE(n.value().delta, qint64(1));
        }
        QVERIFY(!parseStopReply(R"({"v":1,"now":1,"delta":0,"status":"stopped","stop":{"reason":"stop",)"
                                R"("frame":{"level":0}}})")
                     .ok()); // a frame that is there must be complete
    }

    void stopReasonsOfOlderReplies() {
        const auto b =
            parseStopReply(R"({"v":1,"now":1,"delta":0,"status":"stopped","stop":{"reason":"breakpoint",)"
                           R"("id":4,"process":"/p","frame":{"level":0,"kind":"process","name":"/p"}}})");
        QVERIFY(b.ok());
        QVERIFY(b.value().reason == StopReason::Breakpoint);
        const auto s =
            parseStopReply(R"({"v":1,"now":1,"delta":0,"status":"stopped","stop":{"reason":"step",)"
                           R"("process":"/p","frame":{"level":0,"kind":"process","name":"/p"}}})");
        QVERIFY(s.ok());
        QVERIFY(s.value().reason == StopReason::Step);
    }

    void stopRepliesAreChecked() {
        const QString finished = R"({"v":1,"now":30000000,"delta":0,"status":"finished"})";
        QVERIFY(!isStopReply(finished));
        QVERIFY(parseRunReply(finished).ok());
        QVERIFY(!parseStopReply(finished).ok());
        QVERIFY(!isStopReply("not json"));
        QVERIFY(
            !parseStopReply(R"({"v":1,"now":1,"delta":0,"status":"stopped","stop":{"reason":"breakpoint",)"
                            R"("frame":{"level":0,"kind":"process","name":"/p"}}})")
                 .ok()); // breakpoint without id
        QVERIFY(!parseStopReply(R"({"v":1,"now":1,"delta":0,"status":"stopped","stop":{"reason":"nap",)"
                                R"("frame":{"level":0,"kind":"process","name":"/p"}}})")
                     .ok());
        QVERIFY(
            !parseStopReply(R"({"v":1,"now":1,"delta":0,"status":"stopped","stop":{"reason":"step"}})").ok());
        // file and line may be null
        const auto r = parseStopReply(
            R"({"v":1,"now":1,"delta":0,"status":"stopped","stop":{"reason":"step",)"
            R"("process":"/p","frame":{"level":0,"kind":"other","name":"x","file":null,"line":null}}})");
        QVERIFY(r.ok());
        QVERIFY(r.value().frame.file.isEmpty());
        QCOMPARE(r.value().frame.line, 0);
    }

    void stackFrames() {
        const auto r = parseStackReply(
            R"({"v":1,"frames":[{"level":0,"kind":"function","name":"double","symbol":"WORK.DEBUG1.DOUBLE(I)I",)"
            R"("file":"debug1.vhd","line":11},{"level":1,"kind":"process","name":"/counter",)"
            R"("symbol":"WORK.DEBUG1.COUNTER","file":"debug1.vhd","line":45}]})");
        QVERIFY2(r.ok(), r.ok() ? "" : r.error().c_str());
        QCOMPARE(r.value().size(), 2);
        QCOMPARE(r.value()[0].level, 0);
        QCOMPARE(r.value()[0].kind, QString("function"));
        QCOMPARE(r.value()[0].name, QString("double"));
        QCOMPARE(r.value()[1].kind, QString("process"));
        QCOMPARE(r.value()[1].name, QString("/counter"));
        QCOMPARE(r.value()[1].line, 45);
        QVERIFY(!parseStackReply(R"({"v":1})").ok());
        QVERIFY(!parseStackReply(R"({"v":1,"frames":[{"kind":"process","name":"/p"}]})").ok());
    }

    void localVariables() {
        const auto r = parseLocalsReply(
            R"({"v":1,"frame":0,"variables":[{"name":"s","kind":"parameter","type":"BIT","value":null},)"
            "{\"name\":\"tag\",\"kind\":\"parameter\",\"type\":\"STRING\",\"value\":\"\\\"hello\\\"\"},"
            "{\"name\":\"both\",\"kind\":\"variable\",\"type\":\"PAIR\",\"value\":\"(a => 7, b => '1')\"},"
            R"({"name":"k","kind":"variable","type":"INTEGER","value":"2"}]})");
        QVERIFY2(r.ok(), r.ok() ? "" : r.error().c_str());
        QCOMPARE(r.value().frame, 0);
        const auto& v = r.value().variables;
        QCOMPARE(v.size(), 4);
        QVERIFY(!v[0].hasValue); // signal parameter: not shown
        QCOMPARE(v[0].kind, QString("parameter"));
        QVERIFY(v[1].hasValue);
        QCOMPARE(v[1].value, QString("\"hello\""));
        QCOMPARE(v[2].value, QString("(a => 7, b => '1')"));
        QCOMPARE(v[3].type, QString("INTEGER"));
        QCOMPARE(v[3].value, QString("2"));

        const auto empty = parseLocalsReply(R"({"v":1,"frame":2,"variables":[]})");
        QVERIFY(empty.ok());
        QCOMPARE(empty.value().frame, 2);
        QVERIFY(empty.value().variables.isEmpty());
        QVERIFY(!parseLocalsReply(R"({"v":1,"variables":[]})").ok());
        QVERIFY(!parseLocalsReply(R"({"v":1,"frame":0,"variables":[{"value":"1"}]})").ok());
    }

    void debugCommands() {
        QCOMPARE(lineBreakpointCommand("tb.vhd", 12), QString("breakpoint add -json tb.vhd:12"));
        QCOMPARE(lineBreakpointCommand("C:/my src/tb.vhd", 7),
                 QString("breakpoint add -json {C:/my src/tb.vhd:7}"));
        QCOMPARE(lineBreakpointCommand("a{b}.vhd", 1), QString("breakpoint add -json a\\{b\\}.vhd:1"));
        QCOMPARE(statementStepCommand(StepKind::Into), QString("step -into -json"));
        QCOMPARE(statementStepCommand(StepKind::Over), QString("step -over -json"));
        QCOMPARE(statementStepCommand(StepKind::Out), QString("step -out -json"));
        QCOMPARE(resumeCommand(), QString("run -json"));
        QCOMPARE(stackCommand(), QString("stack -json"));
        QCOMPARE(localsCommand(1), QString("locals -frame 1 -json"));
        QCOMPARE(tclWord(""), QString("{}"));
        QCOMPARE(tclWord("plain"), QString("plain"));
        QCOMPARE(tclWord("x$y"), QString("{x$y}"));
        QCOMPARE(tclWord("ends\\"), QString("ends\\\\"));
    }

    void restartReplies() {
        // As restart -json prints it, and as the run interrupted at a source line gives it
        const QString line = R"({"v":1,"now":0,"delta":0,"status":"restarted","breakpoints":2,"forces":1})";
        QVERIFY(isRestartReply(line));
        QVERIFY(!isStopReply(line));
        QVERIFY(!parseRunReply(line).ok()); // check isRestartReply first
        const auto r = parseRestartReply(line);
        QVERIFY2(r.ok(), r.ok() ? "" : r.error().c_str());
        QCOMPARE(r.value().nowFs, qint64{0});
        QCOMPARE(r.value().breakpoints, 2);
        QCOMPARE(r.value().forces, 1);

        const QString finished = R"({"v":1,"now":30000000,"delta":0,"status":"finished"})";
        QVERIFY(!isRestartReply(finished));
        QVERIFY(!parseRestartReply(finished).ok());
        QVERIFY(!isRestartReply("not json"));
        QVERIFY(!parseRestartReply(R"({"v":1,"now":0,"delta":0,"status":"restarted"})").ok());
        QVERIFY(
            !parseRestartReply(R"({"v":2,"now":0,"delta":0,"status":"restarted","breakpoints":0,"forces":0})")
                 .ok());

        QCOMPARE(restartCommand(), QString("restart -json"));
        QCOMPARE(restartCommand(false), QString("restart -noforce -json"));
        QCOMPARE(restartCommand(true, false), QString("restart -nobreakpoint -json"));
        QCOMPARE(restartCommand(false, false), QString("restart -noforce -nobreakpoint -json"));
    }
};

QTEST_APPLESS_MAIN(TestJsonReplies)
#include "test_json_replies.moc"
