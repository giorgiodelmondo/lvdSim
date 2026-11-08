// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QStringList>
#include <QWidget>

#include <optional>

class QLineEdit;
class QPlainTextEdit;
class QTextBlock;

namespace lvd::gui {

/// Console of the simulation: everything the shell prints, and a command line with history (Up and Down).
/// The shell echoes what it is sent, so typed commands appear in the text by themselves.
///
/// HDL reports and assertions (`** Note: 100ns+0: text`, see sim/parsers/report_lines.hpp) are recognised when
/// their line is complete: the time stamp becomes an underlined link coloured by severity, clicking it emits
/// timeActivated(), and reportLogged() announces every report once (the wave window shows it as a marker).
/// Continuation lines and lines without a time stamp (Verilog $display) stay plain.
class TranscriptWidget : public QWidget {
    Q_OBJECT
public:
    explicit TranscriptWidget(QWidget* parent = nullptr);

    /// Appends text as it came out of the simulator; it may end in the middle of a line (the prompt).
    void append(const QString& text);
    /// A line the GUI itself wants to show (errors, warnings), always on its own line.
    void appendNote(const QString& text);
    QString text() const;
    void clear();
    QStringList history() const { return history_; }

    /// Enable or disable typing; a stopped simulation cannot take commands.
    void setInputEnabled(bool enabled);

    /// Time in femtoseconds of the report link under a point of the text viewport, if there is one.
    std::optional<qint64> linkTimeAt(const QPoint& viewportPos) const;
    /// The text area, for tests.
    QPlainTextEdit* textView() const { return view_; }

signals:
    void commandEntered(const QString& command);
    /// A time stamp was clicked: femtoseconds.
    void timeActivated(qint64 timeFs);
    /// A complete report line was seen. severity is 0 note, 1 warning, 2 error, 3 failure.
    void reportLogged(qint64 timeFs, int severity, const QString& message);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void submit();
    void decorateReport(const QTextBlock& block);
    void recall(int direction);

    QPlainTextEdit* view_ = nullptr;
    QLineEdit* input_ = nullptr;
    QStringList history_;
    int historyPos_ = 0; ///< index into history_, history_.size() means "the line being typed"
    QString draft_;
    QPoint pressPos_;
};

} // namespace lvd::gui
