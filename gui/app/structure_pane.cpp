// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/structure_pane.hpp"

#include <QHeaderView>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace lvd::gui {

using namespace lvd::wave;

namespace {
constexpr int kPathRole = Qt::UserRole;
}

StructurePane::StructurePane(QWidget* parent) : QWidget(parent) {
    tree_ = new QTreeWidget(this);
    tree_->setHeaderLabels({tr("Instance"), tr("Kind")});
    tree_->setUniformRowHeights(true);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < tree_->columnCount(); ++c)
        tree_->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    connect(tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
        if (current != nullptr) emit scopeSelected(current->data(0, kPathRole).toString());
    });

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(tree_);
}

void StructurePane::setStore(const WaveStore* store) {
    store_ = store;
    tree_->clear();
    scopes_ = 0;
    if (store_ == nullptr) return;

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
            case 10:  // Verilog package
            case 21:  // VHDL package
                item->setText(1, tr("package"));
                break;
            case 6:   // Verilog struct
            case 15:  // VHDL record
                item->setText(1, tr("record"));
                break;
            case 5:   // Verilog generate
            case 18:  // VHDL for-generate
            case 19:  // VHDL if-generate
            case 20:  // VHDL generate
                item->setText(1, tr("generate"));
                break;
            case 17:  // VHDL block
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

}  // namespace lvd::gui
