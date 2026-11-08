// SPDX-License-Identifier: GPL-3.0-or-later
// Report links in the transcript: recognised lines become clickable time stamps (with the wave window
// connected the way SimWindow connects them).
#include "app/transcript_widget.hpp"
#include "wave/wave_widget.hpp"

#include <QPlainTextEdit>
#include <QTextBlock>
#include <QtTest>

using namespace lvd::gui;
using namespace lvd::wave;

namespace {

std::shared_ptr<WaveStore> makeStore() {
    auto st = std::make_shared<WaveStore>();
    const ScopeId top = st->addScope(kRootScope, "top");
    const SignalId clk = st->addSignal(top, "clk", SignalKind::Logic, 1);
    for (Time t = 0; t <= 4000000000ull; t += 500000000ull)
        st->mutableSignal(clk).appendLogic(t, (t / 500000000ull) % 2 ? "1" : "0");
    st->extendTimeRange(0);
    st->extendTimeRange(4000000000ull); // 4 us in femtoseconds
    return st;
}

const char* kText = "% run 3 us\n"
                    "** Note: 100ns+0: reset released\n"
                    "   Process :tb:_p2 at tb.vhd:21\n"
                    "** Warning: 20 us+1: not a link target in the view\n"
                    "verilog display at 120 time units\n"
                    "** Error: 500ns+3: checksum mismatch\n"
                    "** Failure: 2us+0: stop\n";

// Viewport position of a character in a block, a little inside the glyph.
QPoint charPos(QPlainTextEdit* view, int block, int ch) {
    const QTextBlock b = view->document()->findBlockByNumber(block);
    QTextCursor c(b);
    c.setPosition(b.position() + ch);
    const QRect r = view->cursorRect(c);
    return QPoint(r.left() + 3, r.center().y());
}

} // namespace

class TestTranscript : public QObject {
    Q_OBJECT

    struct Fixture {
        TranscriptWidget transcript;
        WaveWidget wave;
        std::vector<std::tuple<qint64, int, QString>> reports;

        Fixture(bool withStore = true) {
            transcript.resize(700, 300);
            wave.resize(900, 320);
            if (withStore)
                wave.setStore(makeStore());
            connect(&transcript, &TranscriptWidget::timeActivated, &wave, &WaveWidget::showTimeFs);
            connect(&transcript, &TranscriptWidget::reportLogged, &wave,
                    [this](qint64 fs, int sev, const QString& text) {
                        wave.addEventMarker({fs, static_cast<EventSeverity>(sev), text});
                        reports.emplace_back(fs, sev, text);
                    });
            transcript.show();
            (void)QTest::qWaitForWindowExposed(&transcript);
        }
    };

    static void click(QPlainTextEdit* view, const QPoint& p) {
        QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, p);
    }

private slots:
    void textIsUnchangedAndReportsAreAnnounced() {
        Fixture f;
        f.transcript.append(QString::fromLatin1(kText));
        QCOMPARE(f.transcript.text(), QString::fromLatin1(kText));
        QCOMPARE(f.reports.size(), std::size_t(4));
        QCOMPARE(std::get<0>(f.reports[0]), 100000000LL);
        QCOMPARE(std::get<1>(f.reports[0]), 0);
        QCOMPARE(std::get<2>(f.reports[0]), QString("reset released"));
        QCOMPARE(std::get<0>(f.reports[1]), 20000000000LL);
        QCOMPARE(std::get<1>(f.reports[1]), 1);
        QCOMPARE(std::get<1>(f.reports[2]), 2);
        QCOMPARE(std::get<1>(f.reports[3]), 3);
        QCOMPARE(f.wave.eventMarkerCount(), std::size_t(4));
    }

    void clickingTheTimeStampMovesCursorOne() {
        Fixture f;
        f.wave.setView(0.0, 1000000.0); // 1e6 fs per pixel: the whole 4 us run is visible
        const double zoom = f.wave.view().timePerPixel;
        f.transcript.append(QString::fromLatin1(kText));
        QPlainTextEdit* view = f.transcript.textView();

        // "** Note: 100ns+0: ..." in block 1; the stamp starts at character 9
        click(view, charPos(view, 1, 11));
        QCOMPARE(f.wave.cursorCount(), 1);
        QCOMPARE(*f.wave.cursorTime(0), 100000000.0);
        QCOMPARE(f.wave.view().timePerPixel, zoom); // visible already: zoom and position stay
        QCOMPARE(f.wave.view().startTime, 0.0);

        // the error with delta 3 (block 5), the failure (block 6)
        click(view, charPos(view, 5, 12));
        QCOMPARE(*f.wave.cursorTime(0), 500000000.0);
        click(view, charPos(view, 6, 13));
        QCOMPARE(*f.wave.cursorTime(0), 2000000000.0);
        QCOMPARE(f.wave.cursorCount(), 1);
    }

    void aTimeOutsideTheViewScrollsToIt() {
        Fixture f;
        f.wave.setView(0.0, 1000.0); // about 0 .. 600000 fs
        f.transcript.append("** Note: 2us+0: far\n");
        QPlainTextEdit* view = f.transcript.textView();
        click(view, charPos(view, 0, 11));
        QCOMPARE(*f.wave.cursorTime(0), 2000000000.0);
        QVERIFY(f.wave.view().startTime <= 2000000000.0 && f.wave.view().endTime() >= 2000000000.0);
        QCOMPARE(f.wave.view().timePerPixel, 1000.0);
    }

    void onlyTheTimeStampIsALink() {
        Fixture f;
        f.transcript.append(QString::fromLatin1(kText));
        QPlainTextEdit* view = f.transcript.textView();
        QSignalSpy spy(&f.transcript, &TranscriptWidget::timeActivated);
        click(view, charPos(view, 1, 3));  // "Note:"
        click(view, charPos(view, 1, 25)); // the message
        click(view, charPos(view, 2, 10)); // continuation line
        click(view, charPos(view, 4, 5));  // Verilog $display
        click(view, charPos(view, 0, 3));  // the echoed command
        QCOMPARE(spy.count(), 0);
        QVERIFY(!f.transcript.linkTimeAt(charPos(view, 1, 25)).has_value());
        QVERIFY(f.transcript.linkTimeAt(charPos(view, 1, 11)).has_value());
        QCOMPARE(*f.transcript.linkTimeAt(charPos(view, 3, 14)), 20000000000LL);
        click(view, charPos(view, 1, 11));
        QCOMPARE(spy.count(), 1);
    }

    void linkLooksLikeALink() {
        Fixture f;
        f.transcript.append(QString::fromLatin1(kText));
        QPlainTextEdit* view = f.transcript.textView();
        auto formatAt = [&](int block, int ch) {
            const QTextBlock b = view->document()->findBlockByNumber(block);
            QTextCursor c(b);
            c.setPosition(b.position() + ch + 1); // the format of the character before the position
            return c.charFormat();
        };
        const QTextCharFormat note = formatAt(1, 11);
        const QTextCharFormat warning = formatAt(3, 14);
        const QTextCharFormat error = formatAt(5, 12);
        const QTextCharFormat plain = formatAt(1, 25);
        QVERIFY(note.fontUnderline());
        QVERIFY(note.isAnchor());
        QVERIFY(!plain.fontUnderline());
        QVERIFY(!plain.isAnchor());
        QVERIFY(note.foreground() != warning.foreground());
        QVERIFY(warning.foreground() != error.foreground());
        QVERIFY(!formatAt(1, 1).fontUnderline()); // the "** Note:" part before the stamp
        // text appended later is not underlined
        f.transcript.append("after\n");
        const QTextBlock last = view->document()->findBlockByNumber(7);
        QCOMPARE(last.text(), QString("after"));
        QTextCursor c(last);
        c.setPosition(last.position() + 3);
        QVERIFY(!c.charFormat().fontUnderline());
    }

    void hoverShowsAHandCursor() {
        Fixture f;
        f.transcript.append(QString::fromLatin1(kText));
        QPlainTextEdit* view = f.transcript.textView();
        QTest::mouseMove(view->viewport(), charPos(view, 1, 11));
        QCOMPARE(view->viewport()->cursor().shape(), Qt::PointingHandCursor);
        QTest::mouseMove(view->viewport(), charPos(view, 1, 25));
        QCOMPARE(view->viewport()->cursor().shape(), Qt::IBeamCursor);
    }

    void lineSplitAcrossChunksIsRecognisedOnceWhenComplete() {
        Fixture f;
        f.transcript.append("** Note: 100");
        f.transcript.append("ns+0: spl");
        QCOMPARE(f.reports.size(), std::size_t(0)); // not complete yet
        f.transcript.append("it\n** Warn");
        f.transcript.append("ing: 7 us+2: second\n");
        QCOMPARE(f.reports.size(), std::size_t(2));
        QCOMPARE(std::get<2>(f.reports[0]), QString("split"));
        QCOMPARE(std::get<0>(f.reports[1]), 7000000000LL);
    }

    void prompt_and_garbage_do_not_matter() {
        Fixture f;
        f.transcript.append("% ");
        f.transcript.append("\n");
        f.transcript.append(QString::fromUtf8("** \xc3\xa4\xc3\xb6 garbage \xe2\x82\xac\n** Note: \n** Note: "
                                              "5\n** Note: 9999999999999999999999s+0: x\n"));
        QCOMPARE(f.reports.size(), std::size_t(0));
        f.transcript.appendNote("** Warning: GUI note without a time stamp");
        QCOMPARE(f.reports.size(), std::size_t(0));
        f.transcript.clear();
        QVERIFY(f.transcript.text().isEmpty());
    }

    void linesBeforeTheWaveDataExistDoNotCrash() {
        Fixture f(false); // no store yet
        f.transcript.append("** Note: 100ns+0: early\n");
        QCOMPARE(f.wave.eventMarkerCount(), std::size_t(1));
        QPlainTextEdit* view = f.transcript.textView();
        click(view, charPos(view, 0, 11)); // must not crash; there is no data to scroll
        QVERIFY(!f.wave.grab().isNull());
        f.wave.setStore(makeStore());
        QCOMPARE(f.wave.visibleEventGroups().size(), std::size_t(1));
        click(view, charPos(view, 0, 11));
        QCOMPARE(*f.wave.cursorTime(0), 100000000.0);
    }
};

QTEST_MAIN(TestTranscript)
#include "test_transcript.moc"
