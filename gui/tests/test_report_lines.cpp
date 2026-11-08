// SPDX-License-Identifier: GPL-3.0-or-later
// The recogniser of HDL report lines in the transcript (pure, no widgets).
#include "sim/parsers/report_lines.hpp"

#include <QtTest>

using namespace lvd::gui;

class TestReportLines : public QObject {
    Q_OBJECT
private slots:
    void severities_data() {
        QTest::addColumn<QString>("line");
        QTest::addColumn<int>("severity");
        QTest::addColumn<qlonglong>("fs");
        QTest::newRow("note") << "** Note: 100ns+0: reset released" << int(ReportSeverity::Note) << 100000000LL;
        QTest::newRow("warning") << "** Warning: 250ns+0: fifo" << int(ReportSeverity::Warning) << 250000000LL;
        QTest::newRow("error") << "** Error: 500ns+0: bad" << int(ReportSeverity::Error) << 500000000LL;
        QTest::newRow("failure") << "** Failure: 2us+0: stop" << int(ReportSeverity::Failure) << 2000000000LL;
        QTest::newRow("fatal") << "** Fatal: 1ms+0: stop" << int(ReportSeverity::Failure) << 1000000000000LL;
        QTest::newRow("debug") << "** Debug: 3ps+0: x" << int(ReportSeverity::Note) << 3000LL;
    }
    void severities() {
        QFETCH(QString, line);
        QFETCH(int, severity);
        QFETCH(qlonglong, fs);
        const auto r = parseReportLine(line.toStdString());
        QVERIFY(r.has_value());
        QCOMPARE(int(r->severity), severity);
        QCOMPARE(qlonglong(r->timeFs), fs);
    }

    void units_data() {
        QTest::addColumn<QString>("stamp");
        QTest::addColumn<qlonglong>("fs");
        QTest::newRow("fs") << "7fs" << 7LL;
        QTest::newRow("ps") << "7ps" << 7000LL;
        QTest::newRow("ns") << "7ns" << 7000000LL;
        QTest::newRow("us") << "7us" << 7000000000LL;
        QTest::newRow("ms") << "7ms" << 7000000000000LL;
        QTest::newRow("s") << "7s" << 7000000000000000LL;
        QTest::newRow("space") << "20 us" << 20000000000LL;
        QTest::newRow("big ms") << "1000ms" << 1000000000000000LL;
        QTest::newRow("zero") << "0fs" << 0LL;
        QTest::newRow("fraction") << "1.5ns" << 1500000LL;
        QTest::newRow("fraction ps") << "0.25 ps" << 250LL;
    }
    void units() {
        QFETCH(QString, stamp);
        QFETCH(qlonglong, fs);
        const auto r = parseReportLine(("** Note: " + stamp + "+0: hi").toStdString());
        QVERIFY(r.has_value());
        QCOMPARE(qlonglong(r->timeFs), fs);
        QCOMPARE(r->message, std::string("hi"));
    }

    void deltaAndStampRange() {
        const std::string line = "** Warning: 20 us+13: text: with colon";
        const auto r = parseReportLine(line);
        QVERIFY(r.has_value());
        QCOMPARE(r->delta, 13);
        QCOMPARE(line.substr(r->stampBegin, r->stampEnd - r->stampBegin), std::string("20 us+13"));
        QCOMPARE(r->message, std::string("text: with colon"));
    }

    void emptyMessageAndPrompt() {
        const std::string line = "% ** Note: 5ns+2: ";
        const auto r = parseReportLine(line);
        QVERIFY(r.has_value());
        QCOMPARE(r->message, std::string());
        QCOMPARE(line.substr(r->stampBegin, r->stampEnd - r->stampBegin), std::string("5ns+2"));
        QVERIFY(parseReportLine("% % ** Note: 5ns+0: x").has_value());
    }

    void nonMatching_data() {
        QTest::addColumn<QString>("line");
        QTest::newRow("empty") << "";
        QTest::newRow("verilog display") << "verilog display at 120 time units";
        QTest::newRow("verilog with stars") << "** looks like a report";
        QTest::newRow("init") << "** Note: (init): early report";
        QTest::newRow("continuation") << "   Process :tb:_p2 at tb.vhd:21";
        QTest::newRow("unknown severity") << "** Hint: 5ns+0: x";
        QTest::newRow("no delta") << "** Note: 5ns: x";
        QTest::newRow("no unit") << "** Note: 5+0: x";
        QTest::newRow("bad unit") << "** Note: 5min+0: x";
        QTest::newRow("no colon space") << "** Note: 5ns+0 x";
        QTest::newRow("negative") << "** Note: -5ns+0: x";
        QTest::newRow("overflow") << "** Note: 99999999999999999999ns+0: x";
        QTest::newRow("overflow unit") << "** Note: 9999999999s+0: x";
        QTest::newRow("sub fs fraction") << "** Note: 0.0001ps+0: x";
        QTest::newRow("trailing dot") << "** Note: 5.ns+0: x";
        QTest::newRow("indented") << " ** Note: 5ns+0: x";
        QTest::newRow("truncated") << "** Note: 5n";
        QTest::newRow("only stars") << "** ";
        QTest::newRow("garbage") << "\x01\x02\xff\xfe ** \x03";
    }
    void nonMatching() {
        QFETCH(QString, line);
        QVERIFY(!parseReportLine(line.toStdString()).has_value());
    }

    void unitTable() {
        QVERIFY(unitToFemtoseconds("ns").has_value());
        QVERIFY(!unitToFemtoseconds("min").has_value());
        QVERIFY(!unitToFemtoseconds("").has_value());
    }
};

QTEST_APPLESS_MAIN(TestReportLines)
#include "test_report_lines.moc"
