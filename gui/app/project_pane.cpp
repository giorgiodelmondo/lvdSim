// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/project_pane.hpp"

#include "app/project_session.hpp"

#include <QAction>
#include <QFileDialog>
#include <QHeaderView>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidgetAction>

namespace lvd::gui {

namespace build = lvd::tools::build;

namespace {

enum Kind { kLibrary = 1, kFile, kTestsRoot, kTest };
constexpr int kKindRole = Qt::UserRole;
constexpr int kLibRole = Qt::UserRole + 1;
constexpr int kFileRole = Qt::UserRole + 2;
constexpr int kNameRole = Qt::UserRole + 3;

QPixmap drawStatus(StatusIcon kind, int size) {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal s = size;
    const QRectF r(s * 0.1, s * 0.1, s * 0.8, s * 0.8);
    auto disc = [&](const QColor& c) {
        p.setPen(QPen(c.darker(130), s / 16));
        p.setBrush(c);
        p.drawEllipse(r);
    };
    QPen mark(Qt::white, s / 8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    switch (kind) {
    case StatusIcon::Ok: {
        disc(QColor(0x2e, 0x9d, 0x48));
        p.setPen(mark);
        QPainterPath path;
        path.moveTo(s * 0.3, s * 0.52);
        path.lineTo(s * 0.45, s * 0.67);
        path.lineTo(s * 0.72, s * 0.36);
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        break;
    }
    case StatusIcon::OutOfDate:
        disc(QColor(0xe0, 0xa0, 0x1c));
        p.setPen(mark);
        p.drawLine(QPointF(s * 0.5, s * 0.28), QPointF(s * 0.5, s * 0.52));
        p.drawLine(QPointF(s * 0.5, s * 0.52), QPointF(s * 0.66, s * 0.62));
        break;
    case StatusIcon::Failed:
    case StatusIcon::TimedOut:
        disc(kind == StatusIcon::Failed ? QColor(0xd0, 0x33, 0x2b) : QColor(0x9b, 0x3c, 0xb0));
        p.setPen(mark);
        if (kind == StatusIcon::Failed) {
            p.drawLine(QPointF(s * 0.34, s * 0.34), QPointF(s * 0.66, s * 0.66));
            p.drawLine(QPointF(s * 0.66, s * 0.34), QPointF(s * 0.34, s * 0.66));
        } else {
            p.drawLine(QPointF(s * 0.5, s * 0.3), QPointF(s * 0.5, s * 0.5));
            p.drawLine(QPointF(s * 0.5, s * 0.5), QPointF(s * 0.35, s * 0.62));
        }
        break;
    case StatusIcon::Missing:
        p.setPen(QPen(QColor(0x90, 0x90, 0x90), s / 10, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(r);
        break;
    case StatusIcon::Busy:
        disc(QColor(0x2f, 0x6f, 0xd6));
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        for (int i = 0; i < 3; ++i)
            p.drawEllipse(QPointF(s * (0.3 + 0.2 * i), s * 0.5), s * 0.06, s * 0.06);
        break;
    case StatusIcon::Pending:
        p.setPen(QPen(QColor(0x70, 0x70, 0x70), s / 12));
        p.setBrush(QColor(0xe8, 0xe8, 0xe8));
        p.drawEllipse(r);
        break;
    }
    return pm;
}

} // namespace

QIcon statusIcon(StatusIcon kind) {
    QIcon icon;
    for (const int size : {16, 20, 24, 32})
        icon.addPixmap(drawStatus(kind, size));
    return icon;
}

ProjectPane::ProjectPane(ProjectSession* session, QWidget* parent)
    : QWidget(parent)
    , session_(session) {
    toolBar_ = new QToolBar(this);
    toolBar_->setIconSize(QSize(16, 16));
    toolBar_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto add = [this](const char* name, const QString& text, const QString& tip, auto fn) {
        QAction* a = toolBar_->addAction(text);
        a->setObjectName(name);
        a->setToolTip(tip);
        connect(a, &QAction::triggered, this, fn);
        return a;
    };
    add("compileOutOfDate", tr("Compile"),
        tr("Analyse the files that changed and the files that depend on them"),
        [this] { session_->compile(build::Mode::OutOfDate); });
    add("compileAll", tr("All"), tr("Analyse every file of the project"),
        [this] { session_->compile(build::Mode::All); });
    add("compileSelected", tr("Selected"), tr("Analyse the selected file and the files that depend on it"),
        [this] { compileSelected(); });
    toolBar_->addSeparator();
    add("addFiles", tr("Add..."), tr("Add source files to the selected library"),
        [this] { addFilesDialog(); });
    add("removeItem", tr("Remove"), tr("Remove the selected file or test from the project"),
        [this] { removeSelected(); });
    add("moveUp", tr("Up"), tr("Move the file up in the compile order"), [this] { moveSelected(-1); });
    add("moveDown", tr("Down"), tr("Move the file down in the compile order"), [this] { moveSelected(1); });
    toolBar_->addSeparator();
    add("simulateTest", tr("Simulate"), tr("Start the selected test in the simulator"), [this] {
        if (!selectedTest().isEmpty())
            emit simulateRequested(selectedTest());
    });
    add("regressTests", tr("Regress"), tr("Run the selected test, or all tests, as a batch regression"),
        [this] {
            const QString t = selectedTest();
            emit regressionRequested(t.isEmpty() ? QStringList() : QStringList{t});
        });

    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(2);
    tree_->setHeaderLabels({tr("Library / file / test"), tr("Status")});
    tree_->setUniformRowHeights(true);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    // The same actions as the tool bar in a context menu (the tool bar may be cut off in a narrow dock).
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        if (QTreeWidgetItem* it = tree_->itemAt(pos))
            tree_->setCurrentItem(it);
        QMenu menu(this);
        for (QAction* a : toolBar_->actions())
            if (a->isSeparator())
                menu.addSeparator();
            else if (qobject_cast<QWidgetAction*>(a) == nullptr)
                menu.addAction(a);
        menu.exec(tree_->viewport()->mapToGlobal(pos));
    });
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, [this] { updateActions(); });
    connect(tree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* it, int) {
        const int kind = it->data(0, kKindRole).toInt();
        if (kind == kFile) {
            const auto& lib = session_->project().libraries.at(it->data(0, kLibRole).toUInt());
            emit sourceRequested(session_->absolute(QString::fromStdString(
                                     lib.files.at(it->data(0, kFileRole).toUInt()).path)),
                                 0);
        } else if (kind == kTest) {
            emit simulateRequested(it->data(0, kNameRole).toString());
        }
    });

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(toolBar_);
    layout->addWidget(tree_);

    connect(session_, &ProjectSession::projectChanged, this, [this] { rebuild(); });
    connect(session_, &ProjectSession::statusChanged, this, [this] { updateStatus(); });
    connect(session_, &ProjectSession::compileProgress, this, [this](int) { updateStatus(); });
    connect(session_, &ProjectSession::compileStarted, this, [this](int) { updateActions(); });
    connect(session_, &ProjectSession::compileFinished, this, [this](int) { updateActions(); });
    rebuild();
}

QAction* ProjectPane::action(const QString& objectName) const {
    for (QAction* a : toolBar_->actions())
        if (a->objectName() == objectName)
            return a;
    return nullptr;
}

void ProjectPane::rebuild() {
    const auto sel = selectedFile();
    const QString selTest = selectedTest();
    tree_->clear();
    if (!session_->isOpen()) {
        updateActions();
        return;
    }
    const auto& p = session_->project();
    for (std::size_t l = 0; l < p.libraries.size(); ++l) {
        const auto& lib = p.libraries[l];
        auto* li = new QTreeWidgetItem(tree_);
        li->setText(0, QString::fromStdString(lib.name));
        li->setToolTip(0, session_->absolute(QString::fromStdString(p.libraryPath(lib))));
        li->setData(0, kKindRole, kLibrary);
        li->setData(0, kLibRole, static_cast<int>(l));
        QFont f = li->font(0);
        f.setBold(true);
        li->setFont(0, f);
        for (std::size_t k = 0; k < lib.files.size(); ++k) {
            const auto& sf = lib.files[k];
            auto* fi = new QTreeWidgetItem(li);
            fi->setText(0, QString::fromStdString(sf.path));
            fi->setData(0, kKindRole, kFile);
            fi->setData(0, kLibRole, static_cast<int>(l));
            fi->setData(0, kFileRole, static_cast<int>(k));
            QStringList opts;
            if (sf.isVerilog())
                opts << (sf.effectiveLanguage() == lvd::tools::proj::Language::SystemVerilog
                             ? tr("SystemVerilog")
                             : tr("Verilog"));
            else
                opts << tr("VHDL-%1").arg(QString::fromStdString(p.vhdlOf(sf)));
            fi->setToolTip(0, session_->absolute(QString::fromStdString(sf.path)) + "\n" + opts.join(", "));
        }
    }
    auto* tests = new QTreeWidgetItem(tree_);
    tests->setText(0, tr("Tests"));
    tests->setData(0, kKindRole, kTestsRoot);
    QFont f = tests->font(0);
    f.setBold(true);
    tests->setFont(0, f);
    for (const auto& t : p.tests) {
        auto* ti = new QTreeWidgetItem(tests);
        ti->setText(0, QString::fromStdString(t.name));
        ti->setText(1, QString::fromStdString(t.top));
        ti->setData(0, kKindRole, kTest);
        ti->setData(0, kNameRole, QString::fromStdString(t.name));
        QStringList tip;
        for (const auto& g : t.generics)
            tip << "-g" + QString::fromStdString(g);
        if (!t.seeds.empty())
            tip << tr("%n seed(s)", "", static_cast<int>(t.seeds.size()));
        ti->setToolTip(0, tip.join(' '));
    }
    tree_->expandAll();
    updateStatus();
    if (sel.first >= 0)
        selectFile(sel.first, sel.second);
    else if (!selTest.isEmpty())
        selectTest(selTest);
    updateActions();
}

void ProjectPane::updateStatus() {
    if (!session_->isOpen())
        return;
    const auto& st = session_->statuses();
    for (int l = 0; l < tree_->topLevelItemCount(); ++l) {
        QTreeWidgetItem* li = tree_->topLevelItem(l);
        if (li->data(0, kKindRole).toInt() != kLibrary)
            continue;
        int stale = 0;
        for (int k = 0; k < li->childCount(); ++k) {
            QTreeWidgetItem* fi = li->child(k);
            const int idx =
                session_->fileIndex(fi->data(0, kLibRole).toInt(), fi->data(0, kFileRole).toInt());
            if (idx < 0 || idx >= static_cast<int>(st.size()))
                continue;
            const build::FileStatus& s = st[static_cast<std::size_t>(idx)];
            StatusIcon icon = StatusIcon::Ok;
            QString text = tr("up to date");
            if (idx == session_->compilingFile()) {
                icon = StatusIcon::Busy;
                text = tr("analysing");
            } else if (s.state == build::FileState::Failed) {
                icon = StatusIcon::Failed;
                text = tr("failed");
            } else if (s.state == build::FileState::Missing) {
                icon = StatusIcon::Missing;
                text = tr("file missing");
            } else if (s.state == build::FileState::OutOfDate) {
                icon = StatusIcon::OutOfDate;
                text = QString::fromLatin1(build::reasonText(s.reason));
                if (s.reason == build::Reason::Dependency && s.cause >= 0) {
                    const auto files = build::projectFiles(session_->project());
                    const auto c = files[static_cast<std::size_t>(s.cause)];
                    text += " " + QString::fromStdString(session_->project()
                                                             .libraries[static_cast<std::size_t>(c.library)]
                                                             .files[static_cast<std::size_t>(c.file)]
                                                             .path)
                                      .section('/', -1);
                }
            }
            stale += s.state == build::FileState::UpToDate ? 0 : 1;
            fi->setIcon(0, statusIcon(icon));
            fi->setText(1, text);
        }
        li->setText(1, stale == 0 ? tr("%n file(s)", "", li->childCount())
                                  : tr("%1 of %2 to compile").arg(stale).arg(li->childCount()));
    }
}

std::pair<int, int> ProjectPane::selectedFile() const {
    const QList<QTreeWidgetItem*> sel = tree_->selectedItems();
    if (sel.isEmpty() || sel.front()->data(0, kKindRole).toInt() != kFile)
        return {-1, -1};
    return {sel.front()->data(0, kLibRole).toInt(), sel.front()->data(0, kFileRole).toInt()};
}

int ProjectPane::selectedLibrary() const {
    const QList<QTreeWidgetItem*> sel = tree_->selectedItems();
    if (sel.isEmpty())
        return -1;
    const int kind = sel.front()->data(0, kKindRole).toInt();
    return (kind == kFile || kind == kLibrary) ? sel.front()->data(0, kLibRole).toInt() : -1;
}

QString ProjectPane::selectedTest() const {
    const QList<QTreeWidgetItem*> sel = tree_->selectedItems();
    if (sel.isEmpty() || sel.front()->data(0, kKindRole).toInt() != kTest)
        return {};
    return sel.front()->data(0, kNameRole).toString();
}

bool ProjectPane::selectFile(int library, int file) {
    for (QTreeWidgetItemIterator it(tree_); *it; ++it)
        if ((*it)->data(0, kKindRole).toInt() == kFile && (*it)->data(0, kLibRole).toInt() == library &&
            (*it)->data(0, kFileRole).toInt() == file) {
            tree_->setCurrentItem(*it);
            return true;
        }
    return false;
}

bool ProjectPane::selectTest(const QString& name) {
    for (QTreeWidgetItemIterator it(tree_); *it; ++it)
        if ((*it)->data(0, kKindRole).toInt() == kTest && (*it)->data(0, kNameRole).toString() == name) {
            tree_->setCurrentItem(*it);
            return true;
        }
    return false;
}

void ProjectPane::updateActions() {
    const bool open = session_->isOpen();
    const bool busy = session_->compiling();
    const auto file = selectedFile();
    const bool hasFile = file.first >= 0;
    const bool hasTest = !selectedTest().isEmpty();
    auto set = [this](const char* name, bool on) {
        if (QAction* a = action(name))
            a->setEnabled(on);
    };
    set("compileOutOfDate", open && !busy);
    set("compileAll", open && !busy);
    set("compileSelected", open && !busy && hasFile);
    set("addFiles", open && selectedLibrary() >= 0);
    set("removeItem", open && (hasFile || hasTest));
    const int count =
        hasFile ? static_cast<int>(
                      session_->project().libraries[static_cast<std::size_t>(file.first)].files.size())
                : 0;
    set("moveUp", hasFile && file.second > 0);
    set("moveDown", hasFile && file.second + 1 < count);
    set("simulateTest", hasTest);
    set("regressTests", open && !session_->project().tests.empty() && !session_->regressionRunning());
}

void ProjectPane::addFilesDialog() {
    const int lib = selectedLibrary();
    if (lib < 0 || !dialogs_)
        return;
    const QStringList files =
        QFileDialog::getOpenFileNames(this, tr("Add source files"), session_->dir(),
                                      tr("HDL sources (*.vhd *.vhdl *.v *.sv *.vh *.svh);;All files (*)"));
    session_->addFiles(lib, files);
}

void ProjectPane::removeSelected() {
    const auto f = selectedFile();
    if (f.first >= 0) {
        session_->removeFile(f.first, f.second);
        return;
    }
    const QString t = selectedTest();
    const auto& tests = session_->project().tests;
    for (std::size_t i = 0; i < tests.size(); ++i)
        if (QString::fromStdString(tests[i].name) == t) {
            session_->removeTest(static_cast<int>(i));
            return;
        }
}

void ProjectPane::moveSelected(int delta) {
    const auto f = selectedFile();
    if (f.first >= 0 && session_->moveFile(f.first, f.second, delta))
        selectFile(f.first, f.second + delta);
}

void ProjectPane::compileSelected() {
    const auto f = selectedFile();
    const int idx = session_->fileIndex(f.first, f.second);
    if (idx >= 0)
        session_->compile(build::Mode::Selected, {idx});
}

} // namespace lvd::gui
