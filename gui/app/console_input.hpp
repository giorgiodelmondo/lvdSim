// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPlainTextEdit>
#include <QPointer>

namespace lvd::gui {

/// The command line of the console. One line high while a command fits on one line; it grows (up to
/// kMaxVisibleLines) when Shift+Enter inserts a newline or when Enter is pressed on an unfinished command
/// (unbalanced braces, brackets or quotes, or a trailing backslash): the command continues on the next line.
/// The left margin shows the prompt "%" on the first line and ">" on continuation lines.
///
/// Keys it reports instead of handling: Enter on a complete command (submitted), Up on the first line and
/// Down on the last line (historyStep), Tab (completionRequested), Ctrl+R (reverseSearchRequested), Escape.
class ConsoleInput : public QPlainTextEdit {
    Q_OBJECT
public:
    static constexpr int kMaxVisibleLines = 8;

    explicit ConsoleInput(QWidget* parent = nullptr);

    QString text() const { return toPlainText(); }
    /// Replaces the text and puts the cursor at its end.
    void setText(const QString& text);

    /// While this popup (of a completer) is visible, Enter, Tab, Escape, Up and Down are left to it.
    void setCompletionPopup(QWidget* popup) noexcept { popup_ = popup; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    /// Width of the prompt margin, in pixels.
    int promptWidth() const;

signals:
    void submitted(const QString& command);
    void historyStep(int direction); ///< -1 older (Up), +1 newer (Down)
    void completionRequested();
    void reverseSearchRequested();
    void escapePressed();

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    friend class PromptMargin;
    void paintPrompts(QPaintEvent* e);
    void updateHeight();

    QWidget* margin_ = nullptr;
    QPointer<QWidget> popup_;
};

} // namespace lvd::gui
