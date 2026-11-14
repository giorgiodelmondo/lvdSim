// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

class QLineEdit;
class QListWidget;

namespace lvd::gui {

/// The commands typed in the console, oldest first. Blank commands are not kept; a command typed again moves
/// to the end (no duplicates); the oldest are dropped beyond the limit. Kept per user in the GUI settings
/// (kSettingsKey) by SimWindow.
class ConsoleHistory {
public:
    static constexpr int kDefaultLimit = 1000;
    static constexpr const char* kSettingsKey = "console/history";

    explicit ConsoleHistory(int limit = kDefaultLimit);

    /// Adds a command; false if it was blank.
    bool add(const QString& command);
    void setEntries(const QStringList& entries);
    const QStringList& entries() const noexcept { return entries_; }
    qsizetype size() const noexcept { return entries_.size(); }
    QString at(qsizetype i) const { return entries_.value(i); }
    void clear() { entries_.clear(); }

    void setLimit(int limit);
    int limit() const noexcept { return limit_; }

    /// Index of the newest entry before `before` (exclusive) that contains `text` (case-insensitive), or -1.
    /// An empty text matches every entry.
    qsizetype searchBackward(const QString& text, qsizetype before) const;

private:
    void trim();

    QStringList entries_;
    int limit_;
};

/// Transcript > Command history: the history, newest first, with a filter. Insert puts the chosen command
/// into the command line, Run sends it (a double-click inserts it).
class ConsoleHistoryDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConsoleHistoryDialog(QWidget* parent = nullptr);
    void setEntries(const QStringList& oldestFirst);
    QLineEdit* filterEdit() const noexcept { return filter_; }
    QListWidget* list() const noexcept { return list_; }
    /// The commands shown, top to bottom.
    QStringList shown() const;

signals:
    void insertRequested(const QString& command);
    void runRequested(const QString& command);
    void clearRequested();

private:
    void refill();
    QString current() const;

    QStringList entries_;
    QLineEdit* filter_ = nullptr;
    QListWidget* list_ = nullptr;
};

} // namespace lvd::gui
