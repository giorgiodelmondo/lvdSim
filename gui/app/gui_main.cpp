// SPDX-License-Identifier: GPL-3.0-or-later
// lvdsim-gui: the simulation window. Started by `vsim` without -c, or directly:
//   lvdsim-gui [vsim options] [<library>.]<top>
// Headless checks (used by the tests and for screenshots):
//   --send "command"      send a command once the simulator is idle (repeatable, in order)
//   --process NAME        select the process of that name and show its source (like a double-click)
//   --view-declaration P  after the --send commands: select the signal P (dotted store path, t.uut.clk) in
//   Objects and
//                         show its declaration, as a double-click does (repeatable; P not in Objects: asked
//                         by path)
//   --settings file.ini   keep the window layout in this file instead of the user's settings
//   --screenshot out.png  wait until the simulator is idle, save the window and exit
//   --transcript out.txt  also save the transcript text (with --screenshot)
//   --show-dialog compile-libs   open Tools > Compile libraries (with --screenshot: save the dialog and exit)
//     --dialog-vendor xilinx|altera, --dialog-install DIR, --dialog-out DIR, --dialog-run (start it and wait)
//   --open-menu NAME      with --screenshot: open that menu of the menu bar (for example Tools) in the
//   screenshot
//   --hide-dock NAME / --show-dock NAME   hide or raise a dock (library, structure, objects, transcript,
//   processes, breakpoints, source)
//   --load-format F.do / --save-format F.do   load a wave format after the --send commands, save the wave
//   format before the screenshot
//   --size W H            resize the window before the screenshot
//   --cursor FS           add a wave cursor at that time in femtoseconds (repeatable)
//   --wave-op "op args"   edit the wave rows before the screenshot (repeatable, in order; see
//   SimWindow::applyWaveOp): move-up, group, rename, edit (open the rename editor), drag (hold a drag), ...
//   --wave-context-menu NAME   with --screenshot: open the wave window's context menu on the row of that name
//   --dataset NAME=FILE.fst    open a finished waveform as a dataset once the simulator is idle (repeatable)
//   --breakpoint FILE:LINE     set a line breakpoint as a click in the Source gutter does, before the --send
//                              commands (repeatable)
//   --frame N                  after the --send commands: select frame N of the Call Stack (0 = innermost)
//   --complete TEXT            after the --send commands: type TEXT in the console and press Tab (the
//   completion
//                              popup is painted into the screenshot)
//   --crop-dock NAME           save only that dock of the screenshot (for example transcript)
//   --dock-height NAME H       make a bottom or top dock H pixels high before the screenshot
//   --dock-width NAME W        make a left, right or bottom dock W pixels wide before the screenshot
//   --coverage FILE.ncdb       load a coverage database into the Coverage dock (repeatable: merged)
//   --coverage-select PATH     select that instance in the Coverage dock (its annotated source is shown)
//   --coverage-filter TEXT     filter the Coverage hierarchy
//   --coverage-uncovered       show only what is not fully covered
//   --coverage-refresh         Simulation > Refresh coverage after the --send commands (needs -coverage)
//   --project FILE.lvdproj     open a project (Project dock); the working directory becomes its directory
//   --import-do SCRIPT.do OUT.lvdproj   build a project from a compile script and open it
//   --project-compile          analyse what is out of date and wait (Problems dock shows the messages)
//   --project-touch FILE       change the time stamp of a project file first (to show it out of date)
//   --regress                  run every test of the project as a regression and wait (Regression dock)
//   --project-select NAME      select a test (or a file path) in the Project dock
#include "app/compile_libs_dialog.hpp"
#include "app/console_input.hpp"
#include "app/coverage_pane.hpp"
#include "app/objects_pane.hpp"
#include "app/processes_pane.hpp"
#include "app/project_pane.hpp"
#include "app/project_session.hpp"
#include "app/sim_window.hpp"
#include "app/source_pane.hpp"
#include "app/transcript_widget.hpp"
#include "wave/find_value_dialog.hpp"
#include "wave/wave_widget.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QCompleter>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QStringList>
#include <QTableWidget>
#include <QTreeWidget>

#include <environment.hpp>
#include <functional>

using namespace lvd;

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QStringList vsimArgs, sends;
    QList<double> cursors;
    QString screenshot, transcriptFile, processName, settingsFile, showDialog, dialogVendor, dialogInstall,
        dialogOut, openMenu;
    bool dialogRun = false;
    QStringList hideDocks, showDocks;
    QSize windowSize;
    QString loadFormat, saveFormat, waveContextMenu;
    QStringList waveOps, datasets, lineBreakpoints, viewDeclarations;
    int frame = -1;
    QString completeText, cropDock, heightDock, widthDock;
    int dockHeight = 0, dockWidth = 0;
    QStringList coverageFiles;
    QString coverageSelect, coverageFilter;
    bool coverageUncovered = false, coverageRefresh = false;
    QString projectFile, importDo, importOut, projectSelect, projectTouch;
    bool projectCompile = false, regress = false;
    const QStringList args = QCoreApplication::arguments().mid(1);
    for (int i = 0; i < args.size(); ++i) {
        if (args[i] == "--screenshot" && i + 1 < args.size())
            screenshot = args[++i];
        else if (args[i] == "--transcript" && i + 1 < args.size())
            transcriptFile = args[++i];
        else if (args[i] == "--settings" && i + 1 < args.size())
            settingsFile = args[++i];
        else if (args[i] == "--show-dialog" && i + 1 < args.size())
            showDialog = args[++i];
        else if (args[i] == "--dialog-vendor" && i + 1 < args.size())
            dialogVendor = args[++i];
        else if (args[i] == "--dialog-install" && i + 1 < args.size())
            dialogInstall = args[++i];
        else if (args[i] == "--dialog-out" && i + 1 < args.size())
            dialogOut = args[++i];
        else if (args[i] == "--dialog-run")
            dialogRun = true;
        else if (args[i] == "--open-menu" && i + 1 < args.size())
            openMenu = args[++i];
        else if (args[i] == "--process" && i + 1 < args.size())
            processName = args[++i];
        else if (args[i] == "--view-declaration" && i + 1 < args.size())
            viewDeclarations << args[++i];
        else if (args[i] == "--load-format" && i + 1 < args.size())
            loadFormat = args[++i];
        else if (args[i] == "--save-format" && i + 1 < args.size())
            saveFormat = args[++i];
        else if (args[i] == "--hide-dock" && i + 1 < args.size())
            hideDocks << args[++i];
        else if (args[i] == "--show-dock" && i + 1 < args.size())
            showDocks << args[++i];
        else if (args[i] == "--size" && i + 2 < args.size()) {
            windowSize.setWidth(args[i + 1].toInt());
            windowSize.setHeight(args[i + 2].toInt());
            i += 2;
        } else if (args[i] == "--cursor" && i + 1 < args.size())
            cursors << args[++i].toDouble();
        else if (args[i] == "--wave-op" && i + 1 < args.size())
            waveOps << args[++i];
        else if (args[i] == "--wave-context-menu" && i + 1 < args.size())
            waveContextMenu = args[++i];
        else if (args[i] == "--dataset" && i + 1 < args.size())
            datasets << args[++i];
        else if (args[i] == "--send" && i + 1 < args.size())
            sends << args[++i];
        else if (args[i] == "--breakpoint" && i + 1 < args.size())
            lineBreakpoints << args[++i];
        else if (args[i] == "--frame" && i + 1 < args.size())
            frame = args[++i].toInt();
        else if (args[i] == "--complete" && i + 1 < args.size())
            completeText = args[++i];
        else if (args[i] == "--coverage" && i + 1 < args.size())
            coverageFiles << args[++i];
        else if (args[i] == "--coverage-select" && i + 1 < args.size())
            coverageSelect = args[++i];
        else if (args[i] == "--coverage-filter" && i + 1 < args.size())
            coverageFilter = args[++i];
        else if (args[i] == "--coverage-uncovered")
            coverageUncovered = true;
        else if (args[i] == "--coverage-refresh")
            coverageRefresh = true;
        else if (args[i] == "--project" && i + 1 < args.size())
            projectFile = args[++i];
        else if (args[i] == "--import-do" && i + 2 < args.size()) {
            importDo = args[i + 1];
            importOut = args[i + 2];
            i += 2;
        } else if (args[i] == "--project-compile")
            projectCompile = true;
        else if (args[i] == "--project-touch" && i + 1 < args.size())
            projectTouch = args[++i];
        else if (args[i] == "--regress")
            regress = true;
        else if (args[i] == "--project-select" && i + 1 < args.size())
            projectSelect = args[++i];
        else if (args[i] == "--crop-dock" && i + 1 < args.size())
            cropDock = args[++i];
        else if (args[i] == "--dock-height" && i + 2 < args.size()) {
            heightDock = args[i + 1];
            dockHeight = args[i + 2].toInt();
            i += 2;
        } else if (args[i] == "--dock-width" && i + 2 < args.size()) {
            widthDock = args[i + 1];
            dockWidth = args[i + 2].toInt();
            i += 2;
        } else
            vsimArgs << args[i];
    }

    gui::SimWindow w(tools::discoverEnvironment(), settingsFile);
    const bool headless = !screenshot.isEmpty();
    w.setDialogsEnabled(!headless);
    if (windowSize.isValid())
        w.resize(windowSize);
    for (const QString& d : hideDocks)
        w.setDockVisible(d, false);
    for (const QString& d : showDocks)
        w.setDockVisible(d, true);
    w.show();

    if (!importDo.isEmpty() && !w.importProject(importDo, importOut) && headless)
        return 10;
    if (!projectFile.isEmpty() && !w.openProject(projectFile) && headless)
        return 10;
    if (!projectTouch.isEmpty()) {
        // An empty line at the end: the contents change, so the file and its dependents are out of date.
        if (QFile g(projectTouch); g.open(QIODevice::Append))
            g.write("\n");
        w.project()->refreshStatus();
    }
    if (projectCompile &&
        (!w.project()->compile(tools::build::Mode::OutOfDate) || !w.project()->waitForCompile()) && headless)
        return 11;
    if (regress && (!w.project()->runRegression() || !w.project()->waitForRegression()) && headless)
        return 12;
    if (!projectSelect.isEmpty() && !w.projectPane()->selectTest(projectSelect)) {
        const auto files = tools::build::projectFiles(w.project()->project());
        for (const auto& f : files)
            if (QString::fromStdString(w.project()
                                           ->project()
                                           .libraries[static_cast<std::size_t>(f.library)]
                                           .files[static_cast<std::size_t>(f.file)]
                                           .path) == projectSelect)
                w.projectPane()->selectFile(f.library, f.file);
    }

    if (!vsimArgs.isEmpty()) {
        if (!w.startSimulation(vsimArgs) && headless)
            return 2;
    }

    if (showDialog == "compile-libs") {
        gui::CompileLibsDialog* d = w.showCompileLibraries();
        if (dialogVendor.compare("altera", Qt::CaseInsensitive) == 0)
            d->setVendor(tools::Vendor::Altera);
        else if (dialogVendor.compare("xilinx", Qt::CaseInsensitive) == 0)
            d->setVendor(tools::Vendor::Xilinx);
        if (!dialogInstall.isEmpty())
            d->setInstallDir(dialogInstall);
        if (!dialogOut.isEmpty())
            d->setOutputDir(dialogOut);
        if (dialogRun && (!d->start() || !d->waitUntilFinished(600000)))
            return 4;
        QCoreApplication::processEvents();
        if (headless) {
            const bool saved = d->grab().save(screenshot);
            d->close();
            w.close();
            return saved ? 0 : 1;
        }
    }

    if (headless) {
        if (!vsimArgs.isEmpty() && !w.waitUntilIdle())
            return 3;
        for (const QString& d : datasets) {
            const int eq = static_cast<int>(d.indexOf(QLatin1Char('=')));
            if (!w.openDataset(eq > 0 ? d.mid(eq + 1) : d, eq > 0 ? d.left(eq) : QString()))
                return 7;
        }
        for (const QString& b : lineBreakpoints) {
            const int colon = static_cast<int>(b.lastIndexOf(QLatin1Char(':')));
            if (colon <= 0)
                return 8;
            const QString file = b.left(colon);
            const int line = b.mid(colon + 1).toInt();
            if (w.lineBreakpoints().indexOf(file, line) < 0)
                w.toggleLineBreakpoint(file, line);
            if (!w.waitUntilIdle())
                return 3;
        }
        for (const QString& s : sends) {
            w.sendCommand(s);
            if (!w.waitUntilIdle())
                return 3;
        }
        if (frame >= 0) {
            w.selectFrame(frame);
            if (!w.waitUntilIdle())
                return 3;
        }
        for (const QString& p : viewDeclarations) {
            // As a double-click on the row of the Objects pane (expanded and selected for the screenshot).
            QTreeWidgetItem* found = nullptr;
            std::function<void(QTreeWidgetItem*)> walk = [&](QTreeWidgetItem* it) {
                if (found == nullptr && w.objects()->declarationPathOf(it) == p)
                    found = it;
                for (int c = 0; found == nullptr && c < it->childCount(); ++c)
                    walk(it->child(c));
            };
            walk(w.objects()->tree()->invisibleRootItem());
            if (found != nullptr) {
                for (QTreeWidgetItem* up = found->parent(); up != nullptr; up = up->parent())
                    up->setExpanded(true);
                w.objects()->tree()->clearSelection();
                found->setSelected(true);
                w.objects()->tree()->setCurrentItem(found);
                w.objects()->tree()->scrollToItem(found);
                w.objects()->itemDoubleClicked(found);
            } else {
                w.viewDeclaration(lvd::gui::shellPathOfStoreSignal(p));
            }
            if (!w.waitUntilIdle())
                return 3;
        }
        if (!completeText.isEmpty()) {
            w.transcript()->setCommandText(completeText);
            w.transcript()->completeAtCursor();
            if (!w.waitUntilIdle()) // a command name may have asked the shell for its commands
                return 3;
        }
        if (!coverageFiles.isEmpty() || coverageRefresh) {
            // --coverage-refresh: Simulation > Refresh coverage of the running simulation (after the --send
            // commands); --coverage: databases opened with File > Open coverage.
            bool ok = coverageRefresh ? w.refreshCoverage() && w.waitUntilIdle() && w.waitForCoverageTool()
                                      : w.openCoverage(coverageFiles) && w.waitForCoverageTool();
            ok = ok && w.coverage()->database() != nullptr;
            if (ok) {
                w.coverage()->setUncoveredOnly(coverageUncovered);
                w.coverage()->setFilter(coverageFilter);
                ok = coverageSelect.isEmpty() || w.coverage()->selectInstance(coverageSelect);
            }
            if (!ok) {
                if (QFile f(transcriptFile); !transcriptFile.isEmpty() && f.open(QIODevice::WriteOnly))
                    f.write((w.transcript()->text() + "\n" + w.lastError()).toUtf8());
                return 9;
            }
        }
        if (!loadFormat.isEmpty())
            w.loadWaveFormat(loadFormat);
        for (const QString& op : waveOps)
            if (!w.applyWaveOp(op))
                return 6;
        if (!saveFormat.isEmpty() && !w.saveWaveFormat(saveFormat))
            return 5;
        for (double t : cursors)
            w.wave()->addCursor(t);
        QCoreApplication::processEvents();
        if (!processName.isEmpty()) {
            for (int i = 0; i < w.processes()->processCount(); ++i) {
                const auto p = w.processes()->processes().at(i);
                if (p.name != processName)
                    continue;
                w.processes()->table()->selectRow(i);
                w.showSource(p.file, p.line);
            }
        }
        if (!transcriptFile.isEmpty()) {
            QFile f(transcriptFile);
            if (f.open(QIODevice::WriteOnly))
                f.write(w.transcript()->text().toUtf8());
        }
        if (auto* d = w.findChild<QDockWidget*>(heightDock + "Dock"); d && dockHeight > 0)
            w.resizeDocks({d}, {dockHeight}, Qt::Vertical);
        if (auto* d = w.findChild<QDockWidget*>(widthDock + "Dock"); d && dockWidth > 0)
            w.resizeDocks({d}, {dockWidth}, Qt::Horizontal);
        w.settleLayout();
        if (w.wave()->requestedBackend() == lvd::gui::WaveWidget::Backend::Gpu &&
            w.wave()->renderBackend() == lvd::gui::WaveWidget::Backend::Gpu) {
            // The GPU canvas needs a moment for its first frame (device, shaders, tiles).
            QElapsedTimer wait;
            wait.start();
            while (wait.elapsed() < 800)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            w.settleLayout();
        }
        QImage img = w.grab().toImage();
        if (w.wave()->renderBackend() == lvd::gui::WaveWidget::Backend::Gpu) {
            QPainter p(&img);
            p.drawImage(w.wave()->mapTo(&w, QPoint(0, 0)), w.wave()->snapshot());
        }
        if (auto* fd = w.wave()->findDialog(); fd != nullptr && fd->isVisible()) {
            // The Find Value dialog is a separate window: paint it over the top right of the wave window.
            const QPixmap dp = fd->grab();
            const QPoint at = w.wave()->mapTo(&w, QPoint(w.wave()->width() - dp.width() - 24, 70));
            QPainter p(&img);
            p.fillRect(QRect(at, dp.size()).adjusted(-1, -1, 1, 1), QColor(0x80, 0x80, 0x80));
            p.drawPixmap(at, dp);
        }
        if (!openMenu.isEmpty()) {
            for (QAction* a : w.menuBar()->actions()) {
                if (!a->menu() || QString(a->text()).remove('&') != openMenu)
                    continue;
                const QRect r = w.menuBar()->actionGeometry(a);
                const QPoint at = w.menuBar()->mapTo(&w, r.bottomLeft());
                a->menu()->popup(w.mapToGlobal(at));
                QCoreApplication::processEvents();
                QPainter p(&img);
                p.drawPixmap(at, a->menu()->grab());
            }
        }
        if (!waveContextMenu.isEmpty()) {
            lvd::gui::WaveWidget* wv = w.wave();
            int row = -1;
            for (int i = 0; i < wv->rowCount() && row < 0; ++i)
                if (wv->rowBit(i) < 0 && QString::fromStdString(wv->rowName(i)) == waveContextMenu)
                    row = i;
            if (row < 0)
                return 6;
            QMenu* menu = wv->createContextMenu(row);
            const QPoint at(wv->width() / 4, wv->rowTop(row) + wv->rowHeight());
            menu->popup(wv->mapToGlobal(at));
            QCoreApplication::processEvents();
            QPainter p(&img);
            p.drawPixmap(wv->mapTo(&w, at), menu->grab());
            menu->close();
        }
        if (w.transcript()->completionPopupVisible()) {
            // The completion popup is a window of its own: paint it above the command line, at the cursor.
            QAbstractItemView* popup = w.transcript()->completionPopupCompleter()->popup();
            lvd::gui::ConsoleInput* in = w.transcript()->input();
            const QPixmap pp = popup->grab();
            const QRect cr = in->cursorRect();
            const QPoint at =
                in->viewport()->mapTo(&w, QPoint(cr.left(), -in->viewport()->y() - pp.height() - 2));
            QPainter p(&img);
            p.fillRect(QRect(at, pp.size()).adjusted(-1, -1, 1, 1), QColor(0x80, 0x80, 0x80));
            p.drawPixmap(at, pp);
        }
        if (!cropDock.isEmpty()) {
            auto* dock = w.findChild<QDockWidget*>(cropDock + "Dock");
            if (!dock)
                return 6;
            const QRect r(dock->mapTo(&w, QPoint(0, 0)), dock->size());
            img = img.copy(QRect(r.topLeft() * img.devicePixelRatio(), r.size() * img.devicePixelRatio()));
        }
        const bool saved = img.save(screenshot);
        w.close();
        return saved ? 0 : 1;
    }
    return app.exec();
}
