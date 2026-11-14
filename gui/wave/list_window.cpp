// SPDX-License-Identifier: GPL-3.0-or-later
#include "wave/list_window.hpp"

#include "wave/icons.hpp"
#include "wave/signal_mime.hpp"
#include "wave/view_state.hpp"

#include <QAction>
#include <QActionGroup>
#include <QClipboard>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace lvd::gui {

using namespace lvd::wave;

ListWindow::ListWindow(QWidget* parent)
    : QWidget(parent) {
    setObjectName("listWindow");
    model_ = new ListModel(this);

    rowMode_ = new QComboBox(this);
    rowMode_->setObjectName("rowModeBox");
    rowMode_->addItem(tr("on any change"), static_cast<int>(ListTrigger::AnyChange));
    rowMode_->addItem(tr("on changes of"), static_cast<int>(ListTrigger::Signal));
    rowMode_->addItem(tr("every"), static_cast<int>(ListTrigger::Strobe));
    rowMode_->setToolTip(
        tr("When a row is printed: at every time step in which a listed signal changes, when one "
           "trigger signal changes, or at a fixed interval (strobe)"));
    trigger_ = new QComboBox(this);
    trigger_->setObjectName("triggerBox");
    trigger_->setMinimumContentsLength(12);
    trigger_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    strobe_ = new QLineEdit(QStringLiteral("10 ns"), this);
    strobe_->setObjectName("strobeEdit");
    strobe_->setFixedWidth(80);
    strobeStart_ = new QLineEdit(QStringLiteral("0 ns"), this);
    strobeStart_->setObjectName("strobeStartEdit");
    strobeStart_->setFixedWidth(80);
    strobeStartLabel_ = new QLabel(tr("from"), this);
    count_ = new QLabel(this);
    count_->setObjectName("rowCountLabel");
    count_->setSizePolicy(QSizePolicy::Ignored,
                          QSizePolicy::Preferred); // a narrow dock must not be held wide by it
    count_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto* exportButton = new QToolButton(this);
    exportButton->setObjectName("exportCsv");
    exportButton->setText(tr("Export CSV..."));
    exportButton->setToolTip(tr("Write every row to a CSV file (UTF-8, time unit in the header)"));
    exportButton->setToolButtonStyle(Qt::ToolButtonTextOnly);

    auto* top = new QHBoxLayout;
    top->setContentsMargins(4, 2, 4, 2);
    top->addWidget(new QLabel(tr("Rows:"), this));
    top->addWidget(rowMode_);
    top->addWidget(trigger_);
    top->addWidget(strobe_);
    top->addWidget(strobeStartLabel_);
    top->addWidget(strobeStart_);
    top->addWidget(count_, 1);
    top->addWidget(exportButton);

    table_ = new QTableView(this);
    table_->setObjectName("listTable");
    table_->setModel(model_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setWordWrap(false);
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->setVisible(false);
    table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table_->verticalHeader()->setDefaultSectionSize(table_->fontMetrics().height() + 4);
    table_->horizontalHeader()->setSectionsMovable(true);
    table_->horizontalHeader()->setHighlightSections(false);
    table_->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    table_->horizontalHeader()->setDefaultSectionSize(110);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->setAcceptDrops(true);
    table_->viewport()->setAcceptDrops(true);
    table_->viewport()->installEventFilter(this);
    table_->installEventFilter(this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(top);
    layout->addWidget(table_, 1);

    connect(exportButton, &QToolButton::clicked, this, [this] { exportCsvDialog(); });
    connect(rowMode_, &QComboBox::currentIndexChanged, this, [this] {
        updatingUi_ = true; // keep the user's choice in the box while the fields are shown or hidden
        updateRowModeUi();
        updatingUi_ = false;
        applyRowMode();
    });
    connect(trigger_, &QComboBox::currentIndexChanged, this, [this] { applyRowMode(); });
    connect(strobe_, &QLineEdit::editingFinished, this, [this] { applyRowMode(); });
    connect(strobeStart_, &QLineEdit::editingFinished, this, [this] { applyRowMode(); });
    connect(model_, &ListModel::columnsChanged, this, [this] { updateRowModeUi(); });
    connect(model_, &QAbstractItemModel::modelReset, this, [this] { updateCount(); });
    connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex& cur) {
                if (!syncing_ && cur.isValid())
                    emit timeSelected(static_cast<double>(model_->rowTime(cur.row())));
            });
    connect(table_->horizontalHeader(), &QHeaderView::customContextMenuRequested, this,
            [this](const QPoint& pos) {
                const int col = table_->horizontalHeader()->logicalIndexAt(pos);
                if (col < 0)
                    return;
                QMenu* menu = createHeaderMenu(col);
                menu->exec(table_->horizontalHeader()->mapToGlobal(pos));
                delete menu;
            });
    // Dragging a header reorders the signals in the model; the header itself keeps its natural order.
    connect(table_->horizontalHeader(), &QHeaderView::sectionMoved, this, [this](int, int from, int to) {
        if (syncing_)
            return;
        syncing_ = true;
        table_->horizontalHeader()->moveSection(to, from);
        syncing_ = false;
        if (from > 0 && to > 0)
            model_->moveSignalColumn(from - 1, to - 1);
    });
    updateRowModeUi();
    updateCount();
}

void ListWindow::setStore(std::shared_ptr<const WaveStore> store) {
    model_->setStore(std::move(store));
    updateCount();
}

void ListWindow::refresh() {
    QScrollBar* bar = table_->verticalScrollBar();
    const bool atEnd = model_->rowCount() > 0 && bar->value() >= bar->maximum();
    model_->refresh();
    if (atEnd)
        table_->scrollToBottom();
    updateCount();
}

void ListWindow::updateCount() {
    count_->setText(tr("%1 rows").arg(QLocale().toString(model_->rowCount())));
}

int ListWindow::addList(const AddWaveRequest& request) {
    const WaveStore* store = model_->store();
    if (store == nullptr)
        return 0;
    std::vector<std::string> paths;
    if (request.expr) // an expression column: "=TEXT"
        paths.push_back("=" + *request.expr);
    for (const std::string& pattern : request.patterns) {
        if (space_ != nullptr && !WaveSpace::split(pattern).dataset.empty()) {
            for (std::string& p : space_->expandPattern(pattern, request.recursive))
                paths.push_back(std::move(p));
            continue;
        }
        for (std::string& p : expandWavePattern(*store, pattern, request.recursive))
            paths.push_back(std::move(p));
    }
    const int n = model_->addSignals(paths, request.radix, request.label);
    if (n > 0)
        emit signalsAdded(n);
    updateCount();
    return n;
}

int ListWindow::addPaths(const QStringList& addWavePaths) {
    AddWaveRequest req;
    for (const QString& p : addWavePaths)
        req.patterns.push_back(p.toStdString());
    return addList(req);
}

void ListWindow::clear() {
    model_->clearColumns();
    updateCount();
}

void ListWindow::showTime(double t) {
    const int row = model_->rowAtOrBefore(static_cast<Time>(std::max(0.0, std::floor(t + 1e-9))));
    syncing_ = true;
    if (row < 0) {
        table_->clearSelection();
    } else {
        const QModelIndex idx = model_->index(row, 0);
        table_->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect |
                                                           QItemSelectionModel::Rows);
        table_->scrollTo(idx, QAbstractItemView::EnsureVisible);
    }
    syncing_ = false;
}

QString ListWindow::copySelection() {
    std::vector<int> rows;
    for (const QModelIndex& i : table_->selectionModel()->selectedRows())
        rows.push_back(i.row());
    std::sort(rows.begin(), rows.end());
    if (rows.empty())
        return {};
    const QString text = model_->rowsText(rows, QLatin1Char('\t'));
    QGuiApplication::clipboard()->setText(text);
    return text;
}

bool ListWindow::exportCsv(const QString& path, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return model_->writeCsv(f, error);
}

bool ListWindow::exportCsvDialog() {
    const QString path =
        filePrompt_ ? filePrompt_()
                    : QFileDialog::getSaveFileName(this, tr("Export list as CSV"), QStringLiteral("list.csv"),
                                                   tr("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty())
        return false;
    QString error;
    if (!exportCsv(path, &error)) {
        count_->setText(tr("Export failed: %1").arg(error));
        return false;
    }
    return true;
}

QMenu* ListWindow::createHeaderMenu(int column) {
    auto* menu = new QMenu(this);
    const int sig = column - 1;
    const int n = static_cast<int>(model_->signalColumns().size());
    if (column == 0) {
        QMenu* unit = menu->addMenu(tr("Time unit"));
        unit->setObjectName("timeUnitMenu");
        for (const auto& [exp, name] :
             {std::pair{-15, "fs"}, {-12, "ps"}, {-9, "ns"}, {-6, "us"}, {-3, "ms"}}) {
            QAction* a = unit->addAction(QString::fromLatin1(name));
            a->setObjectName(QStringLiteral("unit_") + QLatin1String(name));
            a->setCheckable(true);
            a->setChecked(model_->timeUnitExp() == exp);
            const int e = exp;
            connect(a, &QAction::triggered, this, [this, e] { model_->setTimeUnitExp(e); });
        }
        return menu;
    }
    if (sig < 0 || sig >= n)
        return menu;
    if (declarationGate_) {
        // Expression columns ("=a and b") have no declaration.
        const QString path =
            QString::fromStdString(model_->signalColumns()[static_cast<std::size_t>(sig)].path);
        const bool expression = path.startsWith(QLatin1Char('='));
        const QString why = expression ? tr("An expression has no declaration") : declarationGate_(path);
        QAction* decl = menu->addAction(tr("View Declaration"));
        decl->setObjectName("viewDeclaration");
        decl->setEnabled(why.isEmpty());
        decl->setToolTip(why);
        connect(decl, &QAction::triggered, this, [this, path] { emit declarationRequested(path); });
        menu->addSeparator();
    }
    QMenu* radix = menu->addMenu(tr("Radix"));
    radix->setObjectName("radixMenu");
    for (Radix r : {Radix::Default, Radix::Binary, Radix::Octal, Radix::Hex, Radix::Unsigned, Radix::Signed,
                    Radix::Ascii}) {
        QAction* a = radix->addAction(QString::fromLatin1(radixName(r)));
        a->setObjectName(QStringLiteral("radix_") + QLatin1String(radixName(r)));
        a->setCheckable(true);
        a->setChecked(model_->signalColumns()[static_cast<std::size_t>(sig)].radix == r);
        connect(a, &QAction::triggered, this, [this, sig, r] { model_->setRadix(sig, r); });
    }
    QAction* left =
        menu->addAction(tr("Move left"), this, [this, sig] { model_->moveSignalColumn(sig, sig - 1); });
    left->setObjectName("moveLeft");
    left->setEnabled(sig > 0);
    QAction* right =
        menu->addAction(tr("Move right"), this, [this, sig] { model_->moveSignalColumn(sig, sig + 1); });
    right->setObjectName("moveRight");
    right->setEnabled(sig + 1 < n);
    QAction* trig = menu->addAction(tr("Print rows on changes of this signal"), this, [this, sig] {
        ListRowMode m = model_->rowMode();
        m.trigger = ListTrigger::Signal;
        m.triggerPath = model_->signalColumns()[static_cast<std::size_t>(sig)].path;
        model_->setRowMode(m);
    });
    trig->setObjectName("useAsTrigger");
    menu->addSeparator();
    QAction* remove = menu->addAction(tr("Remove"), this, [this, sig] {
        model_->removeSignalColumn(sig);
        updateCount();
    });
    remove->setObjectName("removeColumn");
    return menu;
}

void ListWindow::updateRowModeUi() {
    const QSignalBlocker blockMode(rowMode_);
    const QSignalBlocker blockTrigger(trigger_);
    const ListRowMode& m = model_->rowMode();
    if (!updatingUi_) // called for a model change, not for a choice in the box
        rowMode_->setCurrentIndex(rowMode_->findData(static_cast<int>(m.trigger)));
    trigger_->clear();
    for (const ListModel::Column& c : model_->signalColumns()) {
        QString p = QString::fromStdString(c.path);
        if (trigger_->findData(p) < 0)
            trigger_->addItem(
                p.mid(p.indexOf(QLatin1Char('.')) + 1).replace(QLatin1Char('.'), QLatin1Char('/')), p);
    }
    const int ti = trigger_->findData(QString::fromStdString(m.triggerPath));
    trigger_->setCurrentIndex(ti);
    const auto mode = static_cast<ListTrigger>(rowMode_->currentData().toInt());
    trigger_->setVisible(mode == ListTrigger::Signal);
    strobe_->setVisible(mode == ListTrigger::Strobe);
    strobeStart_->setVisible(mode == ListTrigger::Strobe);
    strobeStartLabel_->setVisible(mode == ListTrigger::Strobe);
}

void ListWindow::applyRowMode() {
    ListRowMode m;
    m.trigger = static_cast<ListTrigger>(rowMode_->currentData().toInt());
    const int exp = model_->store() ? model_->store()->timescaleExponent() : -15;
    if (m.trigger == ListTrigger::Signal) {
        if (trigger_->currentIndex() < 0 && trigger_->count() > 0) {
            const QSignalBlocker block(trigger_);
            trigger_->setCurrentIndex(0);
        }
        m.triggerPath = trigger_->currentData().toString().toStdString();
    } else if (m.trigger == ListTrigger::Strobe) {
        const auto period = parseTime(strobe_->text().toStdString(), exp);
        const auto start = parseTime(strobeStart_->text().toStdString(), exp);
        strobe_->setStyleSheet(
            period && *period > 0 ? QString() : QStringLiteral("QLineEdit { border: 1px solid #ff4040; }"));
        strobeStart_->setStyleSheet(start ? QString()
                                          : QStringLiteral("QLineEdit { border: 1px solid #ff4040; }"));
        if (!period || *period <= 0 || !start)
            return;
        m.strobePeriod = static_cast<Time>(*period);
        m.strobeStart = static_cast<Time>(*start);
    }
    model_->setRowMode(m);
    updateCount();
}

bool ListWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == table_->viewport()) {
        switch (event->type()) {
        case QEvent::DragEnter:
        case QEvent::DragMove: {
            auto* e = static_cast<QDragMoveEvent*>(event);
            if (hasSignalPaths(e->mimeData())) {
                e->acceptProposedAction();
                return true;
            }
            break;
        }
        case QEvent::Drop: {
            auto* e = static_cast<QDropEvent*>(event);
            if (hasSignalPaths(e->mimeData())) {
                addPaths(signalPathsFromMime(e->mimeData()));
                e->acceptProposedAction();
                return true;
            }
            break;
        }
        default:
            break;
        }
    } else if (watched == table_ && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->matches(QKeySequence::Copy)) {
            copySelection();
            return true;
        }
        if (ke->key() == Qt::Key_Delete) {
            const int col = table_->currentIndex().column();
            if (col > 0) {
                model_->removeSignalColumn(col - 1);
                updateCount();
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace lvd::gui
