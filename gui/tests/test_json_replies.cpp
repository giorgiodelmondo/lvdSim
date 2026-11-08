// SPDX-License-Identifier: GPL-3.0-or-later
// The readers of the fork's -json replies, fed with canned lines (docs/protocol.md, schema version 1).
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
        const auto r = parseRunReply(
            "{\"v\":1,\"now\":50000000,\"delta\":0,\"status\":\"breakpoint\",\"breakpoint\":{\"id\":3,\"time\":50000000}}");
        QVERIFY(r.ok());
        QVERIFY(r.value().status == RunStatus::Breakpoint);
        QVERIFY(r.value().breakpoint.has_value());
        QCOMPARE(r.value().breakpoint->id, 3);
        QCOMPARE(r.value().breakpoint->timeFs, qint64(50000000));
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
        const auto r =
            parseRunReply("{\"v\":1,\"future\":{\"a\":[1,2]},\"now\":1,\"delta\":0,\"status\":\"paused\",\"x\":null}");
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
            "{\"v\":1,\"processes\":[{\"name\":\"p\",\"path\":\"/uut/p\",\"state\":\"waiting\",\"file\":\"a.vhd\",\"line\":9},"
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
            "{\"v\":1,\"processes\":[{\"name\":\"p\\\"q\",\"path\":\"/a\\\\b\",\"state\":\"waiting\",\"file\":\"d:\\\\x\\u00e9.vhd\",\"line\":1}]}");
        QVERIFY(r.ok());
        QCOMPARE(r.value()[0].name, QString("p\"q"));
        QCOMPARE(r.value()[0].path, QString("/a\\b"));
        QCOMPARE(r.value()[0].file, QString::fromUtf8("d:\\x\xc3\xa9.vhd"));
    }

    void breakpoints() {
        const auto list =
            parseBreakpointList("{\"v\":1,\"breakpoints\":[{\"id\":2,\"time\":1000},{\"id\":1,\"time\":5000}]}");
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
        QVERIFY(!timeToFemtoseconds("0.5", "fs").has_value());       // finer than a femtosecond
        QVERIFY(!timeToFemtoseconds("1.0005", "ps").has_value());
        QVERIFY(!timeToFemtoseconds("5", "sec").has_value());        // unit not offered
        QVERIFY(!timeToFemtoseconds("", "ns").has_value());
        QVERIFY(!timeToFemtoseconds(".", "ns").has_value());
        QVERIFY(!timeToFemtoseconds("1e3", "ns").has_value());
        QVERIFY(!timeToFemtoseconds("-1", "ns").has_value());
        QVERIFY(!timeToFemtoseconds("99999999999", "ms").has_value());  // overflow
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
};

QTEST_APPLESS_MAIN(TestJsonReplies)
#include "test_json_replies.moc"
