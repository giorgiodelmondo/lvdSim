// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/declaration_locator.hpp"
#include "app/line_breakpoints.hpp"

#include <QHash>
#include <QPlainTextEdit>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QWidget>

class QLabel;
class QLineEdit;
class QToolButton;

namespace lvd::gui {

class VhdlHighlighter;

/// How a breakpoint is drawn in the gutter.
enum class BreakpointMark {
    Enabled,    ///< red dot
    Disabled,   ///< red ring
    Unverified, ///< grey dot: refused by the kernel, or a line without a statement
};

/// Where the debugger stopped, as drawn in the gutter: the innermost frame (yellow arrow) or a caller frame
/// chosen in the Call Stack (green arrow).
enum class ExecutionMark { None, Innermost, Caller };

/// Coverage of one source line as the gutter shows it (see CoveragePane): the count in a coloured cell
/// (green: all coverage items on the line hit, amber: some, red: none) and a tinted line for partly or not
/// covered lines.
struct CoverageMark {
    qint64 hits = 0; ///< times the statements of the line ran (or the largest count of its other items)
    int items = 0;   ///< coverage items (bins) on the line
    int hitItems = 0;
    QString tip; ///< what is not covered, for the tool tip
    bool covered() const noexcept { return hitItems == items; }
    bool uncovered() const noexcept { return hitItems == 0; }
};

/// The text view inside SourcePane: line numbers in a gutter, a marker for the current line, dots for
/// lines where processes are about to run, breakpoints and the debugger's execution point. A click in the
/// gutter asks for a breakpoint toggle.
class SourceView : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit SourceView(QWidget* parent = nullptr);

    int gutterWidth() const;
    void paintGutter(QPaintEvent* event);
    void setCurrentLine(int line); ///< 1 based, 0 for none
    int currentLine() const noexcept { return current_; }
    void setReadyLines(const QSet<int>& lines);
    QSet<int> readyLines() const { return ready_; }
    void showLine(int line); ///< scroll so that the line is in the middle, no marker

    void setBreakpointMarks(const QHash<int, BreakpointMark>& marks, const QHash<int, QString>& tips = {});
    QHash<int, BreakpointMark> breakpointMarks() const { return marks_; }
    /// The debugger's execution line (highlighted, arrow in the gutter), 0 for none; scrolls to it.
    void setExecutionLine(int line, ExecutionMark mark);
    int executionLine() const noexcept { return exec_; }
    ExecutionMark executionMark() const noexcept { return execMark_; }

    /// Coverage annotations by line (empty: none, and no coverage column in the gutter).
    void setCoverageMarks(const QHash<int, CoverageMark>& marks);
    QHash<int, CoverageMark> coverageMarks() const { return coverage_; }
    /// Width of the coverage column at the right of the gutter, 0 without coverage.
    int coverageColumnWidth() const;
    /// The text of a hit count as the gutter shows it: 7, 1234, 12.3k, 4.5M.
    static QString hitText(qint64 hits);

    /// The line at a y position of the gutter (or the text), 0 below the last line.
    int lineAt(int y) const;
    /// What a click at y in the gutter does: emits gutterClicked for that line.
    void clickGutter(int y);
    QString gutterToolTip(int y) const;
    QWidget* gutter() const noexcept { return gutter_; }

    /// Highlights a line for a moment (after a jump to a declaration), 0 for none.
    void flashLine(int line, int milliseconds = 900);
    int flashingLine() const noexcept { return flash_; }

signals:
    void gutterClicked(int line);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateGutter();
    void updateSelections();

    QWidget* gutter_ = nullptr;
    int current_ = 0;
    QSet<int> ready_;
    QHash<int, BreakpointMark> marks_;
    QHash<int, QString> tips_;
    int exec_ = 0;
    ExecutionMark execMark_ = ExecutionMark::None;
    QHash<int, CoverageMark> coverage_;
    int flash_ = 0;
    QTimer flashTimer_;
};

/// Source dock: a read-only view of a VHDL file with line numbers, syntax colours, the line of the current
/// process, line breakpoints, the debugger's execution point and a find bar (Ctrl+F, F3 and Shift+F3 for
/// next and previous).
///
/// Jumps (openAt(): View Declaration, View Definition, View Instantiation, View Source) are kept in a history
/// that Back and Forward walk (toolbar buttons, Alt+Left and Alt+Right). Each entry remembers the line the
/// cursor was on when the view moved away from it.
class SourcePane : public QWidget {
    Q_OBJECT
public:
    explicit SourcePane(QWidget* parent = nullptr);

    /// Loads the file (relative names are looked up in the current directory) and scrolls to the line
    /// (1 based, 0 to stay at the top). On failure the old text is kept, lastError() says why, and false
    /// is returned. Opening the file that is already shown only moves to the line.
    bool openFile(const QString& path, int line = 0);
    QString filePath() const { return file_; }

    /// Opens the file at the line, selects the identifier (at the column if it is there, else its first
    /// occurrence on the line), flashes the line and records the jump in the history. False (lastError())
    /// if the file cannot be read; the history is then unchanged.
    bool openAt(const SourceLocation& location);
    bool goBack();
    bool goForward();
    bool canGoBack() const noexcept { return historyPos_ > 0; }
    bool canGoForward() const noexcept { return historyPos_ >= 0 && historyPos_ + 1 < history_.size(); }
    int historySize() const noexcept { return static_cast<int>(history_.size()); }
    int historyIndex() const noexcept { return historyPos_; }
    QToolButton* backButton() const noexcept { return back_; }
    QToolButton* forwardButton() const noexcept { return forward_; }
    /// The line of the text cursor (1 based).
    int cursorLine() const;
    QString lastError() const { return error_; }

    /// Marks the line where the selected process stopped.
    void setCurrentLine(int line);
    int currentLine() const { return view_->currentLine(); }
    void setReadyLines(const QSet<int>& lines) { view_->setReadyLines(lines); }

    /// All line breakpoints (of every file); the gutter shows those of the file on view.
    void setBreakpoints(const QVector<LineBreakpoint>& breakpoints);
    /// The debugger's execution point: shown when that file is on view (also after opening it later).
    void setExecutionPoint(const QString& file, int line, ExecutionMark mark);
    void clearExecutionPoint() { setExecutionPoint({}, 0, ExecutionMark::None); }
    int executionLine() const { return view_->executionLine(); }

    /// Coverage annotations by file (absolute paths) and line; the gutter shows those of the file on view,
    /// also for a file opened later. An empty map removes them.
    void setCoverage(const QHash<QString, QHash<int, CoverageMark>>& byFile);
    void clearCoverage() { setCoverage({}); }
    bool hasCoverage() const noexcept { return !coverage_.isEmpty(); }

    /// Finds the next (or previous) match after the cursor, wrapping around; case insensitive. The match is
    /// selected. Returns false if the text does not occur.
    bool find(const QString& text, bool backwards = false);
    void focusFind();

    SourceView* view() const noexcept { return view_; }
    QLineEdit* findEdit() const noexcept { return findEdit_; }
    int lineCount() const;

    /// True when the line has no VHDL or Verilog statement (empty or only a comment).
    bool lineIsBlank(int line) const;

signals:
    /// A click in the gutter of the file on view.
    void breakpointToggleRequested(const QString& file, int line);
    void historyChanged();

private:
    struct Place {
        QString file;
        int line = 0;
    };
    void applyMarks();
    void selectIdentifier(int line, int column, const QString& name);
    void rememberCursor();
    bool showPlace(const Place& place);
    void updateHistoryButtons();

    QString file_;
    QString error_;
    SourceView* view_ = nullptr;
    VhdlHighlighter* highlighter_ = nullptr;
    QLineEdit* findEdit_ = nullptr;
    QLabel* pathLabel_ = nullptr;
    QLabel* findState_ = nullptr;
    QToolButton* back_ = nullptr;
    QToolButton* forward_ = nullptr;
    QVector<Place> history_;
    int historyPos_ = -1;
    QVector<LineBreakpoint> breakpoints_;
    QString execFile_;
    QHash<QString, QHash<int, CoverageMark>> coverage_;
    int execLine_ = 0;
    ExecutionMark execMark_ = ExecutionMark::None;
};

} // namespace lvd::gui
