// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/transcript_widget.hpp"

#include "app/console_input.hpp"
#include "app/console_text.hpp"
#include "sim/parsers/report_lines.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCompleter>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScrollBar>
#include <QShortcut>
#include <QStandardItemModel>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocumentFragment>
#include <QToolButton>
#include <QToolTip>
#include <QUrl>
#include <QVBoxLayout>

namespace lvd::gui {

namespace {

const QString kLinkScheme = QStringLiteral("lvd-time:");
const QString kSourceScheme = QStringLiteral("lvd-src:");

QColor severityColor(ReportSeverity s, bool darkBackground) {
    switch (s) {
    case ReportSeverity::Note:
        return darkBackground ? QColor(0x5f, 0xc8, 0xff) : QColor(0x0b, 0x6f, 0xb0);
    case ReportSeverity::Warning:
        return darkBackground ? QColor(0xff, 0xb4, 0x20) : QColor(0xa8, 0x60, 0x00);
    case ReportSeverity::Error:
        return darkBackground ? QColor(0xff, 0x6a, 0x6a) : QColor(0xc4, 0x1c, 0x1c);
    case ReportSeverity::Failure:
        return darkBackground ? QColor(0xff, 0x55, 0xe0) : QColor(0xa8, 0x0c, 0x94);
    }
    return QColor();
}

std::optional<ReportSeverity> asReportSeverity(LineSeverity s) {
    switch (s) {
    case LineSeverity::Note:
        return ReportSeverity::Note;
    case LineSeverity::Warning:
        return ReportSeverity::Warning;
    case LineSeverity::Error:
        return ReportSeverity::Error;
    case LineSeverity::None:
        break;
    }
    return std::nullopt;
}

QToolButton* toolButton(QWidget* parent, const QString& text, const QString& tip) {
    auto* b = new QToolButton(parent);
    b->setText(text);
    b->setToolTip(tip);
    b->setAutoRaise(true);
    return b;
}

} // namespace

TranscriptWidget::TranscriptWidget(QWidget* parent)
    : QWidget(parent) {
    view_ = new QPlainTextEdit(this);
    view_->setObjectName("transcriptView");
    view_->setReadOnly(true);
    view_->setMaximumBlockCount(kDefaultMaximumLines);
    view_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    view_->setLineWrapMode(QPlainTextEdit::NoWrap);
    view_->setUndoRedoEnabled(false); // the text is a log; formatting a link must not fill an undo stack
    view_->viewport()->setMouseTracking(true);
    view_->viewport()->installEventFilter(this);
    view_->setContextMenuPolicy(Qt::CustomContextMenu);

    input_ = new ConsoleInput(this);
    input_->setPlaceholderText(tr(
        "Command (Tcl and simulator commands: run 100 ns, add wave -r /*, examine /sig ...); Tab completes"));
    input_->viewport()->installEventFilter(this);

    popupCompleter_ = new QCompleter(this);
    popupModel_ = new QStandardItemModel(popupCompleter_);
    popupCompleter_->setModel(popupModel_);
    popupCompleter_->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    popupCompleter_->setCaseSensitivity(Qt::CaseInsensitive);
    popupCompleter_->setMaxVisibleItems(12);
    popupCompleter_->setWidget(input_);
    popupCompleter_->popup()->setObjectName("completionPopup");
    popupCompleter_->popup()->setFont(input_->font());
    input_->setCompletionPopup(popupCompleter_->popup());
    connect(popupCompleter_, qOverload<const QModelIndex&>(&QCompleter::activated), this,
            [this](const QModelIndex& index) { insertCompletion(index.data(Qt::UserRole).toString()); });

    buildFindBar();
    buildSearchBar();
    buildActions();

    connect(input_, &ConsoleInput::submitted, this, [this](const QString& c) { submit(c); });
    connect(input_, &ConsoleInput::historyStep, this, [this](int d) { recall(d); });
    connect(input_, &ConsoleInput::completionRequested, this, [this] { completeAtCursor(); });
    connect(input_, &ConsoleInput::reverseSearchRequested, this, [this] { startHistorySearch(); });
    connect(input_, &ConsoleInput::escapePressed, this, [this] {
        if (findBar_->isVisible())
            findBar_->hide();
    });
    connect(input_, &QPlainTextEdit::textChanged, this, [this] {
        if (completionPopupVisible())
            showCompletion(false);
    });
    connect(view_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu* menu = view_->createStandardContextMenu(pos);
        menu->addAction(copyPlainAction_);
        menu->addSeparator();
        menu->addAction(findAction_);
        menu->addAction(saveAction_);
        menu->addAction(clearAction_);
        menu->addSeparator();
        menu->addAction(historyAction_);
        menu->exec(view_->viewport()->mapToGlobal(pos));
        delete menu;
    });

    setAcceptDrops(true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(view_, 1);
    layout->addWidget(findBar_);
    layout->addWidget(searchBar_);
    layout->addWidget(input_);
}

void TranscriptWidget::buildFindBar() {
    findBar_ = new QWidget(this);
    findBar_->setObjectName("transcriptFindBar");
    findEdit_ = new QLineEdit(findBar_);
    findEdit_->setObjectName("transcriptFind");
    findEdit_->setPlaceholderText(tr("Find in transcript (Enter next, Shift+Enter previous, Esc closes)"));
    findEdit_->setClearButtonEnabled(true);
    findEdit_->installEventFilter(this);
    findCase_ = new QCheckBox(tr("Match case"), findBar_);
    QToolButton* prev = toolButton(findBar_, QStringLiteral("<"), tr("Find previous (Shift+F3)"));
    QToolButton* next = toolButton(findBar_, QStringLiteral(">"), tr("Find next (F3)"));
    QToolButton* close = toolButton(findBar_, QStringLiteral("x"), tr("Close the find bar (Esc)"));
    findState_ = new QLabel(findBar_);
    auto* h = new QHBoxLayout(findBar_);
    h->setContentsMargins(0, 0, 0, 0);
    h->addWidget(findEdit_, 1);
    h->addWidget(findCase_);
    h->addWidget(prev);
    h->addWidget(next);
    h->addWidget(findState_);
    h->addWidget(close);
    findBar_->hide();
    connect(findEdit_, &QLineEdit::textChanged, this, [this] { findState_->clear(); });
    connect(prev, &QToolButton::clicked, this, [this] { find(findEdit_->text(), true); });
    connect(next, &QToolButton::clicked, this, [this] { find(findEdit_->text()); });
    connect(close, &QToolButton::clicked, this, [this] {
        findBar_->hide();
        input_->setFocus();
    });
    auto* f3 = new QShortcut(QKeySequence(Qt::Key_F3), this);
    f3->setContext(Qt::WidgetWithChildrenShortcut);
    connect(f3, &QShortcut::activated, this, [this] { find(findEdit_->text()); });
    auto* sf3 = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3), this);
    sf3->setContext(Qt::WidgetWithChildrenShortcut);
    connect(sf3, &QShortcut::activated, this, [this] { find(findEdit_->text(), true); });
}

void TranscriptWidget::buildSearchBar() {
    searchBar_ = new QWidget(this);
    searchBar_->setObjectName("historySearchBar");
    auto* label = new QLabel(tr("History search:"), searchBar_);
    searchEdit_ = new QLineEdit(searchBar_);
    searchEdit_->setObjectName("historySearch");
    searchEdit_->setPlaceholderText(tr("text in an earlier command (Ctrl+R older, Enter runs, Esc cancels)"));
    searchEdit_->installEventFilter(this);
    searchState_ = new QLabel(searchBar_);
    auto* h = new QHBoxLayout(searchBar_);
    h->setContentsMargins(0, 0, 0, 0);
    h->addWidget(label);
    h->addWidget(searchEdit_, 1);
    h->addWidget(searchState_);
    searchBar_->hide();
    connect(searchEdit_, &QLineEdit::textEdited, this, [this] { searchHistory(false); });
}

void TranscriptWidget::buildActions() {
    findAction_ = new QAction(tr("&Find in transcript..."), this);
    findAction_->setObjectName("transcriptFindAction");
    findAction_->setShortcut(QKeySequence::Find);
    findAction_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(findAction_); // Ctrl+F while the transcript has the focus
    connect(findAction_, &QAction::triggered, this, [this] { showFindBar(); });

    clearAction_ = new QAction(tr("&Clear transcript"), this);
    clearAction_->setObjectName("transcriptClearAction");
    connect(clearAction_, &QAction::triggered, this, [this] { clear(); });

    saveAction_ = new QAction(tr("&Save transcript..."), this);
    saveAction_->setObjectName("transcriptSaveAction");
    connect(saveAction_, &QAction::triggered, this, [this] { saveTranscriptDialog(); });

    copyPlainAction_ = new QAction(tr("Copy &without prompts"), this);
    copyPlainAction_->setObjectName("transcriptCopyPlainAction");
    copyPlainAction_->setToolTip(tr("Copy the selection (or everything) without the \"% \" prompts"));
    connect(copyPlainAction_, &QAction::triggered, this, [this] { copyWithoutPrompts(); });

    historyAction_ = new QAction(tr("Command &history..."), this);
    historyAction_->setObjectName("transcriptHistoryAction");
    connect(historyAction_, &QAction::triggered, this, [this] { showHistoryDialog(); });
}

// ---- transcript text --------------------------------------------------------------------------------------

void TranscriptWidget::append(const QString& text) {
    if (text.isEmpty())
        return;
    QScrollBar* bar = view_->verticalScrollBar();
    const bool atBottom = bar->value() >= bar->maximum() - 2;

    QTextDocument* doc = view_->document();
    QTextCursor c(doc);
    c.movePosition(QTextCursor::End);
    // One insertion for the whole chunk (the cursor turns line ends into blocks); the lines it completed are
    // decorated afterwards. An edit block keeps it to one layout update and one trim to the line cap.
    c.beginEditBlock();
    c.insertText(text, QTextCharFormat());
    const qsizetype completed = text.count(QLatin1Char('\n'));
    if (completed > 0) {
        QTextBlock first = doc->lastBlock();
        for (qsizetype i = 0; i < completed && first.previous().isValid(); ++i)
            first = first.previous();
        for (QTextBlock b = first; b.isValid() && b != doc->lastBlock(); b = b.next())
            decorateLine(b);
    }
    c.endEditBlock();
    if (atBottom)
        bar->setValue(bar->maximum());
}

void TranscriptWidget::appendNote(const QString& text) {
    if (!view_->document()->lastBlock().text().isEmpty())
        append(QStringLiteral("\n"));
    append(text + QLatin1Char('\n'));
}

void TranscriptWidget::decorateLine(const QTextBlock& block) {
    const QString line = block.text();
    if (line.size() < 5)
        return;
    const bool dark = view_->palette().base().color().lightness() < 128;

    // The whole line in the colour of its severity.
    if (const auto sev = asReportSeverity(lineSeverity(line))) {
        QTextCharFormat fmt;
        fmt.setForeground(severityColor(*sev, dark));
        QTextCursor lc(block);
        lc.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        lc.mergeCharFormat(fmt);
    }

    // The time stamp of an HDL report: a link to that time in the wave window.
    if (line.size() >= 12 && line.contains(QStringLiteral("** "))) {
        if (const auto r = parseReportLine(line.toStdString())) {
            // Everything before the time stamp is ASCII, so the offsets in the UTF-8 string are character
            // offsets.
            QTextCharFormat fmt;
            fmt.setAnchor(true);
            fmt.setAnchorHref(kLinkScheme + QString::number(r->timeFs) + ':' + QString::number(r->delta));
            fmt.setForeground(severityColor(r->severity, dark));
            fmt.setFontUnderline(true);
            QTextCursor lc(block);
            lc.setPosition(block.position() + static_cast<int>(r->stampBegin));
            lc.setPosition(block.position() + static_cast<int>(r->stampEnd), QTextCursor::KeepAnchor);
            lc.mergeCharFormat(fmt);
            emit reportLogged(r->timeFs, static_cast<int>(r->severity), QString::fromStdString(r->message));
        }
    }

    // file:line references: links to the Source window.
    for (const SourceRef& ref : findSourceRefs(line)) {
        QTextCharFormat fmt;
        fmt.setAnchor(true);
        fmt.setAnchorHref(kSourceScheme + QString::number(ref.line) + ':' + ref.file);
        fmt.setForeground(view_->palette().link());
        fmt.setFontUnderline(true);
        QTextCursor lc(block);
        lc.setPosition(block.position() + ref.start);
        lc.setPosition(block.position() + ref.start + ref.length, QTextCursor::KeepAnchor);
        lc.mergeCharFormat(fmt);
    }
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

std::optional<std::pair<QString, int>> TranscriptWidget::sourceLinkAt(const QPoint& viewportPos) const {
    const QString href = view_->anchorAt(viewportPos);
    if (!href.startsWith(kSourceScheme))
        return std::nullopt;
    const QString rest = href.mid(kSourceScheme.size());
    const qsizetype colon = rest.indexOf(QLatin1Char(':'));
    bool ok = false;
    const int line = rest.left(colon).toInt(&ok);
    if (!ok || colon < 0)
        return std::nullopt;
    return std::make_pair(rest.mid(colon + 1), line);
}

QString TranscriptWidget::text() const {
    return view_->toPlainText();
}

void TranscriptWidget::clear() {
    view_->clear();
}

QString TranscriptWidget::textWithoutPrompts(bool selectionOnly) const {
    const QTextCursor c = view_->textCursor();
    const QString t = selectionOnly ? c.selection().toPlainText() : view_->toPlainText();
    return stripPrompts(t);
}

void TranscriptWidget::copyWithoutPrompts() {
    QApplication::clipboard()->setText(textWithoutPrompts(view_->textCursor().hasSelection()));
}

bool TranscriptWidget::saveTranscript(const QString& path) const {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    const QByteArray data = text().toUtf8();
    return f.write(data) == data.size();
}

void TranscriptWidget::saveTranscriptDialog() {
    if (!dialogs_)
        return;
    const QString path =
        QFileDialog::getSaveFileName(this, tr("Save transcript"), QStringLiteral("transcript.txt"),
                                     tr("Text files (*.txt *.log);;All files (*)"));
    if (!path.isEmpty() && !saveTranscript(path))
        QMessageBox::warning(this, tr("Save transcript"), tr("Cannot write %1").arg(path));
}

void TranscriptWidget::setMaximumLines(int lines) {
    view_->setMaximumBlockCount(qMax(1, lines));
}

int TranscriptWidget::maximumLines() const {
    return view_->maximumBlockCount();
}

// ---- find -------------------------------------------------------------------------------------------------

void TranscriptWidget::showFindBar() {
    findBar_->show();
    const QString sel = view_->textCursor().selectedText();
    if (!sel.isEmpty() && !sel.contains(QChar::ParagraphSeparator))
        findEdit_->setText(sel);
    findEdit_->setFocus();
    findEdit_->selectAll();
}

bool TranscriptWidget::find(const QString& text, bool backward) {
    if (text.isEmpty())
        return false;
    QTextDocument::FindFlags flags;
    if (backward)
        flags |= QTextDocument::FindBackward;
    if (findCase_->isChecked())
        flags |= QTextDocument::FindCaseSensitively;
    bool found = view_->find(text, flags);
    if (!found) {
        // Wrap around.
        const QTextCursor keep = view_->textCursor();
        QTextCursor c(view_->document());
        c.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
        view_->setTextCursor(c);
        found = view_->find(text, flags);
        if (!found)
            view_->setTextCursor(keep);
        else
            findState_->setText(backward ? tr("wrapped to the end") : tr("wrapped to the start"));
    } else {
        findState_->clear();
    }
    if (!found)
        findState_->setText(tr("not found"));
    return found;
}

// ---- command line -----------------------------------------------------------------------------------------

void TranscriptWidget::setInputEnabled(bool enabled) {
    input_->setEnabled(enabled);
    if (enabled)
        input_->setFocus();
    else
        hideCompletion();
}

void TranscriptWidget::setCommandText(const QString& text) {
    input_->setText(text);
    historyPos_ = history_.size();
    input_->setFocus();
}

void TranscriptWidget::submitCommand(const QString& command) {
    submit(command);
}

void TranscriptWidget::submit(const QString& command) {
    hideCompletion();
    input_->clear();
    if (history_.add(command))
        emit historyChanged();
    historyPos_ = history_.size();
    draft_.clear();
    // A command that may define or rename commands makes the shell's list out of date.
    static const QRegularExpression defines(
        QStringLiteral("(^|[\\s;\\[{])(proc|do|source|rename|interp)\\s"));
    if (defines.match(command).hasMatch())
        completer_.invalidateShellCommands();
    emit commandEntered(command);
}

void TranscriptWidget::setHistory(const QStringList& entries) {
    history_.setEntries(entries);
    historyPos_ = history_.size();
    draft_.clear();
}

void TranscriptWidget::setHistoryLimit(int limit) {
    history_.setLimit(limit);
    historyPos_ = history_.size();
}

void TranscriptWidget::clearHistory() {
    history_.clear();
    historyPos_ = 0;
    draft_.clear();
    emit historyChanged();
}

void TranscriptWidget::recall(int direction) {
    if (history_.size() == 0)
        return;
    if (historyPos_ >= history_.size())
        draft_ = input_->text();
    historyPos_ = qBound<qsizetype>(0, historyPos_ + direction, history_.size());
    input_->setText(historyPos_ == history_.size() ? draft_ : history_.at(historyPos_));
}

ConsoleHistoryDialog* TranscriptWidget::showHistoryDialog() {
    if (!historyDialog_) {
        historyDialog_ = new ConsoleHistoryDialog(this);
        historyDialog_->setWindowFlag(Qt::Window);
        connect(historyDialog_, &ConsoleHistoryDialog::insertRequested, this,
                [this](const QString& c) { setCommandText(c); });
        connect(historyDialog_, &ConsoleHistoryDialog::runRequested, this,
                [this](const QString& c) { submit(c); });
        connect(historyDialog_, &ConsoleHistoryDialog::clearRequested, this, [this] { clearHistory(); });
        connect(this, &TranscriptWidget::historyChanged, historyDialog_,
                [this] { historyDialog_->setEntries(history_.entries()); });
    }
    historyDialog_->setEntries(history_.entries());
    historyDialog_->show();
    historyDialog_->raise();
    return historyDialog_;
}

void TranscriptWidget::startHistorySearch() {
    if (searchBar_->isVisible()) {
        searchHistory(true);
        return;
    }
    hideCompletion();
    searchDraft_ = input_->text();
    searchIndex_ = history_.size();
    searchState_->clear();
    searchEdit_->clear();
    searchBar_->show();
    searchEdit_->setFocus();
}

void TranscriptWidget::searchHistory(bool older) {
    const QString t = searchEdit_->text();
    if (t.isEmpty()) {
        searchState_->clear();
        return;
    }
    const qsizetype from = older ? searchIndex_ : history_.size();
    const qsizetype i = history_.searchBackward(t, from);
    if (i < 0) {
        searchState_->setText(older ? tr("no older match") : tr("no match"));
        return;
    }
    searchIndex_ = i;
    input_->setText(history_.at(i));
    searchState_->setText(tr("%1 of %2").arg(history_.size() - i).arg(history_.size()));
}

void TranscriptWidget::endHistorySearch(bool accept, bool run) {
    searchBar_->hide();
    if (!accept)
        input_->setText(searchDraft_);
    historyPos_ = history_.size();
    input_->setFocus();
    if (accept && run && tclCommandComplete(input_->text()) && !input_->text().trimmed().isEmpty())
        submit(input_->text());
}

// ---- completion -------------------------------------------------------------------------------------------

void TranscriptWidget::setShellCommands(const QStringList& commands) {
    completer_.setShellCommands(commands);
    waitingForCommands_ = false;
    const bool redo =
        tabCursor_ >= 0 && input_->text() == tabText_ && input_->textCursor().position() == tabCursor_;
    tabCursor_ = -1;
    if (redo && input_->isEnabled())
        completeAtCursor();
}

void TranscriptWidget::completeAtCursor() {
    const Completion c = completer_.complete(input_->text(), input_->textCursor().position());
    if (c.kind == Completion::Kind::Command && !completer_.hasShellCommands()) {
        // Complete from the built-in list now; again when the shell has told its own commands.
        tabText_ = input_->text();
        tabCursor_ = input_->textCursor().position();
        if (!waitingForCommands_) {
            waitingForCommands_ = true;
            emit shellCommandsWanted();
        }
    }
    showCompletion(true);
}

void TranscriptWidget::showCompletion(bool fromTab) {
    const QString t = input_->text();
    const int cursor = input_->textCursor().position();
    Completion c = completer_.complete(t, cursor);
    // While typing with the popup open: it follows the word it was opened for, and closes with it.
    if (c.candidates.isEmpty() || (!fromTab && (c.prefix.isEmpty() || c.start != popupStart_))) {
        hideCompletion();
        return;
    }
    if (fromTab && c.candidates.size() == 1) {
        hideCompletion();
        insertCompletion(c.candidates.first());
        return;
    }
    if (fromTab) {
        const QString common = c.commonPrefix();
        if (common != c.prefix) {
            QTextCursor tc = input_->textCursor();
            tc.setPosition(c.start);
            tc.setPosition(cursor, QTextCursor::KeepAnchor);
            const QSignalBlocker block(input_);
            tc.insertText(common);
            input_->setTextCursor(tc);
        }
    }
    popupModel_->clear();
    for (qsizetype i = 0; i < c.candidates.size(); ++i) {
        auto* item = new QStandardItem(c.labels.value(i, c.candidates[i]));
        item->setData(c.candidates[i], Qt::UserRole);
        item->setToolTip(c.candidates[i]);
        popupModel_->appendRow(item);
    }
    popupStart_ = c.start;
    popupCompleter_->setCompletionPrefix(QString());
    QAbstractItemView* popup = popupCompleter_->popup();
    QRect r = input_->cursorRect();
    r.translate(input_->viewport()->pos());
    r.setWidth(qMin(600, popup->sizeHintForColumn(0) + popup->verticalScrollBar()->sizeHint().width() + 12));
    popupCompleter_->complete(r);
    // The first candidate is selected: Enter or Tab takes it, typing goes on narrowing the list.
    popup->setCurrentIndex(popupCompleter_->completionModel()->index(0, 0));
}

void TranscriptWidget::insertCompletion(const QString& candidate) {
    if (candidate.isEmpty())
        return;
    const QString t = input_->text();
    const int cursor = input_->textCursor().position();
    const Completion c = completer_.complete(t, cursor);
    QTextCursor tc = input_->textCursor();
    tc.setPosition(c.start);
    tc.setPosition(cursor, QTextCursor::KeepAnchor);
    QString insert = candidate;
    const bool atWordEnd = cursor >= t.size() || t[cursor].isSpace();
    if (!Completion::isContainer(candidate) && atWordEnd &&
        (cursor >= t.size() || t[cursor] != QLatin1Char(' ')))
        insert += QLatin1Char(' ');
    {
        const QSignalBlocker block(input_);
        tc.insertText(insert);
    }
    input_->setTextCursor(tc);
    hideCompletion();
}

void TranscriptWidget::hideCompletion() {
    if (popupCompleter_ && popupCompleter_->popup()->isVisible())
        popupCompleter_->popup()->hide();
}

bool TranscriptWidget::completionPopupVisible() const {
    return popupCompleter_->popup()->isVisible();
}

QStringList TranscriptWidget::completionLabels() const {
    QStringList l;
    for (int i = 0; i < popupModel_->rowCount(); ++i)
        l << popupModel_->item(i)->text();
    return l;
}

// ---- drag and drop ----------------------------------------------------------------------------------------

QStringList TranscriptWidget::doFilesIn(const QMimeData* mime) {
    QStringList files;
    if (!mime || !mime->hasUrls())
        return files;
    for (const QUrl& u : mime->urls()) {
        if (!u.isLocalFile())
            continue;
        const QFileInfo fi(u.toLocalFile());
        const QString suffix = fi.suffix().toLower();
        if (suffix == QLatin1String("do") || suffix == QLatin1String("tcl") || suffix == QLatin1String("udo"))
            files << fi.absoluteFilePath();
    }
    return files;
}

void TranscriptWidget::dragEnterEvent(QDragEnterEvent* e) {
    if (!doFilesIn(e->mimeData()).isEmpty())
        e->acceptProposedAction();
    else
        e->ignore();
}

void TranscriptWidget::dropEvent(QDropEvent* e) {
    const QStringList files = doFilesIn(e->mimeData());
    if (files.isEmpty()) {
        e->ignore();
        return;
    }
    e->acceptProposedAction();
    for (const QString& f : files)
        emit doFileDropped(f);
}

// ---- events -----------------------------------------------------------------------------------------------

bool TranscriptWidget::eventFilter(QObject* watched, QEvent* event) {
    // Do files dropped on the text or on the command line.
    if ((watched == view_->viewport() || watched == input_->viewport()) &&
        (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove ||
         event->type() == QEvent::Drop)) {
        auto* de = static_cast<QDropEvent*>(event);
        if (!doFilesIn(de->mimeData()).isEmpty()) {
            if (event->type() == QEvent::Drop)
                dropEvent(de);
            else
                de->acceptProposedAction();
            return true;
        }
    }
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
                !view_->textCursor().hasSelection()) {
                if (const auto fs = linkTimeAt(pos))
                    emit timeActivated(*fs);
                else if (const auto src = sourceLinkAt(pos))
                    emit sourceActivated(src->first, src->second);
            }
            break;
        }
        case QEvent::MouseMove: {
            const QPoint pos = static_cast<QMouseEvent*>(event)->position().toPoint();
            view_->viewport()->setCursor(linkTimeAt(pos) || sourceLinkAt(pos) ? Qt::PointingHandCursor
                                                                              : Qt::IBeamCursor);
            break;
        }
        case QEvent::ToolTip: {
            const auto* he = static_cast<QHelpEvent*>(event);
            const QString href = view_->anchorAt(he->pos());
            if (href.startsWith(kLinkScheme)) {
                const QStringList parts = href.mid(kLinkScheme.size()).split(':');
                QToolTip::showText(he->globalPos(),
                                   tr("%1 fs (delta cycle %2). Click to move cursor 1 here.")
                                       .arg(parts.value(0), parts.value(1)),
                                   view_->viewport());
            } else if (const auto src = sourceLinkAt(he->pos())) {
                QToolTip::showText(
                    he->globalPos(),
                    tr("Click to open %1 at line %2 in the Source window.").arg(src->first).arg(src->second),
                    view_->viewport());
            } else {
                QToolTip::hideText();
            }
            return true;
        }
        default:
            break;
        }
    }
    if (watched == findEdit_ && event->type() == QEvent::KeyPress) {
        const auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            find(findEdit_->text(), ke->modifiers() & Qt::ShiftModifier);
            return true;
        }
        if (ke->key() == Qt::Key_Escape) {
            findBar_->hide();
            input_->setFocus();
            return true;
        }
    }
    if (watched == searchEdit_ && event->type() == QEvent::KeyPress) {
        const auto* ke = static_cast<QKeyEvent*>(event);
        const int key = ke->key();
        if (key == Qt::Key_R && (ke->modifiers() & Qt::ControlModifier)) {
            searchHistory(true);
            return true;
        }
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            endHistorySearch(true, true);
            return true;
        }
        if (key == Qt::Key_Escape || (key == Qt::Key_G && (ke->modifiers() & Qt::ControlModifier))) {
            endHistorySearch(false, false);
            return true;
        }
        if (key == Qt::Key_Tab || key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_Left ||
            key == Qt::Key_Right) {
            endHistorySearch(true, false); // edit the match
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace lvd::gui
