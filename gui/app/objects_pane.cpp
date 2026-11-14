// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/objects_pane.hpp"

#include "app/port_icons.hpp"
#include "wave/signal_mime.hpp"

#include <lvdwave/memory.hpp>

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDrag>
#include <QFontMetrics>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QRegularExpression>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <functional>

namespace lvd::gui {

using namespace lvd::wave;

namespace {

constexpr int kPathRole = Qt::UserRole; // dotted store path of a signal
constexpr int kIsSignalRole = Qt::UserRole + 1;
constexpr int kModeRole = Qt::UserRole + 2;   // PortMode as int
constexpr int kMemoryRole = Qt::UserRole + 3; // dotted path of the memory a group item stands for

/// The tree of the pane. A drag is started by the pane (it knows what a scope stands for and draws the
/// picture).
class ObjectsTree : public QTreeWidget {
public:
    ObjectsTree(ObjectsPane* owner, QWidget* parent)
        : QTreeWidget(parent)
        , owner_(owner) {}

protected:
    void startDrag(Qt::DropActions) override { owner_->startSignalDrag(); }
    void keyPressEvent(QKeyEvent* e) override {
        if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && e->modifiers() == Qt::NoModifier) {
            owner_->activateSelection();
            return;
        }
        QTreeWidget::keyPressEvent(e);
    }

private:
    ObjectsPane* owner_;
};

/// The pane has no icon for linkage ports; they keep the neutral one.
PortMode portModeFromDirection(PortDirection d) {
    switch (d) {
    case PortDirection::In:
        return PortMode::In;
    case PortDirection::Out:
        return PortMode::Out;
    case PortDirection::InOut:
        return PortMode::InOut;
    case PortDirection::Buffer:
        return PortMode::Buffer;
    case PortDirection::Internal:
        return PortMode::Internal;
    case PortDirection::Linkage:
    case PortDirection::Unknown:
        break;
    }
    return PortMode::Unknown;
}

const char* kindName(SignalKind k) {
    switch (k) {
    case SignalKind::Logic:
        return "logic";
    case SignalKind::Real:
        return "real";
    case SignalKind::String:
        return "text";
    }
    return "";
}

} // namespace

ObjectsPane::ObjectsPane(QWidget* parent)
    : QWidget(parent) {
    filter_ = new QLineEdit(this);
    filter_->setPlaceholderText(tr("Filter signals (* and ? allowed)"));
    filter_->setClearButtonEnabled(true);
    connect(filter_, &QLineEdit::textChanged, this, [this](const QString& t) { setFilter(t); });

    tree_ = new ObjectsTree(this, this);
    tree_->setDragEnabled(true);
    tree_->setDragDropMode(QAbstractItemView::DragOnly);
    tree_->setIconSize(QSize(16, 16));
    tree_->setColumnCount(3);
    tree_->setHeaderLabels({tr("Name"), tr("Type"), tr("Width")});
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->setUniformRowHeights(true);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < tree_->columnCount(); ++c)
        tree_->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    // Double-click shows the declaration; Return adds to the wave (ObjectsTree::keyPressEvent). itemActivated
    // is not used: platforms emit it for double-clicks too.
    connect(tree_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) { itemDoubleClicked(item); });

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(filter_);
    layout->addWidget(tree_, 1);
}

void ObjectsPane::setStore(const WaveStore* store) {
    store_ = store;
    modes_.clear();
    rebuild();
}

void ObjectsPane::setScope(const QString& dottedPath) {
    if (scope_ == dottedPath)
        return;
    scope_ = dottedPath;
    rebuild();
}

void ObjectsPane::rebuild() {
    tree_->clear();
    scopes_ = signals_ = 0;
    memoryOf_.clear();
    memoryItems_.clear();
    if (store_ == nullptr)
        return;
    for (const MemoryInfo& m : findMemories(*store_))
        for (SignalId id : m.words)
            if (id != kNoSignal)
                memoryOf_.insert(id, QString::fromStdString(m.path));

    const ScopeId id = scope_.isEmpty() ? kNoScope : store_->findScope(scope_.toStdString());
    if (id == kNoScope) {
        populate(nullptr, kRootScope, false); // the whole hierarchy
        tree_->expandToDepth(1);
    } else {
        populate(nullptr, id, true); // the signals of one scope, with its sub-scopes below
        tree_->expandToDepth(0);
    }
    for (QTreeWidgetItem* m : std::as_const(memoryItems_))
        m->setExpanded(false); // thousands of elements stay out of the way until asked for
    setFilter(filter_->text());
}

void ObjectsPane::populate(QTreeWidgetItem* parent, ScopeId scope, bool ownSignals) {
    const Scope& sc = store_->scope(scope);
    for (ScopeId c : sc.children) {
        auto* item = parent != nullptr ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree_);
        item->setText(0, QString::fromStdString(store_->scope(c).name));
        item->setText(1, tr("scope"));
        item->setData(0, kIsSignalRole, false);
        ++scopes_;
        populate(item, c, true);
    }
    if (parent == nullptr && !ownSignals)
        return; // the unnamed root holds no signals
    for (SignalId id : sc.signalIds) {
        const Signal& s = store_->signal(id);
        QTreeWidgetItem* under = parent;
        if (const auto mem = memoryOf_.constFind(id); mem != memoryOf_.constEnd()) {
            QTreeWidgetItem*& group = memoryItems_[mem.value()];
            if (group == nullptr) {
                group = parent != nullptr ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree_);
                const qsizetype dot = mem.value().lastIndexOf('.');
                group->setText(0, dot < 0 ? mem.value() : mem.value().mid(dot + 1));
                group->setText(1, tr("memory"));
                group->setData(0, kIsSignalRole, false);
                group->setData(0, kMemoryRole, mem.value());
                group->setIcon(0, portModeIcon(portModeFromDirection(store_->entry(id).direction())));
            }
            under = group;
        }
        auto* item = under != nullptr ? new QTreeWidgetItem(under) : new QTreeWidgetItem(tree_);
        item->setText(0, QString::fromStdString(store_->entry(id).name()));
        item->setText(1, kindName(s.kind()));
        item->setText(2, s.kind() == SignalKind::Logic ? QString::number(s.width()) : QString());
        item->setData(0, kPathRole, QString::fromStdString(store_->pathOf(id)));
        item->setData(0, kIsSignalRole, true);
        applyMode(item, portModeOf(item->data(0, kPathRole).toString()));
        ++signals_;
    }
    for (QTreeWidgetItem* group : std::as_const(memoryItems_)) {
        if (group->parent() != parent || !group->text(2).isEmpty())
            continue;
        group->setText(2, QStringLiteral("%1 x %2").arg(group->childCount()).arg(group->child(0)->text(2)));
        const QString tip = tr("%1: memory of %2 words, %3 bits each; context menu: Open in Memory window")
                                .arg(group->data(0, kMemoryRole).toString())
                                .arg(group->childCount())
                                .arg(group->child(0)->text(2));
        for (int c = 0; c < group->columnCount(); ++c)
            group->setToolTip(c, tip);
    }
}

QString ObjectsPane::memoryPathOf(const QTreeWidgetItem* item) const {
    for (; item != nullptr; item = item->parent()) {
        const QString m = item->data(0, kMemoryRole).toString();
        if (!m.isEmpty())
            return m;
        if (!item->data(0, kIsSignalRole).toBool())
            return {}; // a scope
    }
    return {};
}

QString ObjectsPane::currentMemoryPath() const {
    return memoryPathOf(tree_->currentItem());
}

QTreeWidgetItem* ObjectsPane::memoryItem(const QString& dottedPath) const {
    return memoryItems_.value(dottedPath, nullptr);
}

void ObjectsPane::applyMode(QTreeWidgetItem* item, PortMode mode) const {
    item->setData(0, kModeRole, static_cast<int>(mode));
    item->setIcon(0, portModeIcon(mode));
    QString tip = tr("%1: %2").arg(item->text(0), portModeText(mode));
    const QString kind = item->text(1);
    if (!kind.isEmpty()) {
        tip += tr(", %1").arg(kind);
        if (!item->text(2).isEmpty())
            tip += tr(" (%1 bits)").arg(item->text(2));
    }
    for (int c = 0; c < item->columnCount(); ++c)
        item->setToolTip(c, tip);
}

void ObjectsPane::setPortModes(const QHash<QString, PortMode>& modes) {
    for (auto it = modes.begin(); it != modes.end(); ++it)
        modes_.insert(it.key(), it.value());
    std::function<void(QTreeWidgetItem*)> walk = [&](QTreeWidgetItem* item) {
        if (item->data(0, kIsSignalRole).toBool()) {
            const auto it = modes.constFind(item->data(0, kPathRole).toString());
            if (it != modes.constEnd())
                applyMode(item, it.value());
        }
        for (int i = 0; i < item->childCount(); ++i)
            walk(item->child(i));
    };
    for (int i = 0; i < tree_->topLevelItemCount(); ++i)
        walk(tree_->topLevelItem(i));
}

PortMode ObjectsPane::portModeOf(const QString& dottedPath) const {
    const auto it = modes_.constFind(dottedPath);
    if (it != modes_.constEnd())
        return it.value();
    if (store_ == nullptr)
        return PortMode::Unknown;
    const SignalId id = store_->findSignal(dottedPath.toStdString());
    return id == kNoSignal ? PortMode::Unknown : portModeFromDirection(store_->entry(id).direction());
}

QMimeData* ObjectsPane::mimeDataForSelection() const {
    const QStringList dotted = selectedSignalPaths();
    if (dotted.isEmpty())
        return nullptr;
    QStringList paths;
    for (const QString& d : dotted)
        paths << addWavePathFromStorePath(d);
    return makeSignalMime(paths);
}

QPixmap ObjectsPane::dragPixmap(const QStringList& names, qreal dpr) {
    constexpr int kMaxLines = 4;
    const QFont font = QApplication::font();
    const QFontMetrics fm(font);
    const int shown = static_cast<int>(std::min<qsizetype>(names.size(), kMaxLines));
    QStringList lines;
    for (int i = 0; i < shown; ++i)
        lines << fm.elidedText(names.at(i), Qt::ElideMiddle, 260);
    if (names.size() > shown)
        lines << tr("... and %1 more").arg(names.size() - shown);
    int w = 0;
    for (const QString& l : lines)
        w = std::max(w, fm.horizontalAdvance(l));
    const QString count = names.size() == 1 ? tr("1 signal") : tr("%1 signals").arg(names.size());
    w = std::max(w, fm.horizontalAdvance(count));
    const int pad = 6;
    const int lineH = fm.height() + 1;
    const QSize size(w + 2 * pad, static_cast<int>(lines.size() + 1) * lineH + 2 * pad);

    QPixmap pm(size * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPalette pal = QApplication::palette();
    QColor bg = pal.color(QPalette::ToolTipBase);
    bg.setAlpha(225);
    p.setPen(QPen(QColor(0x4a, 0xa8, 0xff), 1.5));
    p.setBrush(bg);
    p.drawRoundedRect(QRectF(0.75, 0.75, size.width() - 1.5, size.height() - 1.5), 4, 4);
    p.setFont(font);
    p.setPen(pal.color(QPalette::ToolTipText));
    int y = pad;
    for (const QString& l : lines) {
        p.drawText(QRect(pad, y, w, lineH), Qt::AlignVCenter | Qt::AlignLeft, l);
        y += lineH;
    }
    p.setPen(QColor(0x4a, 0xa8, 0xff));
    p.drawText(QRect(pad, y, w, lineH), Qt::AlignVCenter | Qt::AlignLeft, count);
    return pm;
}

bool ObjectsPane::startSignalDrag() {
    const QStringList dotted = selectedSignalPaths();
    QMimeData* mime = mimeDataForSelection();
    if (mime == nullptr)
        return false;
    QStringList names;
    for (const QString& d : dotted) {
        const qsizetype dot = d.lastIndexOf('.');
        names << (dot < 0 ? d : d.mid(dot + 1));
    }
    auto* drag = new QDrag(tree_);
    drag->setMimeData(mime);
    const qreal dpr = tree_->devicePixelRatioF();
    const QPixmap pm = dragPixmap(names, dpr);
    drag->setPixmap(pm);
    drag->setHotSpot(QPoint(8, 8));
    if (dragRunner_)
        dragRunner_(drag);
    else
        drag->exec(Qt::CopyAction, Qt::CopyAction);
    return true;
}

void ObjectsPane::collect(QTreeWidgetItem* item, QStringList& out) const {
    if (item->isHidden())
        return;
    if (item->data(0, kIsSignalRole).toBool()) {
        out << item->data(0, kPathRole).toString();
        return;
    }
    for (int i = 0; i < item->childCount(); ++i)
        collect(item->child(i), out);
}

QStringList ObjectsPane::selectedSignalPaths() const {
    QStringList out;
    // Selecting a scope and one of its signals would list that signal twice.
    for (QTreeWidgetItem* it : tree_->selectedItems()) {
        QStringList part;
        collect(it, part);
        for (const QString& p : part)
            if (!out.contains(p))
                out << p;
    }
    return out;
}

bool ObjectsPane::applyFilter(QTreeWidgetItem* item, const QString& pattern) {
    if (item->data(0, kIsSignalRole).toBool()) {
        const bool match = pattern.isEmpty() ||
                           QRegularExpression::fromWildcard(pattern, Qt::CaseInsensitive,
                                                            QRegularExpression::UnanchoredWildcardConversion)
                               .match(item->text(0))
                               .hasMatch();
        item->setHidden(!match);
        return match;
    }
    bool any = false;
    for (int i = 0; i < item->childCount(); ++i)
        any = applyFilter(item->child(i), pattern) || any;
    item->setHidden(!pattern.isEmpty() && !any);
    if (!pattern.isEmpty() && any)
        item->setExpanded(true);
    return any;
}

void ObjectsPane::setFilter(const QString& text) {
    if (filter_->text() != text)
        filter_->setText(text);
    for (int i = 0; i < tree_->topLevelItemCount(); ++i)
        applyFilter(tree_->topLevelItem(i), text.trimmed());
}

int ObjectsPane::visibleSignalCount() const {
    int n = 0;
    std::function<void(QTreeWidgetItem*)> walk = [&](QTreeWidgetItem* it) {
        if (it->isHidden())
            return;
        if (it->data(0, kIsSignalRole).toBool())
            ++n;
        for (int i = 0; i < it->childCount(); ++i)
            walk(it->child(i));
    };
    for (int i = 0; i < tree_->topLevelItemCount(); ++i)
        walk(tree_->topLevelItem(i));
    return n;
}

void ObjectsPane::activate(const QStringList& paths) {
    if (!paths.isEmpty())
        emit addToWave(paths);
}

QString ObjectsPane::declarationPathOf(const QTreeWidgetItem* item) const {
    if (item == nullptr)
        return {};
    const QString mem = item->data(0, kMemoryRole).toString();
    if (!mem.isEmpty())
        return mem;
    if (item->data(0, kIsSignalRole).toBool())
        return item->data(0, kPathRole).toString();
    return {};
}

void ObjectsPane::itemDoubleClicked(QTreeWidgetItem* item) {
    const QString path = declarationPathOf(item);
    if (path.isEmpty())
        return; // a scope: the tree expands or collapses it
    if (declarationGate_ && !declarationGate_().isEmpty())
        return;
    emit viewDeclarationRequested(path);
}

QMenu* ObjectsPane::createContextMenu() {
    auto* menu = new QMenu(this);
    menu->setObjectName("objectsContextMenu");
    const QStringList selected = selectedSignalPaths();
    const QString declPath = declarationPathOf(tree_->currentItem());
    QAction* decl = menu->addAction(tr("View Declaration"));
    decl->setObjectName("viewDeclaration");
    const QString why = declarationGate_ ? declarationGate_() : QString();
    decl->setEnabled(!declPath.isEmpty() && why.isEmpty());
    decl->setToolTip(declPath.isEmpty() ? tr("Select a signal") : why);
    connect(decl, &QAction::triggered, this, [this, declPath] { emit viewDeclarationRequested(declPath); });
    menu->addSeparator();
    QAction* add = menu->addAction(tr("Add to Wave"));
    add->setObjectName("addToWave");
    add->setEnabled(!selected.isEmpty());
    connect(add, &QAction::triggered, this, [this] { activate(selectedSignalPaths()); });
    QAction* list = menu->addAction(tr("Add to List"));
    list->setObjectName("addToList");
    list->setEnabled(!selected.isEmpty());
    connect(list, &QAction::triggered, this, [this] {
        const QStringList paths = selectedSignalPaths();
        if (!paths.isEmpty())
            emit addToList(paths);
    });
    const QString mem = currentMemoryPath();
    QAction* open = menu->addAction(tr("Add to Memory"));
    open->setObjectName("openMemory");
    open->setEnabled(!mem.isEmpty());
    open->setToolTip(mem.isEmpty() ? tr("Only for memories (arrays of words)")
                                   : tr("Show %1 in the Memory window").arg(mem));
    connect(open, &QAction::triggered, this, [this, mem] { emit openMemory(mem); });
    QAction* copy = menu->addAction(tr("Copy Path"));
    copy->setObjectName("copyPath");
    const QString one = declPath.isEmpty() ? QString() : addWavePathFromStorePath(declPath);
    copy->setEnabled(!selected.isEmpty() || !one.isEmpty());
    connect(copy, &QAction::triggered, this, [this, one] {
        QStringList paths;
        for (const QTreeWidgetItem* it : tree_->selectedItems()) {
            const QString d = declarationPathOf(it);
            if (!d.isEmpty() && !paths.contains(addWavePathFromStorePath(d)))
                paths << addWavePathFromStorePath(d);
        }
        if (paths.isEmpty() && !one.isEmpty())
            paths << one;
        QApplication::clipboard()->setText(paths.join(QLatin1Char('\n')));
    });
    menu->addSeparator();
    connect(menu->addAction(tr("Expand all")), &QAction::triggered, tree_, &QTreeWidget::expandAll);
    connect(menu->addAction(tr("Collapse all")), &QAction::triggered, tree_, &QTreeWidget::collapseAll);
    return menu;
}

void ObjectsPane::contextMenuEvent(QContextMenuEvent* e) {
    if (QTreeWidgetItem* item = tree_->itemAt(tree_->viewport()->mapFromGlobal(e->globalPos()));
        item != nullptr && !item->isSelected()) {
        tree_->clearSelection();
        item->setSelected(true);
        tree_->setCurrentItem(item);
    }
    QMenu* menu = createContextMenu();
    menu->exec(e->globalPos());
    delete menu;
}

} // namespace lvd::gui
