// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/problems_pane.hpp"

#include "app/project_pane.hpp"

#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace lvd::gui {

namespace diag = lvd::tools::diag;

ProblemsPane::ProblemsPane(QWidget* parent)
    : QWidget(parent) {
    summary_ = new QLabel(this);
    summary_->setObjectName("problemsSummary");
    summary_->setContentsMargins(4, 2, 4, 2);
    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(4);
    tree_->setHeaderLabels({tr("Severity"), tr("Message"), tr("File"), tr("Line")});
    tree_->setRootIsDecorated(false);
    tree_->setUniformRowHeights(true);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    tree_->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    connect(tree_, &QTreeWidget::itemActivated, this,
            [this](QTreeWidgetItem* it, int) { activateRow(tree_->indexOfTopLevelItem(it)); });
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(summary_);
    layout->addWidget(tree_);
    setProblems({});
}

void ProblemsPane::setProblems(const QVector<Problem>& problems) {
    problems_ = problems;
    tree_->clear();
    errors_ = 0;
    int warnings = 0;
    for (const Problem& p : problems) {
        const bool error = p.severity >= diag::Severity::Error;
        errors_ += error ? 1 : 0;
        warnings += error ? 0 : 1;
        auto* it = new QTreeWidgetItem(tree_);
        it->setIcon(0, statusIcon(error ? StatusIcon::Failed : StatusIcon::OutOfDate));
        it->setText(0, QString::fromLatin1(diag::severityName(p.severity)));
        it->setText(1, p.message);
        it->setText(2, p.file.isEmpty() ? p.source : QFileInfo(p.file).fileName());
        it->setText(3, p.line > 0 ? QString::number(p.line) : QString());
        it->setToolTip(1, p.detail.isEmpty() ? p.message : p.message + "\n" + p.detail);
        it->setToolTip(2, p.file);
    }
    summary_->setText(problems.isEmpty()
                          ? tr("No problems in the last analysis.")
                          : tr("%n error(s)", "", errors_) + ", " + tr("%n warning(s)", "", warnings) +
                                tr(" - double-click a row to open the source line"));
}

int ProblemsPane::count() const {
    return problems_.size();
}

void ProblemsPane::activateRow(int row) {
    if (row < 0 || row >= problems_.size() || problems_[row].file.isEmpty())
        return;
    emit sourceRequested(problems_[row].file, problems_[row].line);
}

} // namespace lvd::gui
