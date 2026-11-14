// SPDX-License-Identifier: GPL-3.0-or-later
// Appending simulator output to the transcript: 1M lines in chunks of about 4 KB (what one read of the
// shell's output brings), with events processed every 16 chunks as the GUI would between reads. Reports the
// total time, lines per second and the slowest single append (the part of a frame it takes). Compares with
// the previous appender (one block per line), reproduced here on a bare QPlainTextEdit.
//   bench_transcript [lines] [--no-legacy] [--plain]   (--plain: no reports, links or coloured lines)
#include "app/transcript_widget.hpp"
#include "sim/parsers/report_lines.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QTextCursor>

#include <cstdio>
#include <vector>

using namespace lvd::gui;

namespace {

std::vector<QString> makeChunks(int lines, bool plain) {
    std::vector<QString> chunks;
    QString chunk;
    for (int i = 0; i < lines; ++i) {
        switch (plain ? 9 : i % 10) {
        case 0:
            chunk += QStringLiteral("** Note: %1ns+0: sample %1 checked\n").arg(i);
            break;
        case 1:
            chunk += QStringLiteral("   Process :tb:_p1 at tb.vhd:%1\n").arg(17 + i % 50);
            break;
        case 2:
            chunk += QStringLiteral("% examine /tb/cnt\n");
            break;
        case 3:
            chunk += QStringLiteral("** Warning: (lvdsim) value %1 out of range\n").arg(i);
            break;
        default:
            chunk +=
                QStringLiteral("cnt=%1 state=RUNNING addr=0x%2\n").arg(i).arg(i, 8, 16, QLatin1Char('0'));
        }
        if (chunk.size() >= 4096) {
            chunks.push_back(chunk);
            chunk.clear();
        }
    }
    if (!chunk.isEmpty())
        chunks.push_back(chunk);
    return chunks;
}

struct Result {
    double totalMs = 0;
    double maxAppendMs = 0;
};

template <class Append>
Result run(const std::vector<QString>& chunks, Append&& append) {
    Result r;
    QElapsedTimer total;
    total.start();
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        QElapsedTimer one;
        one.start();
        append(chunks[i]);
        r.maxAppendMs = std::max(r.maxAppendMs, one.nsecsElapsed() / 1e6);
        if (i % 16 == 15)
            QCoreApplication::processEvents();
    }
    QCoreApplication::processEvents();
    r.totalMs = total.nsecsElapsed() / 1e6;
    return r;
}

// The appender before the console work: one insertBlock + insertText per line, each line parsed when
// complete.
void legacyAppend(QPlainTextEdit* view, const QString& text) {
    QTextCursor c(view->document());
    c.movePosition(QTextCursor::End);
    const QStringList lines = text.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        if (i > 0)
            c.insertBlock(c.blockFormat(), QTextCharFormat());
        c.insertText(lines[i]);
        if (i + 1 < lines.size()) {
            const QString line = c.block().text();
            if (line.size() >= 12 && line.contains(QStringLiteral("** ")))
                (void)parseReportLine(line.toStdString());
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    int lines = 1000000;
    bool legacy = true;
    bool plain = false;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == "--no-legacy")
            legacy = false;
        else if (a == "--plain")
            plain = true;
        else if (a.toInt() > 0)
            lines = a.toInt();
    }
    const std::vector<QString> chunks = makeChunks(lines, plain);
    std::printf("bench_transcript: %d lines in %zu chunks, cap %d lines\n", lines, chunks.size(),
                TranscriptWidget::kDefaultMaximumLines);

    {
        TranscriptWidget t;
        t.resize(900, 400);
        t.show();
        QCoreApplication::processEvents();
        int reports = 0;
        QObject::connect(&t, &TranscriptWidget::reportLogged, [&reports] { ++reports; });
        const Result r = run(chunks, [&t](const QString& c) { t.append(c); });
        std::printf("  TranscriptWidget::append  total %8.1f ms  %8.0f klines/s  slowest append %6.2f ms  "
                    "(%d reports, "
                    "%d lines kept)\n",
                    r.totalMs, lines / r.totalMs, r.maxAppendMs, reports,
                    t.textView()->document()->blockCount());
    }
    if (legacy) {
        QPlainTextEdit v;
        v.setReadOnly(true);
        v.setMaximumBlockCount(TranscriptWidget::kDefaultMaximumLines);
        v.setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        v.setLineWrapMode(QPlainTextEdit::NoWrap);
        v.setUndoRedoEnabled(false);
        v.resize(900, 400);
        v.show();
        QCoreApplication::processEvents();
        const Result r = run(chunks, [&v](const QString& c) { legacyAppend(&v, c); });
        std::printf("  previous appender         total %8.1f ms  %8.0f klines/s  slowest append %6.2f ms\n",
                    r.totalMs, lines / r.totalMs, r.maxAppendMs);
    }
    return 0;
}
