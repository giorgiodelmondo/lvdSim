// SPDX-License-Identifier: GPL-3.0-or-later
// lvdsim-gui: the simulation window. Started by `vsim` without -c, or directly:
//   lvdsim-gui [vsim options] [<library>.]<top>
// Headless checks (used by the tests and for screenshots):
//   --send "command"      send a command once the simulator is idle (repeatable, in order)
//   --process NAME        select the process of that name and show its source (like a double-click)
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
#include "app/compile_libs_dialog.hpp"
#include "app/processes_pane.hpp"
#include "app/sim_window.hpp"
#include "app/source_pane.hpp"
#include "app/transcript_widget.hpp"
#include "wave/wave_widget.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QStringList>
#include <QTableWidget>

#include <environment.hpp>

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
    QStringList waveOps;
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
        else if (args[i] == "--send" && i + 1 < args.size())
            sends << args[++i];
        else
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
        if (!w.waitUntilIdle())
            return 3;
        for (const QString& s : sends) {
            w.sendCommand(s);
            if (!w.waitUntilIdle())
                return 3;
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
        const bool saved = img.save(screenshot);
        w.close();
        return saved ? 0 : 1;
    }
    return app.exec();
}
