// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/source_pane.hpp"

#include "app/vhdl_highlighter.hpp"

#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygon>
#include <QRegularExpression>
#include <QShortcut>
#include <QStyle>
#include <QTextBlock>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

namespace lvd::gui {

namespace {

const QColor kBreakpointRed(0xd0, 0x30, 0x30);
const QColor kExecYellow(0xff, 0xcc, 0x00);
const QColor kExecGreen(0x3a, 0xb0, 0x5a);
const QColor kCoveredGreen(0x4c, 0xaf, 0x50);
const QColor kPartialAmber(0xf0, 0xa8, 0x30);
const QColor kUncoveredRed(0xe0, 0x50, 0x50);

QColor coverageColour(const CoverageMark& m) {
    return m.covered() ? kCoveredGreen : m.uncovered() ? kUncoveredRed : kPartialAmber;
}

class Gutter : public QWidget {
public:
    explicit Gutter(SourceView* v)
        : QWidget(v)
        , view_(v) {
        setCursor(Qt::PointingHandCursor);
        setToolTip(QString()); // tool tips come from event()
    }
    QSize sizeHint() const override { return {view_->gutterWidth(), 0}; }

protected:
    void paintEvent(QPaintEvent* e) override { view_->paintGutter(e); }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton)
            view_->clickGutter(qRound(e->position().y()));
        else
            QWidget::mousePressEvent(e);
    }
    bool event(QEvent* e) override {
        if (e->type() == QEvent::ToolTip) {
            auto* he = static_cast<QHelpEvent*>(e);
            const QString tip = view_->gutterToolTip(he->pos().y());
            if (tip.isEmpty())
                QToolTip::hideText();
            else
                QToolTip::showText(he->globalPos(), tip, this);
            return true;
        }
        return QWidget::event(e);
    }

private:
    SourceView* view_;
};

/// Arrow pointing right with its tip at x 17, centred on cy.
QPolygonF executionArrow(int cy, int h) {
    const double half = qMax(3.0, h / 2.0 - 2.0);
    const double shaft = half * 0.45;
    return QPolygonF({QPointF(3, cy - shaft), QPointF(10, cy - shaft), QPointF(10, cy - half),
                      QPointF(17, cy), QPointF(10, cy + half), QPointF(10, cy + shaft),
                      QPointF(3, cy + shaft)});
}

} // namespace

SourceView::SourceView(QWidget* parent)
    : QPlainTextEdit(parent) {
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4);
    gutter_ = new Gutter(this);
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { updateGutter(); });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& r, int dy) {
        if (dy != 0)
            gutter_->scroll(0, dy);
        else
            gutter_->update(0, r.y(), gutter_->width(), r.height());
    });
    updateGutter();
    flashTimer_.setSingleShot(true);
    connect(&flashTimer_, &QTimer::timeout, this, [this] {
        flash_ = 0;
        updateSelections();
    });
}

void SourceView::flashLine(int line, int milliseconds) {
    flash_ = line;
    updateSelections();
    if (line > 0)
        flashTimer_.start(milliseconds);
    else
        flashTimer_.stop();
}

int SourceView::gutterWidth() const {
    int digits = 3;
    for (int n = qMax(1, blockCount()); n >= 1000; n /= 10)
        ++digits;
    return 26 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits + coverageColumnWidth();
}

int SourceView::coverageColumnWidth() const {
    return coverage_.isEmpty() ? 0 : fontMetrics().horizontalAdvance(QStringLiteral("99.9k")) + 8;
}

QString SourceView::hitText(qint64 hits) {
    if (hits < 10000)
        return QString::number(hits);
    if (hits < 1000000)
        return QString::number(hits / 1000.0, 'f', hits < 100000 ? 1 : 0) + QLatin1Char('k');
    if (hits < 1000000000)
        return QString::number(hits / 1e6, 'f', hits < 100000000 ? 1 : 0) + QLatin1Char('M');
    return QString::number(hits / 1e9, 'f', 1) + QLatin1Char('G');
}

void SourceView::setCoverageMarks(const QHash<int, CoverageMark>& marks) {
    const bool widthChanged = marks.isEmpty() != coverage_.isEmpty();
    coverage_ = marks;
    if (widthChanged)
        updateGutter();
    updateSelections();
    gutter_->update();
}

int SourceView::lineAt(int y) const {
    QTextBlock block = firstVisibleBlock();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    while (block.isValid()) {
        const int bottom = top + qRound(blockBoundingRect(block).height());
        if (y >= top && y < bottom)
            return block.blockNumber() + 1;
        if (y < top)
            return 0;
        block = block.next();
        top = bottom;
    }
    return 0;
}

void SourceView::clickGutter(int y) {
    const int line = lineAt(y);
    if (line > 0)
        emit gutterClicked(line);
}

QString SourceView::gutterToolTip(int y) const {
    const int line = lineAt(y);
    if (line <= 0)
        return {};
    QString tip;
    if (line == exec_ && execMark_ != ExecutionMark::None)
        tip = execMark_ == ExecutionMark::Innermost ? tr("Stopped here")
                                                    : tr("Caller frame: the call on this line");
    if (const auto it = tips_.constFind(line); it != tips_.constEnd() && !it->isEmpty())
        tip += (tip.isEmpty() ? QString() : QStringLiteral("\n")) + *it;
    const bool otherTip = !tip.isEmpty();
    if (const auto c = coverage_.constFind(line); c != coverage_.constEnd()) {
        QString cov = c->covered()
                          ? tr("Coverage: all %n item(s) covered", nullptr, c->items)
                          : tr("Coverage: %1 of %n item(s) covered", nullptr, c->items).arg(c->hitItems);
        cov += tr(", count %1").arg(c->hits);
        if (!c->tip.isEmpty())
            cov += QStringLiteral("\n") + c->tip;
        tip += (tip.isEmpty() ? QString() : QStringLiteral("\n")) + cov;
    }
    const QString click =
        marks_.contains(line) ? tr("Click to remove the breakpoint") : tr("Click to set a breakpoint");
    if (tip.isEmpty())
        tip = click;
    else if (!otherTip)
        tip += QStringLiteral("\n") + click;
    return tip;
}

void SourceView::setBreakpointMarks(const QHash<int, BreakpointMark>& marks,
                                    const QHash<int, QString>& tips) {
    marks_ = marks;
    tips_ = tips;
    gutter_->update();
}

void SourceView::setExecutionLine(int line, ExecutionMark mark) {
    exec_ = mark == ExecutionMark::None ? 0 : line;
    execMark_ = exec_ > 0 ? mark : ExecutionMark::None;
    updateSelections();
    gutter_->update();
    if (exec_ > 0)
        showLine(exec_);
}

void SourceView::updateGutter() {
    setViewportMargins(gutterWidth(), 0, 0, 0);
    const QRect cr = contentsRect();
    gutter_->setGeometry(QRect(cr.left(), cr.top(), gutterWidth(), cr.height()));
}

void SourceView::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    updateGutter();
}

void SourceView::paintGutter(QPaintEvent* event) {
    QPainter p(gutter_);
    p.fillRect(event->rect(), palette().color(QPalette::AlternateBase));
    p.setPen(palette().color(QPalette::Mid));
    QTextBlock block = firstVisibleBlock();
    int number = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    const int covW = coverageColumnWidth();
    const int w = gutter_->width() - covW;
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            const int line = number + 1;
            const int h = fontMetrics().height();
            const int cy = top + h / 2;
            const auto mark = marks_.constFind(line);
            if (mark != marks_.constEnd()) {
                p.save();
                p.setRenderHint(QPainter::Antialiasing, true);
                const double r = qMax(3.0, qMin(5.5, h / 2.0 - 1.0));
                const QPointF c(10, cy);
                switch (*mark) {
                case BreakpointMark::Enabled:
                    p.setPen(QPen(kBreakpointRed.darker(130), 1));
                    p.setBrush(kBreakpointRed);
                    break;
                case BreakpointMark::Disabled:
                    p.setPen(QPen(kBreakpointRed, 1.6));
                    p.setBrush(Qt::NoBrush);
                    break;
                case BreakpointMark::Unverified:
                    p.setPen(QPen(QColor(0x70, 0x70, 0x70), 1));
                    p.setBrush(QColor(0xa8, 0xa8, 0xa8));
                    break;
                }
                p.drawEllipse(c, r, r);
                p.restore();
            }
            if (line == exec_ && execMark_ != ExecutionMark::None) {
                p.save();
                p.setRenderHint(QPainter::Antialiasing, true);
                const QColor fill = execMark_ == ExecutionMark::Innermost ? kExecYellow : kExecGreen;
                p.setPen(QPen(fill.darker(190), 1));
                p.setBrush(fill);
                p.drawPolygon(executionArrow(cy, h));
                p.restore();
                p.setPen(palette().color(QPalette::Text));
            } else if (mark != marks_.constEnd()) {
                p.setPen(palette().color(QPalette::Text));
            } else if (line == current_) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0xe0, 0xa0, 0x10));
                p.drawPolygon(
                    QPolygon({QPoint(3, top + 2), QPoint(3, top + h - 2), QPoint(13, top + h / 2)}));
                p.setPen(palette().color(QPalette::Text));
            } else if (ready_.contains(line)) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0x2e, 0xa0, 0x43));
                p.drawEllipse(QPoint(8, top + h / 2), 3, 3);
                p.setPen(palette().color(QPalette::Mid));
            } else {
                p.setPen(palette().color(QPalette::Mid));
            }
            p.drawText(0, top, w - 5, fontMetrics().height(), Qt::AlignRight, QString::number(line));
            if (const auto c = coverage_.constFind(line); c != coverage_.constEnd()) {
                const QColor col = coverageColour(*c);
                const QRect cell(w + 1, top + 1, covW - 3, qMax(1, h - 2));
                p.save();
                p.fillRect(cell, QColor(col.red(), col.green(), col.blue(), 70));
                p.fillRect(QRect(cell.left(), cell.top(), 3, cell.height()), col);
                p.setPen(palette().color(QPalette::Text));
                p.drawText(cell.adjusted(4, 0, -3, 0), Qt::AlignRight | Qt::AlignVCenter, hitText(c->hits));
                p.restore();
            }
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++number;
    }
}

void SourceView::setCurrentLine(int line) {
    current_ = line;
    updateSelections();
    gutter_->update();
    if (line > 0)
        showLine(line);
}

void SourceView::setReadyLines(const QSet<int>& lines) {
    ready_ = lines;
    updateSelections();
    gutter_->update();
}

void SourceView::updateSelections() {
    QList<QTextEdit::ExtraSelection> sels;
    auto add = [&](int line, const QColor& c) {
        const QTextBlock b = document()->findBlockByNumber(line - 1);
        if (!b.isValid())
            return;
        QTextEdit::ExtraSelection s;
        s.format.setBackground(c);
        s.format.setProperty(QTextFormat::FullWidthSelection, true);
        s.cursor = QTextCursor(b);
        sels.push_back(s);
    };
    for (auto it = coverage_.constBegin(); it != coverage_.constEnd(); ++it) {
        if (it->covered())
            continue;
        const QColor c = it->uncovered() ? kUncoveredRed : kPartialAmber;
        add(it.key(), QColor(c.red(), c.green(), c.blue(), it->uncovered() ? 55 : 45));
    }
    for (int line : ready_)
        add(line, QColor(0x2e, 0xa0, 0x43, 40));
    if (current_ > 0 && current_ != exec_)
        add(current_, QColor(0xe0, 0xa0, 0x10, 80));
    if (exec_ > 0)
        add(exec_, execMark_ == ExecutionMark::Innermost ? QColor(0xff, 0xd8, 0x30, 120)
                                                         : QColor(0x50, 0xc0, 0x70, 90));
    if (flash_ > 0)
        add(flash_, QColor(0x4a, 0xa8, 0xff, 110));
    setExtraSelections(sels);
}

void SourceView::showLine(int line) {
    const QTextBlock b = document()->findBlockByNumber(line - 1);
    if (!b.isValid())
        return;
    QTextCursor c(b);
    setTextCursor(c);
    centerCursor();
}

// ---------------------------------------------------------------------------------------------

SourcePane::SourcePane(QWidget* parent)
    : QWidget(parent) {
    pathLabel_ = new QLabel(tr("(no file)"), this);
    pathLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    back_ = new QToolButton(this);
    back_->setObjectName("sourceBack");
    back_->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    back_->setToolTip(tr("Back to the previous place (Alt+Left)"));
    back_->setAutoRaise(true);
    forward_ = new QToolButton(this);
    forward_->setObjectName("sourceForward");
    forward_->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
    forward_->setToolTip(tr("Forward to the next place (Alt+Right)"));
    forward_->setAutoRaise(true);
    connect(back_, &QToolButton::clicked, this, [this] { goBack(); });
    connect(forward_, &QToolButton::clicked, this, [this] { goForward(); });
    view_ = new SourceView(this);
    highlighter_ = new VhdlHighlighter(view_->document());

    findEdit_ = new QLineEdit(this);
    findEdit_->setPlaceholderText(tr("Find (Ctrl+F)"));
    findEdit_->setClearButtonEnabled(true);
    auto* prev = new QToolButton(this);
    prev->setText(QStringLiteral("<"));
    prev->setToolTip(tr("Find previous (Shift+F3)"));
    auto* next = new QToolButton(this);
    next->setText(QStringLiteral(">"));
    next->setToolTip(tr("Find next (F3)"));
    findState_ = new QLabel(this);

    auto* bar = new QHBoxLayout;
    bar->addWidget(findEdit_, 1);
    bar->addWidget(prev);
    bar->addWidget(next);
    bar->addWidget(findState_);

    auto* top = new QHBoxLayout;
    top->setSpacing(2);
    top->addWidget(back_);
    top->addWidget(forward_);
    top->addWidget(pathLabel_, 1);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->addLayout(top);
    layout->addWidget(view_, 1);
    layout->addLayout(bar);

    connect(findEdit_, &QLineEdit::returnPressed, this, [this] { find(findEdit_->text()); });
    connect(findEdit_, &QLineEdit::textChanged, this, [this](const QString&) { findState_->clear(); });
    connect(next, &QToolButton::clicked, this, [this] { find(findEdit_->text()); });
    connect(prev, &QToolButton::clicked, this, [this] { find(findEdit_->text(), true); });
    auto* f3 = new QShortcut(QKeySequence(Qt::Key_F3), this);
    f3->setContext(Qt::WidgetWithChildrenShortcut);
    connect(f3, &QShortcut::activated, this, [this] { find(findEdit_->text()); });
    auto* sf3 = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3), this);
    sf3->setContext(Qt::WidgetWithChildrenShortcut);
    connect(sf3, &QShortcut::activated, this, [this] { find(findEdit_->text(), true); });
    auto* ctrlF = new QShortcut(QKeySequence::Find, this);
    ctrlF->setContext(Qt::WidgetWithChildrenShortcut);
    connect(ctrlF, &QShortcut::activated, this, [this] { focusFind(); });
    auto* altLeft = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Left), this);
    altLeft->setContext(Qt::WidgetWithChildrenShortcut);
    connect(altLeft, &QShortcut::activated, this, [this] { goBack(); });
    auto* altRight = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Right), this);
    altRight->setContext(Qt::WidgetWithChildrenShortcut);
    connect(altRight, &QShortcut::activated, this, [this] { goForward(); });
    updateHistoryButtons();
    connect(view_, &SourceView::gutterClicked, this, [this](int line) {
        if (!file_.isEmpty())
            emit breakpointToggleRequested(file_, line);
    });
}

bool SourcePane::lineIsBlank(int line) const {
    const QTextBlock b = view_->document()->findBlockByNumber(line - 1);
    if (!b.isValid())
        return true;
    const QString t = b.text().trimmed();
    return t.isEmpty() || t.startsWith(QLatin1String("--")) || t.startsWith(QLatin1String("//"));
}

void SourcePane::setBreakpoints(const QVector<LineBreakpoint>& breakpoints) {
    breakpoints_ = breakpoints;
    applyMarks();
}

void SourcePane::setExecutionPoint(const QString& file, int line, ExecutionMark mark) {
    execFile_ = file;
    execLine_ = line;
    execMark_ = mark;
    applyMarks();
}

void SourcePane::applyMarks() {
    QHash<int, BreakpointMark> marks;
    QHash<int, QString> tips;
    if (!file_.isEmpty()) {
        for (const LineBreakpoint& b : breakpoints_) {
            if (!sameSourceFile(b.file, file_))
                continue;
            BreakpointMark m = BreakpointMark::Enabled;
            QString tip;
            if (!b.enabled) {
                m = BreakpointMark::Disabled;
                tip = tr("Breakpoint (disabled)");
            } else if (b.state == LineBreakpoint::State::Rejected) {
                m = BreakpointMark::Unverified;
                tip = tr("Breakpoint not set: %1").arg(b.note);
            } else if (lineIsBlank(b.line)) {
                m = BreakpointMark::Unverified;
                tip = tr("Breakpoint on a line without a statement: it is never reached");
            } else {
                tip = b.hits > 0 ? tr("Breakpoint, reached %n time(s)", nullptr, b.hits) : tr("Breakpoint");
            }
            marks.insert(b.line, m);
            tips.insert(b.line, tip);
        }
    }
    view_->setBreakpointMarks(marks, tips);
    QHash<int, CoverageMark> cov;
    if (!file_.isEmpty())
        for (auto it = coverage_.constBegin(); it != coverage_.constEnd(); ++it)
            if (sameSourceFile(it.key(), file_)) {
                cov = it.value();
                break;
            }
    view_->setCoverageMarks(cov);
    const bool here = execMark_ != ExecutionMark::None && sameSourceFile(execFile_, file_);
    if (here != (view_->executionLine() > 0) || (here && view_->executionLine() != execLine_) ||
        (here && view_->executionMark() != execMark_))
        view_->setExecutionLine(here ? execLine_ : 0, here ? execMark_ : ExecutionMark::None);
}

void SourcePane::setCoverage(const QHash<QString, QHash<int, CoverageMark>>& byFile) {
    coverage_ = byFile;
    applyMarks();
}

int SourcePane::lineCount() const {
    return view_->document()->blockCount();
}

bool SourcePane::openFile(const QString& path, int line) {
    error_.clear();
    if (path.isEmpty()) {
        error_ = tr("no source file known");
        return false;
    }
    const QString canonical = QFileInfo(path).absoluteFilePath();
    if (canonical == file_ && !file_.isEmpty()) {
        if (line > 0)
            view_->showLine(line);
        return true;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        error_ = tr("cannot read %1: %2").arg(path, f.errorString());
        return false;
    }
    view_->setPlainText(QString::fromUtf8(f.readAll()));
    file_ = canonical;
    pathLabel_->setText(file_);
    view_->setCurrentLine(0);
    view_->setReadyLines({});
    view_->setExecutionLine(0, ExecutionMark::None);
    applyMarks();
    if (line > 0)
        view_->showLine(line);
    else if (view_->executionLine() == 0)
        view_->moveCursor(QTextCursor::Start);
    return true;
}

void SourcePane::setCurrentLine(int line) {
    view_->setCurrentLine(line);
}

int SourcePane::cursorLine() const {
    return view_->textCursor().blockNumber() + 1;
}

void SourcePane::rememberCursor() {
    if (historyPos_ >= 0 && historyPos_ < history_.size() && !file_.isEmpty() &&
        history_[historyPos_].file == file_)
        history_[historyPos_].line = cursorLine();
}

void SourcePane::updateHistoryButtons() {
    back_->setEnabled(canGoBack());
    forward_->setEnabled(canGoForward());
}

bool SourcePane::openAt(const SourceLocation& location) {
    const QString before = file_;
    const int beforeLine = cursorLine();
    if (!openFile(location.file, location.line))
        return false;
    // The place the view left is where Back returns to: the last entry, or a new one when the file on view
    // was opened another way (a stop, a selected process).
    if (historyPos_ >= 0 && history_[historyPos_].file == before)
        history_[historyPos_].line = beforeLine;
    else if (!before.isEmpty())
        history_.insert(++historyPos_, Place{before, beforeLine});
    history_.resize(historyPos_ + 1);
    history_.push_back(Place{file_, location.line});
    constexpr int kMaxHistory = 100;
    while (history_.size() > kMaxHistory)
        history_.removeFirst();
    historyPos_ = static_cast<int>(history_.size()) - 1;
    selectIdentifier(location.line, location.column, location.name);
    view_->flashLine(location.line);
    updateHistoryButtons();
    emit historyChanged();
    return true;
}

bool SourcePane::showPlace(const Place& place) {
    if (!openFile(place.file, place.line))
        return false;
    selectIdentifier(place.line, 0, {});
    view_->flashLine(place.line, 500);
    return true;
}

bool SourcePane::goBack() {
    if (!canGoBack())
        return false;
    rememberCursor();
    if (!showPlace(history_[historyPos_ - 1]))
        return false;
    --historyPos_;
    updateHistoryButtons();
    emit historyChanged();
    return true;
}

bool SourcePane::goForward() {
    if (!canGoForward())
        return false;
    rememberCursor();
    if (!showPlace(history_[historyPos_ + 1]))
        return false;
    ++historyPos_;
    updateHistoryButtons();
    emit historyChanged();
    return true;
}

void SourcePane::selectIdentifier(int line, int column, const QString& name) {
    const QTextBlock b = view_->document()->findBlockByNumber(line - 1);
    if (!b.isValid())
        return;
    const QString text = b.text();
    int start = -1;
    int length = 0;
    if (!name.isEmpty()) {
        if (column > 0 && text.mid(column - 1, name.size()).compare(name, Qt::CaseInsensitive) == 0) {
            start = column - 1;
        } else {
            // The column counts tabs differently, or the kernel named the object otherwise: the first
            // occurrence of the name as a whole word.
            const QRegularExpression word(QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(name)),
                                          QRegularExpression::CaseInsensitiveOption);
            const QRegularExpressionMatch m = word.match(text, qMax(0, column - 1));
            const QRegularExpressionMatch any = m.hasMatch() ? m : word.match(text);
            if (any.hasMatch())
                start = static_cast<int>(any.capturedStart());
        }
        if (start >= 0)
            length = static_cast<int>(name.size());
    }
    QTextCursor c(b);
    if (start >= 0) {
        c.setPosition(b.position() + start);
        c.setPosition(b.position() + start + length, QTextCursor::KeepAnchor);
    }
    view_->setTextCursor(c);
    view_->centerCursor();
}

bool SourcePane::find(const QString& text, bool backwards) {
    if (text.isEmpty())
        return false;
    QTextDocument::FindFlags flags;
    if (backwards)
        flags |= QTextDocument::FindBackward;
    QTextDocument* doc = view_->document();
    QTextCursor hit = doc->find(text, view_->textCursor(), flags);
    bool wrapped = false;
    if (hit.isNull()) {
        QTextCursor from(doc);
        from.movePosition(backwards ? QTextCursor::End : QTextCursor::Start);
        hit = doc->find(text, from, flags);
        wrapped = true;
    }
    if (hit.isNull()) {
        findState_->setText(tr("not found"));
        return false;
    }
    findState_->setText(wrapped ? tr("wrapped") : QString());
    view_->setTextCursor(hit);
    view_->centerCursor();
    return true;
}

void SourcePane::focusFind() {
    findEdit_->setFocus();
    findEdit_->selectAll();
}

} // namespace lvd::gui
