// SPDX-License-Identifier: GPL-3.0-or-later
#include "sim/output_filter.hpp"

#include <QtTest>

using namespace lvd::gui;

namespace {
const QByteArray kPrompt("\x01\x02%\x01\x02 ", 6);
}

class TestOutputFilter : public QObject {
    Q_OBJECT
private slots:
    void plainTextAndLineEndings() {
        OutputFilter f;
        const auto o = f.feed("hello\r\nworld\r\n");
        QCOMPARE(o.text, QString("hello\nworld\n"));
        QCOMPARE(o.prompts, 0);
        QVERIFY(o.notices.empty());
    }

    void promptIsCountedAndCleaned() {
        OutputFilter f;
        const auto o = f.feed("banner\r\n\r\n" + kPrompt);
        QCOMPARE(o.text, QString("banner\n\n% ")); // shown at once although there is no newline
        QCOMPARE(o.prompts, 1);

        const auto many = f.feed("run 1 ns\r\n" + kPrompt + "x\r\n" + kPrompt);
        QCOMPARE(many.prompts, 2);
        QCOMPARE(many.text, QString("run 1 ns\n% x\n% "));
    }

    void aPromptSplitAcrossReads() {
        OutputFilter f;
        QByteArray all = "text\r\n" + kPrompt;
        for (int cut = 1; cut < kPrompt.size(); ++cut) {
            OutputFilter g;
            const QByteArray head = all.left(all.size() - cut), tail = all.right(cut);
            const auto a = g.feed(head);
            const auto b = g.feed(tail);
            QCOMPARE(a.prompts + b.prompts, 1);
            QCOMPARE(a.text + b.text, QString("text\n% "));
        }
    }

    void noticesAreExtractedAndHidden() {
        OutputFilter f;
        const auto o = f.feed("before\r\n@@lvd add wave -r /*\r\nafter\r\n");
        QCOMPARE(o.text, QString("before\nafter\n"));
        QCOMPARE(int(o.notices.size()), 1);
        QCOMPARE(o.notices[0], (QStringList{"add", "wave", "-r", "/*"}));
    }

    void noticeWordsFollowTclQuoting() {
        OutputFilter f;
        const auto o = f.feed("@@lvd add wave -label {Two words} /a\r\n");
        QCOMPARE(o.notices[0], (QStringList{"add", "wave", "-label", "Two words", "/a"}));
    }

    void aNoticeSplitAcrossReads() {
        const QByteArray line = "@@lvd add wave /sig\r\n";
        for (int cut = 1; cut < line.size(); ++cut) {
            OutputFilter f;
            const auto a = f.feed("x\r\n" + line.left(cut));
            const auto b = f.feed(line.mid(cut) + "y\r\n");
            QCOMPARE(a.text + b.text, QString("x\ny\n"));
            QCOMPARE(int(a.notices.size() + b.notices.size()), 1);
        }
    }

    void similarLinesAreKept() {
        OutputFilter f;
        const auto o = f.feed("@@lvdx not a notice\r\n@ at sign\r\n");
        QCOMPARE(o.text, QString("@@lvdx not a notice\n@ at sign\n"));
        QVERIFY(o.notices.empty());
    }

    void unfinishedTextIsShownImmediately() {
        OutputFilter f;
        QCOMPARE(f.feed("partial").text, QString("partial"));
        QCOMPARE(f.feed(" line\r\n").text, QString(" line\n"));
        // but something that may still turn out to be a notice waits
        QCOMPARE(f.feed("@@lv").text, QString());
        QCOMPARE(f.feed("d add wave /a\r\n").text, QString());
    }

    void aNoticeFollowsThePromptOnTheSameLine() {
        // The shell prints its prompt, then the command runs and prints, then the command is echoed.
        OutputFilter f;
        const auto o = f.feed(kPrompt + "@@lvd add wave /a\r\nsource x.do\r\n" + kPrompt);
        QCOMPARE(o.text, QString("% source x.do\n% ")); // the prompt stays, the notice is gone
        QCOMPARE(int(o.notices.size()), 1);
        QCOMPARE(o.notices[0], (QStringList{"add", "wave", "/a"}));
        QCOMPARE(o.prompts, 2);
    }

    void severalNoticesAfterOnePrompt() {
        OutputFilter f;
        const auto o = f.feed(kPrompt + "@@lvd add wave /a\r\n@@lvd add wave /b\r\necho\r\n");
        QCOMPARE(int(o.notices.size()), 2);
        QCOMPARE(o.text, QString("% echo\n"));
    }

    void aNoticeAfterAPromptSplitAcrossReads() {
        const QByteArray all = kPrompt + "@@lvd add wave /sig\r\ndone\r\n";
        for (int cut = 1; cut < all.size(); ++cut) {
            OutputFilter f;
            const auto a = f.feed(all.left(cut));
            const auto b = f.feed(all.mid(cut));
            QCOMPARE(int(a.notices.size() + b.notices.size()), 1);
            QCOMPARE(a.text + b.text, QString("% done\n"));
            QCOMPARE(a.prompts + b.prompts, 1);
        }
    }

    void aBarePromptIsStillShownAtOnce() {
        OutputFilter f;
        QCOMPARE(f.feed(kPrompt).text, QString("% "));
        QCOMPARE(f.feed(kPrompt + kPrompt).text, QString("% % "));
    }

    void aJsonReplyIsCutOutAndNumbered() {
        OutputFilter f;
        // prompt 1, echo of command 0, its reply, prompt 2
        const auto o = f.feed(kPrompt + "run -json 1 ns\r\n{\"v\":1,\"now\":1}\r\n" + kPrompt);
        QCOMPARE(o.prompts, 2);
        QCOMPARE(o.text, QString("% run -json 1 ns\n% "));
        QCOMPARE(o.replies.size(), std::size_t{1});
        QCOMPARE(o.replies[0].text, QString("{\"v\":1,\"now\":1}"));
        QCOMPARE(o.replies[0].prompt, 1); // command index prompt - 1 = 0
        QCOMPARE(o.replies[0].textOffset, qsizetype(QString("% run -json 1 ns\n").size()));

        // the next command's reply is numbered from the running prompt count, across chunks
        const auto p = f.feed(QByteArray("step -json\r\n{\"v\":1}\r\n") + kPrompt);
        QCOMPARE(p.replies.size(), std::size_t{1});
        QCOMPARE(p.replies[0].prompt, 2);
    }

    void aJsonReplySplitAnywhere() {
        const QByteArray all = kPrompt + "processes -json\r\n{\"v\":1,\"processes\":[]}\r\n" + kPrompt;
        for (int cut = 1; cut < all.size(); ++cut) {
            OutputFilter f;
            const auto a = f.feed(all.left(cut));
            const auto b = f.feed(all.mid(cut));
            QCOMPARE(a.replies.size() + b.replies.size(), std::size_t{1});
            const auto& r = a.replies.empty() ? b.replies[0] : a.replies[0];
            QCOMPARE(r.text, QString("{\"v\":1,\"processes\":[]}"));
            QCOMPARE(r.prompt, 1);
            QCOMPARE(a.text + b.text, QString("% processes -json\n% "));
            QCOMPARE(a.prompts + b.prompts, 2);
        }
    }

    void jsonLookalikesStayInTheText() {
        OutputFilter f;
        const auto o = f.feed("{\"x\":1}\r\n{ \"v\":1}\r\n");
        QVERIFY(o.replies.empty());
        QCOMPARE(o.text, QString("{\"x\":1}\n{ \"v\":1}\n"));
    }

    void utf8Survives() {
        OutputFilter f;
        QCOMPARE(f.feed(QString::fromUtf8("caf\xc3\xa9\r\n").toUtf8()).text,
                 QString::fromUtf8("caf\xc3\xa9\n"));
    }
};

QTEST_APPLESS_MAIN(TestOutputFilter)
#include "test_output_filter.moc"
