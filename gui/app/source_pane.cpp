// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/source_pane.hpp"

#include "app/vhdl_highlighter.hpp"

#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPolygon>
#include <QShortcut>
#include <QTextBlock>
#include <QToolButton>
#include <QVBoxLayout>

namespace lvd::gui {

namespace {

class Gutter : public QWidget {
public:
    explicit Gutter(SourceView* v)
        : QWidget(v)
        , view_(v) {}
    QSize sizeHint() const override { return {view_->gutterWidth(), 0}; }

protected:
    void paintEvent(QPaintEvent* e) override { view_->paintGutter(e); }

private:
    SourceView* view_;
};

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
}

int SourceView::gutterWidth() const {
    int digits = 3;
    for (int n = qMax(1, blockCount()); n >= 1000; n /= 10)
        ++digits;
    return 22 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
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
    const int w = gutter_->width();
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            const int line = number + 1;
            const int h = fontMetrics().height();
            if (line == current_) {
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
    for (int line : ready_)
        add(line, QColor(0x2e, 0xa0, 0x43, 40));
    if (current_ > 0)
        add(current_, QColor(0xe0, 0xa0, 0x10, 80));
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

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->addWidget(pathLabel_);
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
    if (line > 0)
        view_->showLine(line);
    else
        view_->moveCursor(QTextCursor::Start);
    return true;
}

void SourcePane::setCurrentLine(int line) {
    view_->setCurrentLine(line);
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
