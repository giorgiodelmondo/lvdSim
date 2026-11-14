// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/source_pane.hpp"

#include <QHash>
#include <QString>
#include <QWidget>

#include <coverage.hpp>
#include <memory>

class QCheckBox;
class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace lvd::gui {

/// Coverage dock: the instance hierarchy with the coverage of each kind (statement, branch, expression,
/// toggle, FSM state, functional) as coloured bars, a filter and an "uncovered only" switch; below it the
/// details of the selected instance: lines with something not covered, toggles per signal bit and FSM states.
/// Selecting an instance or a detail asks for its source (sourceRequested); lineMarks() gives the gutter
/// annotations of the selected instance for the Source pane.
///
/// The numbers of a hierarchy row include the instances below it (the tool tip also gives the instance
/// alone), as nvc's HTML report does. Kinds without any item in the database have no column.
class CoveragePane : public QWidget {
    Q_OBJECT
public:
    /// Columns of the hierarchy tree.
    enum Column { kInstanceColumn = 0, kUnitColumn = 1, kFirstKindColumn = 2 };
    static constexpr int kTotalColumn = kFirstKindColumn + lvd::tools::cov::kKindCount;
    /// Item data role with the percentage (double, -1 when the kind has no items) of a percentage cell.
    static constexpr int kPercentRole = Qt::UserRole + 1;
    /// Item data role with the scope index of a hierarchy row.
    static constexpr int kScopeRole = Qt::UserRole + 2;

    explicit CoveragePane(QWidget* parent = nullptr);

    /// Shows a database; `origin` says where it came from (file names), for the summary line.
    void setDatabase(std::shared_ptr<const lvd::tools::cov::Database> db, const QString& origin);
    /// No database; the message is shown instead of the summary.
    void clear(const QString& message = {});
    const lvd::tools::cov::Database* database() const noexcept { return db_.get(); }
    QString origin() const { return origin_; }

    void setFilter(const QString& text);
    void setUncoveredOnly(bool on);
    bool uncoveredOnly() const;
    /// Selects the instance with that path (/top/u1 or WORK.TOP.U1); false if there is none or it is filtered
    /// out.
    bool selectInstance(const QString& path);
    /// Scope index of the selected instance, -1 for none.
    int selectedInstance() const noexcept { return selected_; }
    /// Path (/top/u1) of the selected instance, empty for none.
    QString selectedInstancePath() const;
    /// The instance paths of the hierarchy rows that are visible, depth first.
    QStringList visibleInstancePaths() const;

    /// Gutter annotations of the selected instance, by source file as nvc names it.
    QHash<QString, QHash<int, CoverageMark>> lineMarks() const;
    static QHash<int, CoverageMark> marksFor(const lvd::tools::cov::Database& db, int instance, int file);
    /// "statement", "branch", "expression", "toggle", "FSM state", "functional" (plural with n != 1).
    static QString kindName(lvd::tools::cov::Kind k, qint64 n = 1);
    /// "75.6%", "100%", "-" for none.
    static QString percentText(double percent);
    /// Design unit column text of an instance's block name: ENTITY-ARCH -> entity(arch), extended identifiers
    /// as they are.
    static QString unitText(const std::string& blockName);

    QTreeWidget* hierarchy() const noexcept { return tree_; }
    QTreeWidget* details() const noexcept { return details_; }
    QLineEdit* filterEdit() const noexcept { return filter_; }
    QCheckBox* uncoveredCheck() const noexcept { return uncovered_; }
    QLabel* summary() const noexcept { return summary_; }

signals:
    void instanceSelected(int scope);
    /// A source position to show; the file as nvc names it.
    void sourceRequested(const QString& file, int line);

private:
    void rebuildTree();
    void addInstance(QTreeWidgetItem* parent, int scope);
    void applyFilter();
    bool filterItem(QTreeWidgetItem* item, const QString& text, bool uncovered);
    void showDetails(int scope);
    void onCurrentInstance(QTreeWidgetItem* item);
    void onDetail(QTreeWidgetItem* item);
    QString fileName(int file) const;

    std::shared_ptr<const lvd::tools::cov::Database> db_;
    QString origin_;
    int selected_ = -1;
    QLabel* summary_ = nullptr;
    QLineEdit* filter_ = nullptr;
    QCheckBox* uncovered_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    QTreeWidget* details_ = nullptr;
};

} // namespace lvd::gui
