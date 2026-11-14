// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/locals_pane.hpp"

#include "sim/parsers/vhdl_aggregate.hpp"

#include <QContextMenuEvent>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace lvd::gui {

namespace {

constexpr int kPathRole = Qt::UserRole;
constexpr int kMaxDepth = 8;
const QColor kChanged(0xc8, 0x28, 0x28);

} // namespace

LocalsPane::LocalsPane(QWidget* parent)
    : QWidget(parent) {
    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(3);
    tree_->setHeaderLabels({tr("Name"), tr("Type"), tr("Value")});
    tree_->setUniformRowHeights(true);
    tree_->header()->setStretchLastSection(true);
    tree_->setToolTip(tr("Variables of the selected frame; values changed since the last stop are red"));
    note_ = new QLabel(this);
    note_->setWordWrap(true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->addWidget(tree_, 1);
    layout->addWidget(note_);
    connect(tree_, &QTreeWidget::itemExpanded, this,
            [this](QTreeWidgetItem* it) { expanded_.insert(it->data(0, kPathRole).toString()); });
    connect(tree_, &QTreeWidget::itemCollapsed, this,
            [this](QTreeWidgetItem* it) { expanded_.remove(it->data(0, kPathRole).toString()); });
    clear(tr("Not stopped."));
}

void LocalsPane::clear(const QString& note) {
    tree_->clear();
    variables_.clear();
    items_.clear();
    shown_.clear();
    changed_.clear();
    note_->setText(note);
}

void LocalsPane::forgetHistory() {
    lastSeen_.clear();
    baseline_.clear();
    seenAt_.clear();
    expanded_.clear();
}

void LocalsPane::setLocals(const QString& scope, const LocalsReply& reply, int stopSerial,
                           const QString& emptyNote) {
    clear(reply.variables.isEmpty() ? emptyNote : QString());
    variables_ = reply.variables;
    // The first time this frame is shown at this stop, the values seen at the stop before are the baseline.
    if (seenAt_.value(scope, -1) != stopSerial) {
        baseline_[scope] = lastSeen_.value(scope);
        seenAt_[scope] = stopSerial;
    }
    compare_ = &baseline_[scope];
    for (const VariableInfo& v : reply.variables) {
        auto* it = new QTreeWidgetItem(tree_);
        it->setText(0, v.name);
        it->setText(1, v.type);
        it->setToolTip(0, v.kind);
        if (v.kind == QLatin1String("constant") || v.kind == QLatin1String("parameter"))
            it->setText(0, QStringLiteral("%1 (%2)").arg(v.name, v.kind));
        if (!v.hasValue) {
            it->setData(0, kPathRole, v.name);
            it->setText(2, tr("(not shown)"));
            it->setForeground(2, palette().color(QPalette::Disabled, QPalette::Text));
            it->setToolTip(2, tr("The simulator cannot show values of this type"));
            items_.insert(v.name, it);
            continue;
        }
        addValue(it, v.name, v.value, 0);
    }
    compare_ = nullptr;
    lastSeen_[scope] = shown_;
    tree_->resizeColumnToContents(0);
}

void LocalsPane::addValue(QTreeWidgetItem* item, const QString& path, const QString& value, int depth) {
    item->setData(0, kPathRole, path);
    item->setText(2, value);
    item->setToolTip(2, value);
    items_.insert(path, item);
    shown_.insert(path, value);
    if (compare_ != nullptr) {
        const auto old = compare_->constFind(path);
        if (old != compare_->constEnd() && *old != value) {
            changed_.insert(path);
            item->setForeground(2, kChanged);
            QFont f = item->font(2);
            f.setBold(true);
            item->setFont(2, f);
        }
    }
    if (depth >= kMaxDepth)
        return;
    const auto elements = splitAggregate(value);
    if (!elements || elements->size() < 1)
        return;
    bool named = false;
    for (const AggregateElement& e : *elements)
        named = named || !e.name.isEmpty();
    if (!named && elements->size() < 2)
        return;
    int position = 0;
    for (const AggregateElement& e : *elements) {
        ++position;
        auto* child = new QTreeWidgetItem(item);
        const QString label = e.name.isEmpty() ? QStringLiteral("#%1").arg(position) : e.name;
        child->setText(0, label);
        const QString childPath = e.name.isEmpty() ? QStringLiteral("%1#%2").arg(path).arg(position)
                                                   : QStringLiteral("%1.%2").arg(path, e.name);
        addValue(child, childPath, e.value, depth + 1);
    }
    if (expanded_.contains(path))
        item->setExpanded(true);
}

QString LocalsPane::valueOf(const QString& path) const {
    const QTreeWidgetItem* it = items_.value(path);
    return it == nullptr ? QString() : it->text(2);
}

QStringList LocalsPane::changedPaths() const {
    QStringList l(changed_.begin(), changed_.end());
    l.sort();
    return l;
}

QTreeWidgetItem* LocalsPane::itemOf(const QString& path) const {
    return items_.value(path);
}

QString LocalsPane::note() const {
    return note_->text();
}

QString LocalsPane::variableOf(const QTreeWidgetItem* item) {
    while (item != nullptr && item->parent() != nullptr)
        item = item->parent();
    return item == nullptr ? QString() : item->text(0);
}

QMenu* LocalsPane::createContextMenu(QTreeWidgetItem* item) {
    auto* menu = new QMenu(this);
    menu->setObjectName("localsContextMenu");
    const QString var = variableOf(item);
    const QString why = var.isEmpty() ? tr("Select a variable") : gate_ ? gate_(var) : QString();
    QAction* decl = menu->addAction(tr("View Declaration"));
    decl->setObjectName("viewDeclaration");
    decl->setEnabled(why.isEmpty());
    decl->setToolTip(why);
    connect(decl, &QAction::triggered, this, [this, var] { emit viewDeclarationRequested(var); });
    return menu;
}

void LocalsPane::contextMenuEvent(QContextMenuEvent* e) {
    QTreeWidgetItem* item = tree_->itemAt(tree_->viewport()->mapFromGlobal(e->globalPos()));
    if (item != nullptr)
        tree_->setCurrentItem(item);
    QMenu* menu = createContextMenu(item);
    menu->exec(e->globalPos());
    delete menu;
}

} // namespace lvd::gui
