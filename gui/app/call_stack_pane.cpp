// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/call_stack_pane.hpp"

#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace lvd::gui {

CallStackPane::CallStackPane(QWidget* parent)
    : QWidget(parent) {
    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(3);
    tree_->setHeaderLabels({tr("#"), tr("Frame"), tr("Location")});
    tree_->setRootIsDecorated(false);
    tree_->setUniformRowHeights(true);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree_->header()->setStretchLastSection(true);
    note_ = new QLabel(this);
    note_->setWordWrap(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->addWidget(tree_, 1);
    layout->addWidget(note_);

    connect(tree_, &QTreeWidget::itemSelectionChanged, this, [this] {
        if (filling_)
            return;
        const int level = selectedLevel();
        if (level >= 0)
            emit frameSelected(level);
    });
    clear(tr("Not stopped. Set a breakpoint in the Source gutter or use Step Into."));
}

void CallStackPane::setFrames(const QString& process, const QVector<SourceFrame>& frames, int selected) {
    filling_ = true;
    frames_ = frames;
    tree_->clear();
    for (const SourceFrame& f : frames) {
        auto* it = new QTreeWidgetItem(tree_);
        it->setText(0, QString::number(f.level));
        QString name = f.name;
        if (f.kind == QLatin1String("function") || f.kind == QLatin1String("procedure"))
            name = QStringLiteral("%1 %2").arg(f.kind, f.name);
        it->setText(1, name);
        it->setToolTip(1, f.symbol);
        it->setText(2, f.file.isEmpty()
                           ? tr("(unknown)")
                           : QStringLiteral("%1:%2").arg(QFileInfo(f.file).fileName()).arg(f.line));
        it->setToolTip(2, f.file);
        it->setData(0, Qt::UserRole, f.level);
        if (f.level == selected)
            tree_->setCurrentItem(it);
    }
    note_->setText(process.isEmpty() ? QString() : tr("Stopped in %1").arg(process));
    tree_->resizeColumnToContents(1);
    filling_ = false;
}

void CallStackPane::clear(const QString& note) {
    filling_ = true;
    frames_.clear();
    tree_->clear();
    note_->setText(note);
    filling_ = false;
}

int CallStackPane::selectedLevel() const {
    const auto items = tree_->selectedItems();
    return items.isEmpty() ? -1 : items.first()->data(0, Qt::UserRole).toInt();
}

void CallStackPane::selectLevel(int level) {
    for (int i = 0; i < tree_->topLevelItemCount(); ++i)
        if (tree_->topLevelItem(i)->data(0, Qt::UserRole).toInt() == level)
            tree_->setCurrentItem(tree_->topLevelItem(i));
}

} // namespace lvd::gui
