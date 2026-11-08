// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/library_pane.hpp"

#include <QContextMenuEvent>
#include <QHeaderView>
#include <QMenu>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace lvd::gui {

namespace {

constexpr int kUnitRole = Qt::UserRole;  // "library.unit" on unit rows and their architecture rows

}  // namespace

LibraryPane::LibraryPane(QWidget* parent) : QWidget(parent) {
    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(2);
    tree_->setHeaderLabels({tr("Library / unit"), tr("Details")});
    tree_->setUniformRowHeights(true);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < tree_->columnCount(); ++c)
        tree_->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    // No activation handler on purpose: a double click only expands or collapses (QTreeView default).
    // Simulating is explicit: context menu "Simulate" or the vsim command.
    tree_->setExpandsOnDoubleClick(true);
    tree_->setContextMenuPolicy(Qt::NoContextMenu);  // the pane's own contextMenuEvent builds the menu

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(tree_);
}

void LibraryPane::setLibraries(const std::vector<lvd::tools::LibraryInfo>& libraries) {
    tree_->clear();
    libraries_ = units_ = 0;

    for (const lvd::tools::LibraryInfo& lib : libraries) {
        auto* libItem = new QTreeWidgetItem(tree_);
        libItem->setText(0, QString::fromStdString(lib.name));
        libItem->setText(1, tr("%n unit(s)", "", static_cast<int>(lib.units.size())));
        libItem->setToolTip(0, QString::fromStdString(lib.path));
        ++libraries_;

        for (const lvd::tools::LibraryUnit& u : lib.units) {
            const QString id = QString::fromStdString(lib.name + "." + u.name);
            auto* unitItem = new QTreeWidgetItem(libItem);
            unitItem->setText(0, QString::fromStdString(u.name));
            unitItem->setText(1, u.elaborated ? tr("elaborated") : (u.architectures.empty() ? tr("entity or package") : tr("entity")));
            unitItem->setData(0, kUnitRole, id);
            ++units_;

            for (const std::string& a : u.architectures) {
                auto* archItem = new QTreeWidgetItem(unitItem);
                archItem->setText(0, QString::fromStdString(a));
                archItem->setText(1, tr("architecture"));
                archItem->setData(0, kUnitRole, id);
            }
        }
    }
    tree_->expandToDepth(0);
}

QMenu* LibraryPane::createContextMenu(QTreeWidgetItem* item) {
    auto* menu = new QMenu(this);
    menu->setObjectName("libraryContextMenu");
    const QString unit = item ? item->data(0, kUnitRole).toString() : QString();
    QAction* sim = menu->addAction(tr("Simulate"));
    sim->setObjectName("simulate");
    sim->setEnabled(!unit.isEmpty());
    connect(sim, &QAction::triggered, this, [this, unit] { emit simulateRequested(unit); });
    menu->addSeparator();
    QAction* refresh = menu->addAction(tr("Refresh"));
    refresh->setObjectName("refresh");
    connect(refresh, &QAction::triggered, this, &LibraryPane::refreshRequested);
    return menu;
}

void LibraryPane::contextMenuEvent(QContextMenuEvent* e) {
    QTreeWidgetItem* item = tree_->itemAt(tree_->viewport()->mapFromGlobal(e->globalPos()));
    if (item) tree_->setCurrentItem(item);
    QMenu* menu = createContextMenu(item);
    menu->exec(e->globalPos());
    delete menu;
}

}  // namespace lvd::gui
