// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/console_input.hpp"

#include "app/console_text.hpp"

#include <QFontDatabase>
#include <QKeyEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>

namespace lvd::gui {

class PromptMargin : public QWidget {
public:
    explicit PromptMargin(ConsoleInput* input)
        : QWidget(input)
        , input_(input) {}

protected:
    void paintEvent(QPaintEvent* e) override { input_->paintPrompts(e); }

private:
    ConsoleInput* input_;
};

ConsoleInput::ConsoleInput(QWidget* parent)
    : QPlainTextEdit(parent) {
    setObjectName("consoleInput");
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabChangesFocus(false);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    document()->setDocumentMargin(2);
    margin_ = new PromptMargin(this);
    setViewportMargins(promptWidth(), 0, 0, 0);
    connect(document(), &QTextDocument::blockCountChanged, this, [this] { updateHeight(); });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& r, int dy) {
        if (dy != 0)
            margin_->scroll(0, dy);
        else
            margin_->update(0, r.y(), margin_->width(), r.height());
    });
    updateHeight();
}

int ConsoleInput::promptWidth() const {
    return fontMetrics().horizontalAdvance(QStringLiteral("% ")) + 4;
}

void ConsoleInput::setText(const QString& text) {
    setPlainText(text);
    moveCursor(QTextCursor::End);
}

QSize ConsoleInput::sizeHint() const {
    const int lines = qBound(1, document()->blockCount(), kMaxVisibleLines);
    const int h = lines * fontMetrics().lineSpacing() + 2 * static_cast<int>(document()->documentMargin()) +
                  2 * frameWidth() + 2;
    return {QPlainTextEdit::sizeHint().width(), h};
}

QSize ConsoleInput::minimumSizeHint() const {
    return {80, sizeHint().height()};
}

void ConsoleInput::updateHeight() {
    setFixedHeight(sizeHint().height());
    updateGeometry();
}

void ConsoleInput::resizeEvent(QResizeEvent* e) {
    QPlainTextEdit::resizeEvent(e);
    const QRect cr = contentsRect();
    margin_->setGeometry(QRect(cr.left(), cr.top(), promptWidth(), cr.height()));
}

void ConsoleInput::paintPrompts(QPaintEvent* e) {
    QPainter p(margin_);
    p.fillRect(e->rect(), palette().base());
    p.setPen(isEnabled() ? palette().color(QPalette::PlaceholderText)
                         : palette().color(QPalette::Disabled, QPalette::Text));
    p.setFont(font());
    QTextBlock block = firstVisibleBlock();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    while (block.isValid() && top <= e->rect().bottom()) {
        const int h = qRound(blockBoundingRect(block).height());
        if (block.isVisible())
            p.drawText(QRect(2, top, margin_->width() - 2, fontMetrics().height()),
                       Qt::AlignLeft | Qt::AlignTop,
                       block.blockNumber() == 0 ? QStringLiteral("%") : QStringLiteral(">"));
        block = block.next();
        top += h;
    }
}

void ConsoleInput::keyPressEvent(QKeyEvent* e) {
    const int key = e->key();
    const Qt::KeyboardModifiers mods = e->modifiers() & ~Qt::KeypadModifier;
    if (popup_ && popup_->isVisible() &&
        (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Escape || key == Qt::Key_Tab ||
         key == Qt::Key_Backtab || key == Qt::Key_Up || key == Qt::Key_Down)) {
        e->ignore(); // the popup handles these
        return;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        if (mods & Qt::ShiftModifier) {
            textCursor().insertText(QStringLiteral("\n"));
            return;
        }
        const QString t = toPlainText();
        if (tclCommandComplete(t)) {
            emit submitted(t);
        } else {
            // Unfinished: continue on the next line, at the end of the text wherever the cursor was.
            moveCursor(QTextCursor::End);
            textCursor().insertText(QStringLiteral("\n"));
            ensureCursorVisible();
        }
        return;
    }
    if (key == Qt::Key_Tab && mods == Qt::NoModifier) {
        emit completionRequested();
        return;
    }
    if (key == Qt::Key_R && mods == Qt::ControlModifier) {
        emit reverseSearchRequested();
        return;
    }
    if (key == Qt::Key_Escape) {
        emit escapePressed();
        return;
    }
    if (key == Qt::Key_Up && mods == Qt::NoModifier && textCursor().blockNumber() == 0) {
        emit historyStep(-1);
        return;
    }
    if (key == Qt::Key_Down && mods == Qt::NoModifier &&
        textCursor().blockNumber() == document()->blockCount() - 1) {
        emit historyStep(+1);
        return;
    }
    QPlainTextEdit::keyPressEvent(e);
}

} // namespace lvd::gui
