// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sim/parsers/json_replies.hpp"

#include <QHash>
#include <QSet>
#include <QStringList>
#include <QWidget>

#include <functional>

class QLabel;
class QMenu;
class QTreeWidget;
class QTreeWidgetItem;

namespace lvd::gui {

/// Locals dock: the variables of the selected call stack frame (`locals -frame n -json`) with name, type and
/// value. A value in aggregate syntax (a record or an array) can be expanded into its elements. Values that
/// differ from the previous stop in the same frame (same process and subprogram) are highlighted.
class LocalsPane : public QWidget {
    Q_OBJECT
public:
    explicit LocalsPane(QWidget* parent = nullptr);

    /// Shows the variables. `scope` names the frame (process and subprogram) so that values are compared with
    /// those of the same frame; `stopSerial` counts stops, so showing a frame again at the same stop keeps
    /// the highlight. `emptyNote` is shown when there are no variables.
    void setLocals(const QString& scope, const LocalsReply& reply, int stopSerial,
                   const QString& emptyNote = {});
    void clear(const QString& note = {});
    /// Forget the values of earlier stops (a new simulation).
    void forgetHistory();

    int variableCount() const noexcept { return static_cast<int>(variables_.size()); }
    /// Value shown for a variable or an element: "total", "both.a", "ints#2" (2nd positional element).
    QString valueOf(const QString& path) const;
    /// Paths of the highlighted (changed) values.
    QStringList changedPaths() const;
    /// The item of a path, nullptr if none.
    QTreeWidgetItem* itemOf(const QString& path) const;
    QTreeWidget* tree() const noexcept { return tree_; }
    QString note() const;

    /// View Declaration in the context menu (object name viewDeclaration): the gate returns why the
    /// declaration of a variable cannot be shown, empty when it can.
    void setDeclarationGate(std::function<QString(const QString& variable)> gate) { gate_ = std::move(gate); }
    /// The context menu for an item (may be null); caller owns it.
    QMenu* createContextMenu(QTreeWidgetItem* item);
    /// The variable an item belongs to (its top-level name), empty for none.
    static QString variableOf(const QTreeWidgetItem* item);

signals:
    /// Show where the variable (name as listed) of the selected frame is declared.
    void viewDeclarationRequested(const QString& variable);

protected:
    void contextMenuEvent(QContextMenuEvent*) override;

private:
    void addValue(QTreeWidgetItem* item, const QString& path, const QString& value, int depth);

    QTreeWidget* tree_ = nullptr;
    QLabel* note_ = nullptr;
    QVector<VariableInfo> variables_;
    QHash<QString, QHash<QString, QString>> lastSeen_; ///< scope -> path -> value at the last stop seen
    QHash<QString, QHash<QString, QString>> baseline_; ///< scope -> values the current stop is compared with
    QHash<QString, int> seenAt_;                       ///< scope -> stop serial of lastSeen_
    QHash<QString, QString> shown_;                    ///< path -> value now on view
    const QHash<QString, QString>* compare_ = nullptr; ///< baseline while filling
    QHash<QString, QTreeWidgetItem*> items_;
    QSet<QString> changed_;
    QSet<QString> expanded_;
    std::function<QString(const QString&)> gate_;
};

} // namespace lvd::gui
