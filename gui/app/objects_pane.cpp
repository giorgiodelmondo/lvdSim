// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/objects_pane.hpp"

#include <QContextMenuEvent>
#include <functional>
#include <QHeaderView>
#include <QLineEdit>
#include <QMenu>
#include <QRegularExpression>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace lvd::gui {

using namespace lvd::wave;

namespace {

constexpr int kPathRole = Qt::UserRole; // dotted store path of a signal
constexpr int kIsSignalRole = Qt::UserRole + 1;

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

    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(3);
    tree_->setHeaderLabels({tr("Name"), tr("Type"), tr("Width")});
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->setUniformRowHeights(true);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < tree_->columnCount(); ++c)
        tree_->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    connect(tree_, &QTreeWidget::itemActivated, this,
            [this](QTreeWidgetItem*, int) { activate(selectedSignalPaths()); });

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(filter_);
    layout->addWidget(tree_, 1);
}

void ObjectsPane::setStore(const WaveStore* store) {
    store_ = store;
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
    if (store_ == nullptr)
        return;

    const ScopeId id = scope_.isEmpty() ? kNoScope : store_->findScope(scope_.toStdString());
    if (id == kNoScope) {
        populate(nullptr, kRootScope, false);  // the whole hierarchy
        tree_->expandToDepth(1);
    } else {
        populate(nullptr, id, true);  // the signals of one scope, with its sub-scopes below
        tree_->expandToDepth(0);
    }
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
        auto* item = parent != nullptr ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree_);
        item->setText(0, QString::fromStdString(store_->entry(id).name()));
        item->setText(1, kindName(s.kind()));
        item->setText(2, s.kind() == SignalKind::Logic ? QString::number(s.width()) : QString());
        item->setData(0, kPathRole, QString::fromStdString(store_->pathOf(id)));
        item->setData(0, kIsSignalRole, true);
        ++signals_;
    }
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

void ObjectsPane::contextMenuEvent(QContextMenuEvent* e) {
    QMenu menu(this);
    QAction* add = menu.addAction(tr("Add to wave"));
    add->setEnabled(!selectedSignalPaths().isEmpty());
    connect(add, &QAction::triggered, this, [this] { activate(selectedSignalPaths()); });
    menu.addSeparator();
    connect(menu.addAction(tr("Expand all")), &QAction::triggered, tree_, &QTreeWidget::expandAll);
    connect(menu.addAction(tr("Collapse all")), &QAction::triggered, tree_, &QTreeWidget::collapseAll);
    menu.exec(e->globalPos());
}

} // namespace lvd::gui
