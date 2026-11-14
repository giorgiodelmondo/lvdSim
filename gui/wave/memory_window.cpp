// SPDX-License-Identifier: GPL-3.0-or-later
#include "wave/memory_window.hpp"

#include "wave/icons.hpp"
#include "wave/view_state.hpp"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableView>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace lvd::gui {

using namespace lvd::wave;

namespace {

void fillRadixBox(QComboBox* box, std::initializer_list<std::pair<const char*, Radix>> items) {
    for (const auto& [text, r] : items)
        box->addItem(QObject::tr(text), static_cast<int>(r));
}

void selectRadix(QComboBox* box, Radix r) {
    const int i = box->findData(static_cast<int>(r));
    if (i >= 0)
        box->setCurrentIndex(i);
}

QString rangeText(const MemoryInfo& m) {
    return m.left <= m.right ? QStringLiteral("%1 to %2").arg(m.left).arg(m.right)
                             : QStringLiteral("%1 downto %2").arg(m.left).arg(m.right);
}

} // namespace

MemoryWindow::MemoryWindow(QWidget* parent)
    : QWidget(parent) {
    setObjectName("memoryWindow");
    model_ = new MemoryModel(this);

    list_ = new QTreeWidget(this);
    list_->setObjectName("memoryList");
    list_->setColumnCount(3);
    list_->setHeaderLabels({tr("Memory"), tr("Range"), tr("Width")});
    list_->setRootIsDecorated(false);
    list_->setUniformRowHeights(true);
    list_->header()->setStretchLastSection(false);
    list_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    list_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    list_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    connect(list_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* item) { selectMemoryRow(item ? list_->indexOfTopLevelItem(item) : -1); });

    hint_ = new QLabel(this);
    hint_->setObjectName("memoryHint");
    hint_->setWordWrap(true);
    hint_->setText(
        tr("No memories in the waveform. Arrays of words are recorded when the simulation is started "
           "with Simulation > Record memories turned on (vsim option -lvdarrays)."));

    words_ = new QSpinBox(this);
    words_->setObjectName("wordsBox");
    words_->setRange(1, 256);
    words_->setValue(model_->wordsPerLine());
    words_->setToolTip(tr("Words per line"));
    connect(words_, &QSpinBox::valueChanged, this, [this](int n) { setWordsPerLine(n); });

    addressRadix_ = new QComboBox(this);
    addressRadix_->setObjectName("addressRadixBox");
    fillRadixBox(addressRadix_, {{"hex", Radix::Hex},
                                 {"decimal", Radix::Unsigned},
                                 {"octal", Radix::Octal},
                                 {"binary", Radix::Binary}});
    addressRadix_->setToolTip(tr("Address radix"));
    connect(addressRadix_, &QComboBox::currentIndexChanged, this,
            [this] { setAddressRadix(static_cast<Radix>(addressRadix_->currentData().toInt())); });

    dataRadix_ = new QComboBox(this);
    dataRadix_->setObjectName("dataRadixBox");
    fillRadixBox(dataRadix_, {{"hex", Radix::Hex},
                              {"binary", Radix::Binary},
                              {"octal", Radix::Octal},
                              {"unsigned", Radix::Unsigned},
                              {"signed", Radix::Signed},
                              {"ascii", Radix::Ascii}});
    dataRadix_->setToolTip(tr("Data radix"));
    connect(dataRadix_, &QComboBox::currentIndexChanged, this,
            [this] { setDataRadix(static_cast<Radix>(dataRadix_->currentData().toInt())); });

    time_ = new QComboBox(this);
    time_->setObjectName("timeBox");
    time_->addItem(tr("latest"), static_cast<int>(TimeSource::Latest));
    time_->addItem(tr("at wave cursor"), static_cast<int>(TimeSource::Cursor));
    time_->setToolTip(
        tr("Show the memory at the end of the recorded data (updated while the simulation runs) "
           "or at the active wave cursor"));
    connect(time_, &QComboBox::currentIndexChanged, this,
            [this] { setTimeSource(static_cast<TimeSource>(time_->currentData().toInt())); });

    prevWrite_ = new QToolButton(this);
    prevWrite_->setObjectName("prevWrite");
    prevWrite_->setIcon(makeIcon(Icon::PrevEdge));
    prevWrite_->setToolTip(tr("Previous write to this memory (moves the wave cursor)"));
    connect(prevWrite_, &QToolButton::clicked, this, [this] { stepWrite(-1); });
    nextWrite_ = new QToolButton(this);
    nextWrite_->setObjectName("nextWrite");
    nextWrite_->setIcon(makeIcon(Icon::NextEdge));
    nextWrite_->setToolTip(tr("Next write to this memory (moves the wave cursor)"));
    connect(nextWrite_, &QToolButton::clicked, this, [this] { stepWrite(1); });

    goto_ = new QLineEdit(this);
    goto_->setObjectName("gotoEdit");
    goto_->setPlaceholderText(tr("Go to address"));
    goto_->setClearButtonEnabled(true);
    goto_->setToolTip(
        tr("An address in the address radix, or with a prefix: 0x1F, 16#1F#, x\"1F\", 0b101, 42 "
           "in decimal radix"));
    connect(goto_, &QLineEdit::returnPressed, this, [this] { goToAddress(goto_->text()); });

    find_ = new QLineEdit(this);
    find_->setObjectName("findEdit");
    find_->setPlaceholderText(tr("Find value"));
    find_->setClearButtonEnabled(true);
    find_->setToolTip(tr("A value in the data radix; ? or - match any digit"));
    connect(find_, &QLineEdit::returnPressed, this, [this] { findValue(find_->text(), 1); });
    auto* findPrev = new QToolButton(this);
    findPrev->setObjectName("findPrev");
    findPrev->setArrowType(Qt::UpArrow);
    findPrev->setToolTip(tr("Find the previous word with this value"));
    connect(findPrev, &QToolButton::clicked, this, [this] { findValue(find_->text(), -1); });
    auto* findNext = new QToolButton(this);
    findNext->setObjectName("findNext");
    findNext->setArrowType(Qt::DownArrow);
    findNext->setToolTip(tr("Find the next word with this value"));
    connect(findNext, &QToolButton::clicked, this, [this] { findValue(find_->text(), 1); });

    auto* save = new QToolButton(this);
    save->setObjectName("saveMemory");
    save->setText(tr("Save..."));
    save->setToolTip(tr("Write the memory as shown to a $readmemh file ($readmemb for .bin or .memb)"));
    connect(save, &QToolButton::clicked, this, [this] { saveDialog(); });

    table_ = new QTableView(this);
    table_->setObjectName("memoryTable");
    table_->setModel(model_);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setSelectionBehavior(QAbstractItemView::SelectItems);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table_->verticalHeader()->setDefaultSectionSize(table_->fontMetrics().height() + 4);
    table_->verticalHeader()->setDefaultAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table_, &QTableView::customContextMenuRequested, this, &MemoryWindow::showContextMenu);

    status_ = new QLabel(this);
    status_->setObjectName("memoryStatus");
    status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    auto* bar1 = new QHBoxLayout;
    bar1->setContentsMargins(4, 2, 4, 0);
    bar1->addWidget(new QLabel(tr("Words/line"), this));
    bar1->addWidget(words_);
    bar1->addWidget(new QLabel(tr("Address"), this));
    bar1->addWidget(addressRadix_);
    bar1->addWidget(new QLabel(tr("Data"), this));
    bar1->addWidget(dataRadix_);
    bar1->addStretch(1);
    auto* bar2 = new QHBoxLayout;
    bar2->setContentsMargins(4, 0, 4, 2);
    bar2->addWidget(time_);
    bar2->addWidget(prevWrite_);
    bar2->addWidget(nextWrite_);
    bar2->addWidget(goto_, 1);
    bar2->addWidget(find_, 1);
    bar2->addWidget(findPrev);
    bar2->addWidget(findNext);
    bar2->addWidget(save);

    auto* gridPart = new QWidget(this);
    auto* gl = new QVBoxLayout(gridPart);
    gl->setContentsMargins(0, 0, 0, 0);
    gl->setSpacing(2);
    gl->addLayout(bar1);
    gl->addLayout(bar2);
    gl->addWidget(table_, 1);
    gl->addWidget(status_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(hint_);
    layout->addWidget(list_); // as high as its lines (setStore), up to six
    layout->addWidget(gridPart, 1);
    setStore(nullptr);
}

void MemoryWindow::setStore(std::shared_ptr<const WaveStore> store) {
    store_ = std::move(store);
    model_->setStore(store_);
    memories_ = store_ ? findMemories(*store_) : std::vector<MemoryInfo>{};
    {
        const QSignalBlocker block(list_);
        list_->clear();
        for (const MemoryInfo& m : memories_) {
            auto* item = new QTreeWidgetItem(list_);
            item->setText(0, QString::fromStdString(m.path));
            item->setText(1, rangeText(m));
            item->setText(2, QString::number(m.width));
            item->setToolTip(0, tr("%1: %2 words of %3 bits").arg(item->text(0)).arg(m.depth()).arg(m.width));
        }
    }
    hint_->setVisible(store_ != nullptr && memories_.empty());
    // The list is as high as a few of its lines (it scrolls beyond six); the grid gets the rest.
    const int lines = std::clamp(static_cast<int>(memories_.size()), 1, 6);
    list_->setMaximumHeight(list_->header()->sizeHint().height() +
                            lines * (list_->fontMetrics().height() + 6) + 2 * list_->frameWidth() + 4);
    message_.clear();
    if (!memories_.empty())
        list_->setCurrentItem(list_->topLevelItem(0)); // shows it
    else
        selectMemoryRow(-1);
}

void MemoryWindow::refresh() {
    if (!store_)
        return;
    applyTime();
    model_->refresh();
    updateStatus(message_);
}

QStringList MemoryWindow::memoryPaths() const {
    QStringList out;
    for (const MemoryInfo& m : memories_)
        out << QString::fromStdString(m.path);
    return out;
}

void MemoryWindow::selectMemoryRow(int row) {
    message_.clear();
    if (row < 0 || row >= static_cast<int>(memories_.size()))
        model_->setMemory(std::nullopt);
    else
        model_->setMemory(memories_[static_cast<std::size_t>(row)]);
    applyTime();
    table_->resizeColumnsToContents();
    updateStatus();
}

bool MemoryWindow::showMemory(const QString& path) {
    if (!store_)
        return false;
    QString p = path.trimmed();
    while (p.startsWith('/'))
        p.remove(0, 1);
    p.replace('/', '.');
    for (int i = 0; i < static_cast<int>(memories_.size()); ++i) {
        if (QString::fromStdString(memories_[static_cast<std::size_t>(i)].path)
                .compare(p, Qt::CaseInsensitive) == 0) {
            if (list_->currentItem() != list_->topLevelItem(i))
                list_->setCurrentItem(list_->topLevelItem(i));
            return true;
        }
    }
    return false;
}

QString MemoryWindow::currentMemory() const {
    const MemoryImage* img = model_->image();
    return img ? QString::fromStdString(img->info().path) : QString();
}

bool MemoryWindow::applyRequest(const AddMemRequest& request, QString* error) {
    QString bad;
    QString found;
    for (const std::string& p : request.paths) {
        const QString q = QString::fromStdString(p);
        if (showMemory(q))
            found = q;
        else
            bad += (bad.isEmpty() ? QString() : QStringLiteral(", ")) + q;
    }
    if (!found.isEmpty()) {
        if (request.wordsPerLine)
            setWordsPerLine(static_cast<int>(*request.wordsPerLine));
        if (request.addressRadix)
            setAddressRadix(*request.addressRadix);
        if (request.dataRadix)
            setDataRadix(*request.dataRadix);
    }
    if (!bad.isEmpty() && error)
        *error = memories_.empty() ? tr("no memory %1 (no memories are recorded; start the simulation with "
                                        "Simulation > Record memories or -lvdarrays)")
                                         .arg(bad)
                                   : tr("no memory %1").arg(bad);
    return bad.isEmpty() && !request.paths.empty();
}

void MemoryWindow::setTimeSource(TimeSource source) {
    source_ = source;
    {
        const QSignalBlocker block(time_);
        time_->setCurrentIndex(time_->findData(static_cast<int>(source)));
    }
    applyTime();
    updateStatus();
}

void MemoryWindow::showCursorTime(std::optional<double> t) {
    cursor_ = t;
    if (source_ == TimeSource::Cursor) {
        applyTime();
        updateStatus();
    }
}

Time MemoryWindow::shownTime() const {
    const Time end = store_ ? store_->endTime() : 0;
    if (source_ == TimeSource::Cursor && cursor_ && *cursor_ >= 0.0)
        return std::min(end, static_cast<Time>(std::llround(*cursor_)));
    return end;
}

void MemoryWindow::applyTime() {
    model_->setTime(shownTime());
}

void MemoryWindow::setWordsPerLine(int n) {
    const QModelIndex cur = table_->currentIndex();
    const int word = model_->wordAt(cur);
    model_->setWordsPerLine(n);
    {
        const QSignalBlocker block(words_);
        words_->setValue(model_->wordsPerLine());
    }
    table_->resizeColumnsToContents();
    if (word >= 0) {
        const QModelIndex idx = model_->indexOfWord(static_cast<std::size_t>(word));
        table_->setCurrentIndex(idx);
        table_->scrollTo(idx);
    }
}

void MemoryWindow::setAddressRadix(Radix r) {
    model_->setAddressRadix(r);
    const QSignalBlocker block(addressRadix_);
    selectRadix(addressRadix_, r == Radix::Signed ? Radix::Unsigned : r);
}

void MemoryWindow::setDataRadix(Radix r) {
    if (r == Radix::Default)
        r = Radix::Hex;
    model_->setDataRadix(r);
    {
        const QSignalBlocker block(dataRadix_);
        selectRadix(dataRadix_, r);
    }
    table_->resizeColumnsToContents();
}

bool MemoryWindow::goToAddress(const QString& text) {
    const MemoryImage* img = model_->image();
    if (img == nullptr)
        return false;
    const auto a = parseAddress(text.toStdString(), model_->addressRadix());
    const MemoryInfo& m = img->info();
    if (!a || *a < m.low || *a > m.high()) {
        updateStatus(tr("%1 is not an address of %2 (%3 to %4)")
                         .arg(text, QString::fromStdString(m.path),
                              QString::fromStdString(formatAddress(m.low, model_->addressRadix())),
                              QString::fromStdString(formatAddress(m.high(), model_->addressRadix()))));
        return false;
    }
    const QModelIndex idx = model_->indexOfWord(static_cast<std::size_t>(*a - m.low));
    table_->setCurrentIndex(idx);
    table_->scrollTo(idx, QAbstractItemView::PositionAtCenter);
    updateStatus();
    return true;
}

bool MemoryWindow::findValue(const QString& text, int dir) {
    const MemoryImage* img = model_->image();
    if (img == nullptr || text.trimmed().isEmpty())
        return false;
    const auto pattern = ValuePattern::parse(text.trimmed().toStdString(), model_->dataRadix(),
                                             SignalKind::Logic, img->info().width);
    if (!pattern.ok()) {
        updateStatus(tr("Find: %1").arg(QString::fromStdString(pattern.error())));
        return false;
    }
    const int cur = model_->wordAt(table_->currentIndex());
    const std::optional<std::size_t> from =
        cur >= 0 ? std::optional<std::size_t>(static_cast<std::size_t>(cur)) : std::nullopt;
    const auto hit = img->findWord(pattern.value(), model_->time(), from,
                                   dir < 0 ? SearchDirection::Backward : SearchDirection::Forward, true);
    if (!hit) {
        updateStatus(tr("Find: no word is %1").arg(text.trimmed()));
        return false;
    }
    const QModelIndex idx = model_->indexOfWord(*hit);
    table_->setCurrentIndex(idx);
    table_->scrollTo(idx, QAbstractItemView::PositionAtCenter);
    const bool wrapped = from && (dir < 0 ? *hit >= *from : *hit <= *from);
    updateStatus(tr("Found %1 at address %2%3")
                     .arg(text.trimmed(), model_->addressText(*hit), wrapped ? tr(" (wrapped)") : QString()));
    return true;
}

bool MemoryWindow::stepWrite(int dir) {
    const MemoryImage* img = model_->image();
    if (img == nullptr)
        return false;
    const Time now = shownTime();
    const auto t = dir < 0 ? img->previousWriteTime(now) : img->nextWriteTime(now);
    if (!t) {
        updateStatus(dir < 0 ? tr("No earlier write to this memory") : tr("No later write to this memory"));
        return false;
    }
    cursor_ = static_cast<double>(*t);
    setTimeSource(TimeSource::Cursor);
    emit timeSelected(static_cast<double>(*t));
    // Show the first word written there.
    const auto w = img->wordsWrittenAt(*t);
    if (!w.empty()) {
        const QModelIndex idx = model_->indexOfWord(w.front());
        table_->setCurrentIndex(idx);
        table_->scrollTo(idx);
    }
    return true;
}

std::vector<std::size_t> MemoryWindow::selectedWords() const {
    std::vector<std::size_t> out;
    for (const QModelIndex& i : table_->selectionModel()->selectedIndexes()) {
        const int w = model_->wordAt(i);
        if (w >= 0)
            out.push_back(static_cast<std::size_t>(w));
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

QString MemoryWindow::memoryFileText(Radix radix) const {
    const MemoryImage* img = model_->image();
    if (img == nullptr)
        return {};
    const int exp = store_ ? store_->timescaleExponent() : -15;
    const std::string when = formatTime(static_cast<double>(model_->time()), exp);
    return QString::fromStdString(
        img->memoryFileText(model_->time(), radix, static_cast<unsigned>(model_->wordsPerLine()), when));
}

bool MemoryWindow::saveMemoryFile(const QString& path, QString* error) {
    if (model_->image() == nullptr) {
        if (error)
            *error = tr("no memory is shown");
        return false;
    }
    const QString lower = path.toLower();
    const Radix radix = lower.endsWith(".bin") || lower.endsWith(".memb") ? Radix::Binary : Radix::Hex;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error)
            *error = tr("cannot write %1: %2").arg(path, f.errorString());
        return false;
    }
    f.write(memoryFileText(radix).toUtf8());
    updateStatus(tr("Saved %1").arg(path));
    return true;
}

bool MemoryWindow::saveDialog() {
    if (model_->image() == nullptr)
        return false;
    const QString path =
        filePrompt_
            ? filePrompt_()
            : QFileDialog::getSaveFileName(this, tr("Save memory"), {},
                                           tr("Hex memory file (*.mem *.hex);;Binary memory file (*.memb "
                                              "*.bin);;All files (*)"));
    if (path.isEmpty())
        return false;
    QString err;
    if (!saveMemoryFile(path, &err)) {
        updateStatus(err);
        return false;
    }
    return true;
}

QString MemoryWindow::statusText() const {
    return status_->text();
}

void MemoryWindow::updateStatus(const QString& message) {
    message_ = message;
    const MemoryImage* img = model_->image();
    prevWrite_->setEnabled(img != nullptr);
    nextWrite_->setEnabled(img != nullptr);
    if (img == nullptr) {
        status_->setText(message);
        return;
    }
    const int exp = store_ ? store_->timescaleExponent() : -15;
    const auto fmt = [exp](Time t) {
        return QString::fromStdString(formatTime(static_cast<double>(t), exp));
    };
    const MemoryInfo& m = img->info();
    QString s = tr("%1: %2 x %3 bits, at %4")
                    .arg(QString::fromStdString(m.path))
                    .arg(m.depth())
                    .arg(m.width)
                    .arg(fmt(model_->time()));
    if (const auto ht = model_->highlightTime()) {
        const std::size_t n = model_->highlightedWords().size();
        s += tr("; last write %1: %2").arg(fmt(*ht), n == 1 ? tr("1 word") : tr("%1 words").arg(n));
    } else {
        s += tr("; not written yet");
    }
    if (!message.isEmpty())
        s += QStringLiteral("  |  ") + message;
    status_->setText(s);
    status_->setToolTip(s);
}

void MemoryWindow::showContextMenu(const QPoint& pos) {
    const MemoryImage* img = model_->image();
    if (img == nullptr)
        return;
    const std::vector<std::size_t> words = selectedWords();
    QMenu menu(this);
    QAction* add = menu.addAction(words.size() == 1 ? tr("Add word to wave") : tr("Add words to wave"));
    add->setEnabled(!words.empty());
    connect(add, &QAction::triggered, this, [this, words, img] {
        QString base = QString::fromStdString(img->info().path);
        base.replace('.', '/');
        QStringList paths;
        for (std::size_t w : words)
            paths << QStringLiteral("/%1[%2]").arg(base).arg(img->info().low + static_cast<std::int64_t>(w));
        emit addToWave(paths);
    });
    QAction* copy = menu.addAction(tr("Copy"));
    copy->setEnabled(!words.empty());
    connect(copy, &QAction::triggered, this, [this, words] {
        QStringList lines;
        for (std::size_t w : words)
            lines << model_->addressText(w) + QLatin1Char('\t') + model_->valueText(w);
        QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')) + QLatin1Char('\n'));
    });
    menu.addSeparator();
    connect(menu.addAction(tr("Save...")), &QAction::triggered, this, [this] { saveDialog(); });
    menu.exec(table_->viewport()->mapToGlobal(pos));
}

} // namespace lvd::gui
