// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPlainTextEdit>
#include <QSet>
#include <QString>
#include <QWidget>

class QLabel;
class QLineEdit;

namespace lvd::gui {

class VhdlHighlighter;

/// The text view inside SourcePane: line numbers in a gutter, a marker for the current line, and dots for
/// lines where processes are about to run.
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

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateGutter();
    void updateSelections();

    QWidget* gutter_ = nullptr;
    int current_ = 0;
    QSet<int> ready_;
};

/// Source dock: a read-only view of a VHDL file with line numbers, syntax colours, the line of the current
/// process and a find bar (Ctrl+F, F3 and Shift+F3 for next and previous).
class SourcePane : public QWidget {
    Q_OBJECT
public:
    explicit SourcePane(QWidget* parent = nullptr);

    /// Loads the file (relative names are looked up in the current directory) and scrolls to the line
    /// (1 based, 0 to stay at the top). On failure the old text is kept, lastError() says why, and false
    /// is returned. Opening the file that is already shown only moves to the line.
    bool openFile(const QString& path, int line = 0);
    QString filePath() const { return file_; }
    QString lastError() const { return error_; }

    /// Marks the line where the selected process stopped.
    void setCurrentLine(int line);
    int currentLine() const { return view_->currentLine(); }
    void setReadyLines(const QSet<int>& lines) { view_->setReadyLines(lines); }

    /// Finds the next (or previous) match after the cursor, wrapping around; case insensitive. The match is
    /// selected. Returns false if the text does not occur.
    bool find(const QString& text, bool backwards = false);
    void focusFind();

    SourceView* view() const noexcept { return view_; }
    QLineEdit* findEdit() const noexcept { return findEdit_; }
    int lineCount() const;

private:
    QString file_;
    QString error_;
    SourceView* view_ = nullptr;
    VhdlHighlighter* highlighter_ = nullptr;
    QLineEdit* findEdit_ = nullptr;
    QLabel* pathLabel_ = nullptr;
    QLabel* findState_ = nullptr;
};

} // namespace lvd::gui
