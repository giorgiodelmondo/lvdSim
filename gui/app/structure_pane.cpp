// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/structure_pane.hpp"

#include <QContextMenuEvent>
#include <QHeaderView>
#include <QMenu>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace lvd::gui {

using namespace lvd::wave;

namespace {
constexpr int kPathRole = Qt::UserRole;
constexpr int kPackageRole = Qt::UserRole + 1;
} // namespace

StructurePane::StructurePane(QWidget* parent)
    : QWidget(parent) {
    tree_ = new QTreeWidget(this);
    tree_->setHeaderLabels({tr("Instance"), tr("Kind")});
    tree_->setUniformRowHeights(true);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < tree_->columnCount(); ++c)
        tree_->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    connect(tree_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
                if (current != nullptr)
                    emit scopeSelected(current->data(0, kPathRole).toString());
            });
    // Double-click on an instance shows its definition; scopes with children still expand or collapse.
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        const QString path = definitionPathOf(item);
        if (path.isEmpty() || (gate_ && !gate_().isEmpty()))
            return;
        emit viewDefinitionRequested(path);
    });
    tree_->setContextMenuPolicy(Qt::NoContextMenu); // the pane's contextMenuEvent builds the menu

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(tree_);
}

void StructurePane::setStore(const WaveStore* store) {
    store_ = store;
    tree_->clear();
    scopes_ = 0;
    if (store_ == nullptr)
        return;

    auto* root = new QTreeWidgetItem(tree_);
    root->setText(0, tr("design"));
    root->setData(0, kPathRole, QString());
    populate(root, kRootScope);
    root->setExpanded(true);
    tree_->setCurrentItem(root);
}

void StructurePane::populate(QTreeWidgetItem* parent, ScopeId scope) {
    for (ScopeId c : store_->scope(scope).children) {
        const Scope& sc = store_->scope(c);
        auto* item = new QTreeWidgetItem(parent);
        item->setText(0, QString::fromStdString(sc.name));
        // Scope type numbers of the FST format (fstapi.h, enum fstScopeType)
        switch (sc.type) {
        case 10: // Verilog package
        case 21: // VHDL package
            item->setText(1, tr("package"));
            item->setData(0, kPackageRole, true);
            break;
        case 6:  // Verilog struct
        case 15: // VHDL record
            item->setText(1, tr("record"));
            break;
        case 5:  // Verilog generate
        case 18: // VHDL for-generate
        case 19: // VHDL if-generate
        case 20: // VHDL generate
            item->setText(1, tr("generate"));
            break;
        case 17: // VHDL block
            item->setText(1, tr("block"));
            break;
        default:
            item->setText(1, tr("instance"));
            break;
        }
        item->setData(0, kPathRole, QString::fromStdString(store_->scopePath(c)));
        ++scopes_;
        populate(item, c);
    }
}

QString StructurePane::definitionPathOf(const QTreeWidgetItem* item) const {
    if (item == nullptr || item->data(0, kPackageRole).toBool())
        return {};
    return item->data(0, kPathRole).toString();
}

QMenu* StructurePane::createContextMenu(QTreeWidgetItem* item) {
    auto* menu = new QMenu(this);
    menu->setObjectName("structureContextMenu");
    const QString path = definitionPathOf(item);
    const QString why = gate_ ? gate_() : QString();
    const bool top = !path.isEmpty() && !path.contains(QLatin1Char('.'));
    QAction* def = menu->addAction(tr("View Definition"));
    def->setObjectName("viewDefinition");
    def->setEnabled(!path.isEmpty() && why.isEmpty());
    def->setToolTip(path.isEmpty() ? tr("Select an instance") : why);
    connect(def, &QAction::triggered, this, [this, path] { emit viewDefinitionRequested(path); });
    QAction* inst = menu->addAction(tr("View Instantiation"));
    inst->setObjectName("viewInstantiation");
    inst->setEnabled(!path.isEmpty() && !top && why.isEmpty());
    inst->setToolTip(top              ? tr("The top-level design unit is not instantiated")
                     : path.isEmpty() ? tr("Select an instance")
                                      : why);
    connect(inst, &QAction::triggered, this, [this, path] { emit viewInstantiationRequested(path); });
    return menu;
}

void StructurePane::contextMenuEvent(QContextMenuEvent* e) {
    QTreeWidgetItem* item = tree_->itemAt(tree_->viewport()->mapFromGlobal(e->globalPos()));
    if (item != nullptr)
        tree_->setCurrentItem(item);
    QMenu* menu = createContextMenu(item);
    menu->exec(e->globalPos());
    delete menu;
}

} // namespace lvd::gui
