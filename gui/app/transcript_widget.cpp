// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/transcript_widget.hpp"

#include "sim/parsers/report_lines.hpp"

#include <QFontDatabase>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QToolTip>
#include <QVBoxLayout>

namespace lvd::gui {

namespace {

const QString kLinkScheme = QStringLiteral("lvd-time:");

QColor linkColor(ReportSeverity s, bool darkBackground) {
    switch (s) {
        case ReportSeverity::Note: return darkBackground ? QColor(0x5f, 0xc8, 0xff) : QColor(0x0b, 0x6f, 0xb0);
        case ReportSeverity::Warning: return darkBackground ? QColor(0xff, 0xb4, 0x20) : QColor(0xa8, 0x60, 0x00);
        case ReportSeverity::Error: return darkBackground ? QColor(0xff, 0x6a, 0x6a) : QColor(0xc4, 0x1c, 0x1c);
        case ReportSeverity::Failure: return darkBackground ? QColor(0xff, 0x55, 0xe0) : QColor(0xa8, 0x0c, 0x94);
    }
    return QColor();
}

}  // namespace

TranscriptWidget::TranscriptWidget(QWidget* parent)
    : QWidget(parent) {
    view_ = new QPlainTextEdit(this);
    view_->setReadOnly(true);
    view_->setMaximumBlockCount(20000);
    view_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    view_->setLineWrapMode(QPlainTextEdit::NoWrap);
    view_->setUndoRedoEnabled(false);  // the text is a log; formatting a link must not fill an undo stack
    view_->viewport()->setMouseTracking(true);
    view_->viewport()->installEventFilter(this);

    input_ = new QLineEdit(this);
    input_->setPlaceholderText(
        tr("Command (Tcl and simulator commands: run 100 ns, add wave -r /*, examine /sig ...)"));
    input_->setFont(view_->font());
    input_->installEventFilter(this);
    connect(input_, &QLineEdit::returnPressed, this, [this] { submit(); });

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(view_, 1);
    layout->addWidget(input_);
}

void TranscriptWidget::append(const QString& text) {
    if (text.isEmpty())
        return;
    QScrollBar* bar = view_->verticalScrollBar();
    const bool atBottom = bar->value() >= bar->maximum() - 2;

    QTextCursor c(view_->document());
    c.movePosition(QTextCursor::End);
    // insertText does not understand newlines as block breaks in a plain text edit; split them.
    const QStringList lines = text.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        if (i > 0)
            c.insertBlock(c.blockFormat(), QTextCharFormat());
        c.insertText(lines[i]);
        if (i + 1 < lines.size())  // the line is complete now
            decorateReport(c.block());
    }
    if (atBottom)
        bar->setValue(bar->maximum());
}

void TranscriptWidget::appendNote(const QString& text) {
    if (!view_->document()->isEmpty() && !view_->toPlainText().endsWith('\n'))
        append("\n");
    append(text + "\n");
}

void TranscriptWidget::decorateReport(const QTextBlock& block) {
    const QString line = block.text();
    if (line.size() < 12 || !line.contains(QStringLiteral("** ")))
        return;
    const auto r = parseReportLine(line.toStdString());
    if (!r)
        return;
    // Everything before the time stamp is ASCII, so the offsets in the UTF-8 string are character offsets.
    const bool dark = view_->palette().base().color().lightness() < 128;
    QTextCharFormat fmt;
    fmt.setAnchor(true);
    fmt.setAnchorHref(kLinkScheme + QString::number(r->timeFs) + ':' + QString::number(r->delta));
    fmt.setForeground(linkColor(r->severity, dark));
    fmt.setFontUnderline(true);
    QTextCursor lc(block);
    lc.setPosition(block.position() + static_cast<int>(r->stampBegin));
    lc.setPosition(block.position() + static_cast<int>(r->stampEnd), QTextCursor::KeepAnchor);
    lc.mergeCharFormat(fmt);
    emit reportLogged(r->timeFs, static_cast<int>(r->severity), QString::fromStdString(r->message));
}

std::optional<qint64> TranscriptWidget::linkTimeAt(const QPoint& viewportPos) const {
    const QString href = view_->anchorAt(viewportPos);
    if (!href.startsWith(kLinkScheme))
        return std::nullopt;
    const QStringList parts = href.mid(kLinkScheme.size()).split(':');
    bool ok = false;
    const qint64 fs = parts.value(0).toLongLong(&ok);
    return ok ? std::optional<qint64>(fs) : std::nullopt;
}

QString TranscriptWidget::text() const {
    return view_->toPlainText();
}

void TranscriptWidget::clear() {
    view_->clear();
}

void TranscriptWidget::setInputEnabled(bool enabled) {
    input_->setEnabled(enabled);
    if (enabled)
        input_->setFocus();
}

void TranscriptWidget::submit() {
    const QString cmd = input_->text();
    input_->clear();
    if (!cmd.trimmed().isEmpty() && (history_.isEmpty() || history_.last() != cmd))
        history_ << cmd;
    historyPos_ = static_cast<int>(history_.size());
    draft_.clear();
    emit commandEntered(cmd);
}

void TranscriptWidget::recall(int direction) {
    if (history_.isEmpty())
        return;
    if (historyPos_ == history_.size())
        draft_ = input_->text();
    historyPos_ = qBound(0, historyPos_ + direction, static_cast<int>(history_.size()));
    input_->setText(historyPos_ == history_.size() ? draft_ : history_[historyPos_]);
}

bool TranscriptWidget::eventFilter(QObject* watched, QEvent* event) {
    if (watched == view_->viewport()) {
        switch (event->type()) {
            case QEvent::MouseButtonPress:
                pressPos_ = static_cast<QMouseEvent*>(event)->position().toPoint();
                break;
            case QEvent::MouseButtonRelease: {
                const auto* me = static_cast<QMouseEvent*>(event);
                const QPoint pos = me->position().toPoint();
                // A click, not the end of a selection drag.
                if (me->button() == Qt::LeftButton && (pos - pressPos_).manhattanLength() < 4 &&
                    !view_->textCursor().hasSelection())
                    if (const auto fs = linkTimeAt(pos))
                        emit timeActivated(*fs);
                break;
            }
            case QEvent::MouseMove:
                view_->viewport()->setCursor(linkTimeAt(static_cast<QMouseEvent*>(event)->position().toPoint())
                                                 ? Qt::PointingHandCursor
                                                 : Qt::IBeamCursor);
                break;
            case QEvent::ToolTip: {
                const auto* he = static_cast<QHelpEvent*>(event);
                const QString href = view_->anchorAt(he->pos());
                if (href.startsWith(kLinkScheme)) {
                    const QStringList parts = href.mid(kLinkScheme.size()).split(':');
                    QToolTip::showText(he->globalPos(),
                                       tr("%1 fs (delta cycle %2). Click to move cursor 1 here.")
                                           .arg(parts.value(0), parts.value(1)),
                                       view_->viewport());
                } else {
                    QToolTip::hideText();
                }
                return true;
            }
            default: break;
        }
    }
    if (watched == input_ && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Up) {
            recall(-1);
            return true;
        }
        if (key == Qt::Key_Down) {
            recall(+1);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace lvd::gui
