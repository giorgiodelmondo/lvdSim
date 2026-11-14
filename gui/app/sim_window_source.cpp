// SPDX-License-Identifier: GPL-3.0-or-later
// Source navigation of the main window: View Declaration (Objects, Wave, List, Locals), View Definition and
// View Instantiation (Sim), View Source (Processes, Library), and where the files named by the simulator are.
#include "app/library_pane.hpp"
#include "app/locals_pane.hpp"
#include "app/objects_pane.hpp"
#include "app/project_session.hpp"
#include "app/sim_window.hpp"
#include "app/source_pane.hpp"
#include "app/structure_pane.hpp"
#include "app/transcript_widget.hpp"
#include "app/unit_source.hpp"
#include "wave/list_window.hpp"
#include "wave/signal_mime.hpp"
#include "wave/wave_widget.hpp"

#include <lvdwave/wave_space.hpp>

#include <QDir>
#include <QDockWidget>
#include <QFileInfo>

namespace lvd::gui {

namespace {

QStringList qtList(const std::vector<std::string>& v) {
    QStringList l;
    for (const std::string& s : v)
        l << QString::fromStdString(s);
    return l;
}

/// The name to select at the start line of a region's definition: the architecture of "lib.ent(arch)", the
/// module of "lib.mod", or the label of a block or generate statement.
QString unitWord(const QString& unit) {
    const qsizetype open = unit.indexOf(QLatin1Char('('));
    if (open >= 0 && unit.endsWith(QLatin1Char(')')))
        return unit.mid(open + 1, unit.size() - open - 2);
    return unit.mid(unit.lastIndexOf(QLatin1Char('.')) + 1);
}

/// "g(1)" -> "g": the label of a generate iteration as written in the source.
QString labelWord(const QString& name) {
    const qsizetype open = name.indexOf(QLatin1Char('('));
    return open > 0 ? name.left(open) : name;
}

} // namespace

QString SimWindow::declarationBlocker() const {
    return locator_.unavailableReason(session_.running());
}

void SimWindow::probeDeclarations() {
    if (session_.running())
        locator_.probe();
}

void SimWindow::connectSourceNavigation() {
    locator_.setRequest([this](const QString& command, std::function<void(const QString&)> ok,
                               std::function<void(const QString&)> failed) {
        if (!session_.running()) {
            failed(tr("no simulation is running"));
            return;
        }
        // Sent like the stack and locals queries: one line, answered at once, typed commands are not held up.
        const auto onFailure = failed;
        if (request(command, std::move(ok), std::move(failed)) < 0)
            onFailure(tr("the simulator did not take the command"));
    });

    objects_->setDeclarationGate([this] { return declarationBlocker(); });
    structure_->setDefinitionGate([this] { return declarationBlocker(); });
    const auto pathGate = [this](const QString& path) -> QString {
        // "ref:tb.q" is a signal of a dataset ("tb.q[3:0]" has a colon too, in its range)
        if (!lvd::wave::WaveSpace::split(path.toStdString()).dataset.empty())
            return tr("Only signals of the running simulation have a declaration to show");
        return declarationBlocker();
    };
    wave_->setDeclarationGate(pathGate);
    list_->setDeclarationGate(pathGate);
    locals_->setDeclarationGate([this](const QString&) -> QString {
        if (!stoppedAtSource_)
            return tr("Not stopped at a source line");
        if (frames_.value(selectedFrame_).kind != QLatin1String("process"))
            return tr("Only the variables of a process can be looked up, not those of a subprogram");
        return declarationBlocker();
    });
    library_->setSourceGate(
        [this](const QString& unit, const QString& arch) { return librarySourceBlocker(unit, arch); });

    connect(objects_, &ObjectsPane::viewDeclarationRequested, this,
            [this](const QString& path) { viewDeclaration(shellPathOfStoreSignal(path)); });
    connect(objects_, &ObjectsPane::addToList, this, [this](const QStringList& paths) {
        QStringList addPaths;
        for (const QString& p : paths)
            addPaths << addWavePathFromStorePath(p);
        list_->addPaths(addPaths);
    });
    connect(wave_, &WaveWidget::declarationRequested, this,
            [this](const QString& path) { viewDeclaration(shellPathOfStoreSignal(path)); });
    connect(list_, &ListWindow::declarationRequested, this,
            [this](const QString& path) { viewDeclaration(shellPathOfStoreSignal(path)); });
    connect(structure_, &StructurePane::viewDefinitionRequested, this,
            [this](const QString& path) { viewRegion(shellPathOfStoreScope(path), false); });
    connect(structure_, &StructurePane::viewInstantiationRequested, this,
            [this](const QString& path) { viewRegion(shellPathOfStoreScope(path), true); });
    connect(locals_, &LocalsPane::viewDeclarationRequested, this, [this](const QString& variable) {
        const SourceFrame f = frames_.value(selectedFrame_);
        viewDeclaration(f.name + QLatin1Char('/') + variable.toLower());
    });
    connect(library_, &LibraryPane::viewSourceRequested, this,
            [this](const QString& unit, const QString& arch) { viewLibraryUnit(unit, arch); });
}

void SimWindow::viewDeclaration(const QString& shellPath) {
    const QString why = declarationBlocker();
    if (!why.isEmpty()) {
        transcript_->appendNote(tr("** View Declaration: %1").arg(why));
        return;
    }
    if (shellPath.isEmpty()) {
        transcript_->appendNote(tr("** View Declaration: this object has no name in the simulator"));
        return;
    }
    locator_.describe(shellPath, [this, shellPath](const lvd::Result<DescribedObject>& r) {
        if (!r.ok()) {
            transcript_->appendNote(
                tr("** View Declaration: %1: %2").arg(shellPath, QString::fromStdString(r.error())));
            return;
        }
        showDeclaration(shellPath, r.value(), false, false);
    });
}

void SimWindow::viewRegion(const QString& shellPath, bool instantiation) {
    const QString what = instantiation ? tr("View Instantiation") : tr("View Definition");
    const QString why = declarationBlocker();
    if (!why.isEmpty()) {
        transcript_->appendNote(tr("** %1: %2").arg(what, why));
        return;
    }
    if (shellPath.isEmpty()) {
        transcript_->appendNote(tr("** %1: this scope has no name in the simulator").arg(what));
        return;
    }
    locator_.describe(
        shellPath, [this, shellPath, what, instantiation](const lvd::Result<DescribedObject>& r) {
            if (!r.ok()) {
                transcript_->appendNote(
                    tr("** %1: %2: %3").arg(what, shellPath, QString::fromStdString(r.error())));
                return;
            }
            showDeclaration(shellPath, r.value(), true, instantiation);
        });
}

void SimWindow::showDeclaration(const QString& shellPath, const DescribedObject& o, bool region,
                                bool instantiation) {
    const QString what = instantiation ? tr("View Instantiation")
                         : region      ? tr("View Definition")
                                       : tr("View Declaration");
    if (!o.hasLocation()) {
        transcript_->appendNote(
            tr("** %1: %2: the simulator does not report source locations (describe -location needs a newer "
               "nvc kernel of the lvdsim fork)")
                .arg(what, shellPath));
        return;
    }
    SourceLocation loc;
    if (instantiation) {
        if (o.instFile.isEmpty() || o.instLine <= 0) {
            transcript_->appendNote(
                tr("** %1: %2 is the top level of the design: nothing instantiates it").arg(what, shellPath));
            return;
        }
        loc = {o.instFile, o.instLine, 0, labelWord(o.name)};
    } else if (!o.unitFile.isEmpty() && o.unitLine > 0) {
        loc = {o.unitFile, o.unitLine, 0, unitWord(o.unit)};
    } else if (!o.file.isEmpty() && o.line > 0) {
        loc = {o.file, o.line, o.column, o.name};
    } else {
        transcript_->appendNote(
            tr("** %1: the simulator does not know where %2 is declared").arg(what, shellPath));
        return;
    }
    jumpToSource(loc);
}

QString SimWindow::declarationSourceFile(const QString& name, QStringList* searched) const {
    if (name.isEmpty())
        return {};
    const QFileInfo fi(name);
    if (fi.isAbsolute()) {
        if (searched != nullptr)
            *searched = QStringList{QDir::toNativeSeparators(fi.absolutePath())};
        return fi.exists() ? QDir::cleanPath(fi.absoluteFilePath()) : QString();
    }
    // nvc keeps the name a file was analysed with, relative to the folder of the analysis: usually where the
    // simulation started, the project folder, or next to the libraries.
    QStringList dirs;
    const auto add = [&dirs](const QString& d) {
        if (d.isEmpty())
            return;
        const QString c = QDir::cleanPath(QDir(d).absolutePath());
        if (!dirs.contains(c))
            dirs << c;
    };
    const QDir base(sessionDir_.isEmpty() ? QDir::currentPath() : sessionDir_);
    add(base.absolutePath());
    if (project_ != nullptr && project_->isOpen())
        add(project_->dir());
    for (const QString& a : sessionArgs_) {
        if (!a.startsWith(QLatin1String("--work=")) && !a.startsWith(QLatin1String("--map=")))
            continue;
        const QString spec = a.mid(a.indexOf(QLatin1Char('=')) + 1);
        const qsizetype colon = spec.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        const QString lib = base.absoluteFilePath(spec.mid(colon + 1));
        add(QFileInfo(lib).absolutePath());
        add(lib);
    }
    for (const lvd::tools::LibraryInfo& l : libraryInfos_)
        add(QFileInfo(base.absoluteFilePath(QString::fromStdString(l.path))).absolutePath());
    for (const QString& f : libraryFolders_) {
        add(f);
        add(QFileInfo(f).absolutePath());
    }
    for (const QString& d : coverageDirs_)
        add(d);
    add(QDir::currentPath());
    if (searched != nullptr) {
        searched->clear();
        for (const QString& d : dirs)
            *searched << QDir::toNativeSeparators(d);
    }
    for (const QString& d : dirs) {
        const QString candidate = QDir(d).absoluteFilePath(name);
        if (QFileInfo::exists(candidate))
            return QDir::cleanPath(candidate);
    }
    const QString known = resolveSourceFile(name); // a file the breakpoints or processes know by that tail
    return QFileInfo::exists(known) ? known : QString();
}

bool SimWindow::jumpToSource(const SourceLocation& location) {
    QStringList searched;
    const QString file = declarationSourceFile(location.file, &searched);
    if (file.isEmpty()) {
        transcript_->appendNote(tr("** Source file %1 not found (looked in %2)")
                                    .arg(location.file, searched.join(QStringLiteral(", "))));
        return false;
    }
    SourceLocation at = location;
    at.file = file;
    if (!source_->openAt(at)) {
        transcript_->appendNote("** " + source_->lastError());
        return false;
    }
    if (auto* dock = qobject_cast<QDockWidget*>(source_->parentWidget())) {
        dock->show();
        dock->raise();
    }
    source_->view()->setFocus();
    return true;
}

QString SimWindow::librarySourceBlocker(const QString& unit, const QString& architecture) const {
    if (unit.isEmpty())
        return tr("Select a design unit");
    if (project_ != nullptr && project_->isOpen() &&
        findUnitInProject(
            project_->project(), [this](const QString& p) { return project_->absolute(p); },
            unit.section(QLatin1Char('.'), 0, 0), unit.section(QLatin1Char('.'), 1), architecture))
        return {};
    if (env_.nvc.empty())
        return tr("The source of this unit is not known: it is not in the open project, and there is no nvc "
                  "to ask");
    return {}; // nvc is asked when it is used (it knows VHDL units)
}

bool SimWindow::waitForLibraryTool(int timeoutMs) {
    return libraryTool_ == nullptr || libraryTool_->waitForIdle(timeoutMs);
}

void SimWindow::viewLibraryUnit(const QString& unit, const QString& architecture) {
    const QString lib = unit.section(QLatin1Char('.'), 0, 0);
    const QString name = unit.section(QLatin1Char('.'), 1);
    if (lib.isEmpty() || name.isEmpty())
        return;
    if (project_ != nullptr && project_->isOpen()) {
        const auto loc = findUnitInProject(
            project_->project(), [this](const QString& p) { return project_->absolute(p); }, lib, name,
            architecture);
        if (loc) {
            jumpToSource(*loc);
            return;
        }
    }
    QString libPath;
    for (const lvd::tools::LibraryInfo& l : libraryInfos_)
        if (QString::fromStdString(l.name).compare(lib, Qt::CaseInsensitive) == 0)
            libPath = QFileInfo(QString::fromStdString(l.path)).absoluteFilePath();
    if (libPath.isEmpty() || env_.nvc.empty()) {
        transcript_->appendNote(
            tr("** View Source: the source of %1 is not known (it is not in the open project)").arg(unit));
        return;
    }
    if (libraryTool_ == nullptr) {
        libraryTool_ = new NvcTool(this);
        NvcTool::Setup s;
        s.nvc = QString::fromStdString(env_.nvc);
        s.globalArgs = qtList(env_.ctx.nvcGlobalArgs);
        if (!env_.nvcDllDir.empty())
            s.pathPrepend << QString::fromStdString(env_.nvcDllDir);
        libraryTool_->setSetup(s);
    }
    if (libraryTool_->busy()) {
        transcript_->appendNote(tr("** View Source: still looking up the previous unit"));
        return;
    }
    QStringList args;
    QStringList parents;
    for (const lvd::tools::LibraryInfo& l : libraryInfos_) {
        const QString parent =
            QFileInfo(QFileInfo(QString::fromStdString(l.path)).absoluteFilePath()).absolutePath();
        if (!parents.contains(parent))
            parents << parent;
    }
    for (const QString& p : parents)
        args << QStringLiteral("-L") << p;
    args << QStringLiteral("--work=%1:%2").arg(lib, libPath) << QStringLiteral("--print-deps") << name;
    const QString libParent = QFileInfo(libPath).absolutePath();
    const bool started = libraryTool_->start(
        args, [this, unit, lib, name, architecture, libParent](int code, const QString& out) {
            QString src = sourceFromPrintDeps(out, lib, name, architecture);
            if (src.isEmpty() && !architecture.isEmpty())
                src = sourceFromPrintDeps(out, lib, name, {});
            if (src.isEmpty()) {
                const QString first = out.trimmed().section(QLatin1Char('\n'), 0, 0);
                transcript_->appendNote(tr("** View Source: nvc does not name the source file of %1 (%2). "
                                           "Verilog modules are only found "
                                           "in an open project.")
                                            .arg(unit, code == 0 ? tr("no dependency line") : first));
                return;
            }
            QString file = src;
            if (QFileInfo(src).isRelative()) {
                for (const QString& d : {libParent, sessionDir_, QDir::currentPath()}) {
                    if (d.isEmpty())
                        continue;
                    const QString c = QDir(d).absoluteFilePath(src);
                    if (QFileInfo::exists(c)) {
                        file = QDir::cleanPath(c);
                        break;
                    }
                }
            }
            const int line = unitLineInFile(file, name, architecture);
            jumpToSource({file, line > 0 ? line : 1, 0, architecture.isEmpty() ? name : architecture});
        });
    if (!started)
        transcript_->appendNote(tr("** View Source: nvc could not be started"));
}

} // namespace lvd::gui
