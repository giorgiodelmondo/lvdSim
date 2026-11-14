// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QIcon>
#include <QWidget>

class QAction;
class QToolBar;
class QTreeWidget;
class QTreeWidgetItem;

namespace lvd::gui {

class ProjectSession;

/// Status icons of the Project and Regression docks, drawn with QPainter (no image files).
enum class StatusIcon { Ok, OutOfDate, Failed, Missing, Busy, Pending, TimedOut };
QIcon statusIcon(StatusIcon kind);

/// The Project dock: a tree of the project's libraries with their source files (in analysis order of the
/// project, with a status icon each) and its tests. A tool bar compiles what is out of date, everything or
/// the selection, adds, removes and reorders files and starts the selected test.
class ProjectPane : public QWidget {
    Q_OBJECT
public:
    explicit ProjectPane(ProjectSession* session, QWidget* parent = nullptr);

    QTreeWidget* tree() const noexcept { return tree_; }
    QToolBar* toolBar() const noexcept { return toolBar_; }
    /// Rebuilds the tree from the session (keeps the selection where it can).
    void rebuild();
    /// Updates the status column and icons only.
    void updateStatus();

    /// The selected source file as (library, file), or (-1, -1).
    std::pair<int, int> selectedFile() const;
    int selectedLibrary() const; ///< the library of the selected file or library row, -1 if none
    QString selectedTest() const;
    bool selectFile(int library, int file);
    bool selectTest(const QString& name);

    /// Tool bar actions (by object name: compileOutOfDate, compileAll, compileSelected, addFiles, removeItem,
    /// moveUp, moveDown, simulateTest, regressTests).
    QAction* action(const QString& objectName) const;

    void setDialogsEnabled(bool on) { dialogs_ = on; }

signals:
    void sourceRequested(const QString& file, int line);
    void simulateRequested(const QString& test);
    void regressionRequested(const QStringList& tests);

private:
    void addFilesDialog();
    void removeSelected();
    void moveSelected(int delta);
    void compileSelected();
    void updateActions();

    ProjectSession* session_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    QToolBar* toolBar_ = nullptr;
    bool dialogs_ = true;
};

} // namespace lvd::gui
