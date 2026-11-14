// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/console_completer.hpp"
#include "app/console_history.hpp"

#include <QPointer>
#include <QStringList>
#include <QWidget>

#include <optional>

class QAction;
class QCheckBox;
class QCompleter;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QLineEdit;
class QMimeData;
class QPlainTextEdit;
class QStandardItemModel;
class QTextBlock;

namespace lvd::gui {

class ConsoleInput;

/// Console of the simulation: everything the shell prints, and a command line.
/// The shell echoes what it is sent, so typed commands appear in the text by themselves.
///
/// Transcript: lines starting with "** Error", "** Warning", "** Note" are coloured; HDL reports and
/// assertions
/// (`** Note: 100ns+0: text`, see sim/parsers/report_lines.hpp) get their time stamp as a link
/// (timeActivated) and are announced once (reportLogged); `file.vhd:12` and Tcl traces `(file "x.do" line 3)`
/// are links too (sourceActivated). Find (Ctrl+F, F3, Shift+F3), Save, Clear and Copy without prompts are in
/// the context menu and in actions() for the window's menu. The text is capped at maximumLines() lines
/// (oldest dropped).
///
/// Command line (ConsoleInput): history with Up/Down and Ctrl+R (search), kept by the owner through
/// historyChanged(); Tab completion (ConsoleCompleter) with a popup when there is more than one candidate;
/// multi-line commands (Shift+Enter, or Enter on an unfinished command). A .do or .tcl file dropped on the
/// console asks for it to be run (doFileDropped).
class TranscriptWidget : public QWidget {
    Q_OBJECT
public:
    static constexpr int kDefaultMaximumLines = 20000;

    explicit TranscriptWidget(QWidget* parent = nullptr);

    /// Appends text as it came out of the simulator; it may end in the middle of a line (the prompt).
    void append(const QString& text);
    /// A line the GUI itself wants to show (errors, warnings), always on its own line.
    void appendNote(const QString& text);
    QString text() const;
    void clear();

    /// The transcript text (or only the selected part) without the shell prompts at line starts.
    QString textWithoutPrompts(bool selectionOnly = false) const;
    /// Copies textWithoutPrompts (the selection if there is one, else everything) to the clipboard.
    void copyWithoutPrompts();
    /// Writes the transcript text to a file (UTF-8). False if it cannot be written.
    bool saveTranscript(const QString& path) const;
    /// Asks for a file name, then saveTranscript.
    void saveTranscriptDialog();

    void setMaximumLines(int lines);
    int maximumLines() const;

    // ---- find in the transcript ---------------------------------------------------------------------------
    /// Shows the find bar and focuses it (Ctrl+F while the transcript has the focus).
    void showFindBar();
    /// Selects the next (or previous) occurrence of the text after (before) the cursor, wrapping around.
    bool find(const QString& text, bool backward = false);
    QLineEdit* findEdit() const noexcept { return findEdit_; }
    QWidget* findBar() const noexcept { return findBar_; }

    // ---- command line -------------------------------------------------------------------------------------
    QStringList history() const { return history_.entries(); }
    void setHistory(const QStringList& entries);
    void setHistoryLimit(int limit);
    void clearHistory();
    /// Transcript > Command history: shows the history dialog (created on first use).
    ConsoleHistoryDialog* showHistoryDialog();

    /// Enable or disable typing; a stopped simulation cannot take commands.
    void setInputEnabled(bool enabled);
    ConsoleInput* input() const noexcept { return input_; }
    /// Puts a command into the command line (to edit it before Enter).
    void setCommandText(const QString& text);
    /// Runs a command as if typed (history, commandEntered).
    void submitCommand(const QString& command);

    /// Ctrl+R: starts (or continues to older matches of) the search in the history.
    void startHistorySearch();
    QLineEdit* historySearchEdit() const noexcept { return searchEdit_; }
    QWidget* historySearchBar() const noexcept { return searchBar_; }

    // ---- completion ---------------------------------------------------------------------------------------
    ConsoleCompleter& completer() noexcept { return completer_; }
    /// The shell's command list arrived (answer to shellCommandsWanted); a Tab waiting for it is redone.
    void setShellCommands(const QStringList& commands);
    /// Tab: completes the word at the cursor; with several candidates the common part is inserted and the
    /// popup lists them.
    void completeAtCursor();
    bool completionPopupVisible() const;
    /// The candidates of the popup, as the popup shows them (labels).
    QStringList completionLabels() const;
    QCompleter* completionPopupCompleter() const noexcept { return popupCompleter_; }

    /// Time in femtoseconds of the report link under a point of the text viewport, if there is one.
    std::optional<qint64> linkTimeAt(const QPoint& viewportPos) const;
    /// File and line of the source link under a point of the text viewport.
    std::optional<std::pair<QString, int>> sourceLinkAt(const QPoint& viewportPos) const;
    /// The text area, for tests.
    QPlainTextEdit* textView() const noexcept { return view_; }

    QAction* findAction() const noexcept { return findAction_; }
    QAction* clearAction() const noexcept { return clearAction_; }
    QAction* saveAction() const noexcept { return saveAction_; }
    QAction* copyWithoutPromptsAction() const noexcept { return copyPlainAction_; }
    QAction* historyAction() const noexcept { return historyAction_; }

    /// Dialogs (save) make no sense in automated runs.
    void setDialogsEnabled(bool enabled) noexcept { dialogs_ = enabled; }

signals:
    void commandEntered(const QString& command);
    /// A time stamp was clicked: femtoseconds.
    void timeActivated(qint64 timeFs);
    /// A file:line reference was clicked (file as printed; may be relative).
    void sourceActivated(const QString& file, int line);
    /// A complete report line was seen. severity is 0 note, 1 warning, 2 error, 3 failure.
    void reportLogged(qint64 timeFs, int severity, const QString& message);
    /// The command history changed (a command was entered, or it was cleared): time to store it.
    void historyChanged();
    /// Completion of a command name would like the shell's `info commands`; answer with setShellCommands.
    void shellCommandsWanted();
    /// A .do or .tcl file was dropped on the console.
    void doFileDropped(const QString& path);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    void buildFindBar();
    void buildSearchBar();
    void buildActions();
    void submit(const QString& command);
    void decorateLine(const QTextBlock& block);
    void recall(int direction);
    void searchHistory(bool older);
    void endHistorySearch(bool accept, bool run);
    void showCompletion(bool fromTab);
    void insertCompletion(const QString& candidate);
    void hideCompletion();
    static QStringList doFilesIn(const QMimeData* mime);

    QPlainTextEdit* view_ = nullptr;
    ConsoleInput* input_ = nullptr;
    ConsoleHistory history_;
    qsizetype historyPos_ = 0; ///< index into the history, size() means "the line being typed"
    QString draft_;
    QPoint pressPos_;
    bool dialogs_ = true;

    QWidget* findBar_ = nullptr;
    QLineEdit* findEdit_ = nullptr;
    QLabel* findState_ = nullptr;
    QCheckBox* findCase_ = nullptr;

    QWidget* searchBar_ = nullptr;
    QLineEdit* searchEdit_ = nullptr;
    QLabel* searchState_ = nullptr;
    qsizetype searchIndex_ = -1;
    QString searchDraft_;

    ConsoleCompleter completer_;
    QCompleter* popupCompleter_ = nullptr;
    QStandardItemModel* popupModel_ = nullptr;
    bool waitingForCommands_ = false; ///< shellCommandsWanted was emitted, no answer yet
    QString tabText_;                 ///< command line text of a Tab that waits for the shell's command list
    int tabCursor_ = -1;
    int popupStart_ = -1; ///< start of the word the popup completes

    QAction* findAction_ = nullptr;
    QAction* clearAction_ = nullptr;
    QAction* saveAction_ = nullptr;
    QAction* copyPlainAction_ = nullptr;
    QAction* historyAction_ = nullptr;
    QPointer<ConsoleHistoryDialog> historyDialog_;
};

} // namespace lvd::gui
