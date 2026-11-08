// SPDX-License-Identifier: GPL-3.0-or-later
// Tools > Compile libraries: detection, command line, option handling, a fake script that prints a summary,
// cancel.
#include "app/compile_libs_dialog.hpp"
#include "app/library_pane.hpp"
#include "app/sim_window.hpp"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <environment.hpp>
#include <libraries.hpp>

using namespace lvd::gui;
using lvd::tools::Vendor;

namespace {

#ifdef Q_OS_WIN
constexpr bool kWin = true;
#else
constexpr bool kWin = false;
#endif

bool writeFile(const QString& path, const QByteArray& data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(data);
    return true;
}

QString readFile(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromLocal8Bit(f.readAll()) : QString();
}

// The script prints what compile_libs prints, records its arguments next to itself.
QString makeScript(const QString& dir, int exitCode, bool slow, bool failedFile = false) {
    const QString path = dir + (kWin ? "/compile_libs.bat" : "/compile_libs.sh");
    QByteArray s;
    if (kWin) {
        s = "@echo off\r\n"
            "echo %* > \"%~dp0args.txt\"\r\n"
            "echo altera_mf (VHDL megafunctions)\r\n"
            "echo altera_mf: 2 ok, 0 failed, 0 skipped in 1s\r\n"
            "echo lpm\r\n"
            "echo lpm: 2 ok, " +
            QByteArray(failedFile ? "1" : "0") +
            " failed, 0 skipped in 0s\r\n"
            "echo some warning 1>&2\r\n";
        if (slow)
            s += "ping -n 60 127.0.0.1 > nul\r\n";
        s += "echo.\r\n"
             "echo library            files ok   failed  skipped    time\r\n"
             "echo altera_mf                 2        0        0      1s\r\n"
             "echo lpm                       2        " +
             QByteArray(failedFile ? "1" : "0") +
             "        0      0s\r\n"
             "exit /b " +
             QByteArray::number(exitCode) + "\r\n";
    } else {
        s = "#!/bin/sh\n"
            "echo \"$@\" > \"$(dirname \"$0\")/args.txt\"\n"
            "echo 'altera_mf (VHDL megafunctions)'\n"
            "echo 'altera_mf: 2 ok, 0 failed, 0 skipped in 1s'\n"
            "echo lpm\n"
            "echo 'lpm: 2 ok, " +
            QByteArray(failedFile ? "1" : "0") +
            " failed, 0 skipped in 0s'\n"
            "echo 'some warning' >&2\n";
        if (slow)
            s += "sleep 60\n";
        s += "echo\n"
             "echo 'library            files ok   failed  skipped    time'\n"
             "echo 'altera_mf                 2        0        0      1s'\n"
             "echo 'lpm                       2        " +
             QByteArray(failedFile ? "1" : "0") +
             "        0      0s'\n"
             "exit " +
             QByteArray::number(exitCode) + "\n";
    }
    writeFile(path, s);
    return path;
}

void makeQuartus(const QString& root, const QString& version) {
    QDir().mkpath(root + "/" + version + "/quartus/eda/sim_lib");
}
void makeVivado(const QString& root, const QString& rel) {
    QDir().mkpath(root + "/" + rel + "/data/vhdl/src/unisims");
}

} // namespace

class CompileLibsDialogTest : public QObject {
    Q_OBJECT
    QTemporaryDir tmp_;
    QString base() const { return tmp_.path(); }

    CompileLibsDialog::Config config(const QString& name, const QString& script = {}) const {
        CompileLibsDialog::Config c;
        c.settingsFile = base() + "/" + name + ".ini";
        c.scriptPath = script;
        c.appDir = base() + "/noapp";
        c.toolsDir = base() + "/tools";
        c.defaultOutputDir = base() + "/default libs";
        c.installRoots =
            QStringList{base() + "/roots/missing", base() + "/roots/intelFPGA_pro", base() + "/roots/Xilinx"};
        c.environmentValues = QStringList{};
        return c;
    }

private slots:
    void initTestCase() {
        QVERIFY(tmp_.isValid());
        makeQuartus(base() + "/roots/intelFPGA_pro", "24.2");
        makeQuartus(base() + "/roots/intelFPGA_pro", "23.4");
        makeVivado(base() + "/roots/Xilinx", "Vivado/2022.2");
        makeVivado(base() + "/roots/Xilinx", "Vivado/2023.1");
    }

    void detectsVersionFoldersPerVendor() {
        CompileLibsDialog d(config("detect"));
        d.setVendor(Vendor::Xilinx);
        QCOMPARE(d.detectedInstalls(), (QStringList{base() + "/roots/Xilinx/Vivado/2023.1",
                                                    base() + "/roots/Xilinx/Vivado/2022.2"}));
        QCOMPARE(d.installCombo()->count(), 2);
        QCOMPARE(d.installDir(), base() + "/roots/Xilinx/Vivado/2023.1");
        d.setVendor(Vendor::Altera);
        QCOMPARE(d.detectedInstalls(),
                 (QStringList{base() + "/roots/intelFPGA_pro/24.2", base() + "/roots/intelFPGA_pro/23.4"}));
        QCOMPARE(d.installDir(), base() + "/roots/intelFPGA_pro/24.2");
    }

    void environmentValueComesFirst() {
        auto c = config("env");
        makeVivado(base() + "/elsewhere", "v1");
        c.environmentValues = QStringList{base() + "/elsewhere/v1"};
        CompileLibsDialog d(c);
        d.setVendor(Vendor::Xilinx);
        QCOMPARE(d.detectedInstalls().first(), base() + "/elsewhere/v1");
        QCOMPARE(d.detectedInstalls().size(), 3);
    }

    void defaultLibraryChecksFollowTheVendor() {
        CompileLibsDialog d(config("checks"));
        d.setVendor(Vendor::Xilinx);
        QCOMPARE(d.selectedLibraries(), (QStringList{"unisim", "unimacro"}));
        QCOMPARE(d.libraryChecks().size(), 4);
        d.setVendor(Vendor::Altera);
        QCOMPARE(d.selectedLibraries(), (QStringList{"altera_mf", "lpm", "sgate", "altera_primitives"}));
        QCOMPARE(d.libraryChecks().size(), 9);
        d.setSelectedLibraries({"lpm", "tennm"});
        QCOMPARE(d.selectedLibraries(), (QStringList{"lpm", "tennm"}));
        QCOMPARE(d.outputDir(), base() + "/default libs");
        QVERIFY(d.useLibraries());
    }

    void commandLineConstruction() {
        const QString script = makeScript(base() + "/with space", 0, false);
        CompileLibsDialog d(config("cmd", script));
        d.setVendor(Vendor::Altera);
        d.setInstallDir(base() + "/roots/intelFPGA_pro/24.2");
        d.setOutputDir(base() + "/out libs");
        d.setSelectedLibraries({"altera_mf", "lpm"});
        const auto r = d.request();
        QCOMPARE(int(r.vendor), int(Vendor::Altera));
        QCOMPARE(QString::fromStdString(r.installDir), base() + "/roots/intelFPGA_pro/24.2");
        QString err;
        const QString text = d.commandText(&err);
        QVERIFY2(!text.isEmpty(), qPrintable(err));
        QVERIFY(text.contains("--quartus"));
        QVERIFY(text.contains("altera_mf,lpm"));
        QVERIFY(text.contains("out libs") || text.contains("out\\ libs"));
        if (kWin) {
            QVERIFY(text.startsWith("cmd.exe /d /s /c \"\""));
            QVERIFY(text.contains("with space\\compile_libs.bat"));
        } else {
            QVERIFY(text.startsWith("sh "));
        }
        d.setVendor(Vendor::Xilinx);
        d.setInstallDir(base() + "/roots/Xilinx/Vivado/2022.2");
        d.setSelectedLibraries({"unisim"});
        QVERIFY(d.commandText().contains("--vivado"));
        d.setSelectedLibraries({});
        QVERIFY(d.commandText(&err).isEmpty());
        QCOMPARE(err, QString("no library selected"));
    }

    void invalidInputIsRefusedWithAMessage() {
        const QString script = makeScript(base() + "/s_invalid", 0, false);
        CompileLibsDialog d(config("invalid", script));
        d.setVendor(Vendor::Altera);
        d.setInstallDir(base() + "/roots/Xilinx"); // not a Quartus folder
        d.setOutputDir(base() + "/out_invalid");
        QVERIFY(!d.start());
        QVERIFY(d.summaryLabel()->text().contains("does not look like a Quartus"));
        QVERIFY(!d.running());
        d.setInstallDir(base() + "/roots/intelFPGA_pro/24.2");
        d.setSelectedLibraries({});
        QVERIFY(!d.start());
        QVERIFY(d.summaryLabel()->text().contains("no library selected"));
    }

    void missingScriptShowsAClearMessage() {
        CompileLibsDialog d(config("noscript"));
        QVERIFY(d.scriptPath().isEmpty());
        QVERIFY(d.summaryLabel()->text().contains("was not found"));
        QVERIFY(!d.startButton()->isEnabled());
        QVERIFY(!d.start());
    }

    void runsAScriptAndReadsItsSummary() {
        const QString dir = base() + "/s_ok";
        const QString script = makeScript(dir, 0, false);
        CompileLibsDialog d(config("run", script));
        d.setVendor(Vendor::Altera);
        d.setInstallDir(base() + "/roots/intelFPGA_pro/24.2");
        d.setOutputDir(base() + "/out_run");
        d.setSelectedLibraries({"altera_mf", "lpm"});
        QSignalSpy compiled(&d, &CompileLibsDialog::librariesCompiled);
        QSignalSpy ended(&d, &CompileLibsDialog::runFinished);
        QVERIFY(d.start());
        QVERIFY(d.running());
        QVERIFY(!d.startButton()->isEnabled());
        QVERIFY(d.cancelButton()->isEnabled());
        QVERIFY(!d.start()); // not twice
        QVERIFY(d.waitUntilFinished(30000));
        QCOMPARE(d.lastExitCode(), 0);
        QVERIFY(!d.lastRunCancelled());
        QCOMPARE(ended.count(), 1);
        QCOMPARE(compiled.count(), 1);
        QCOMPARE(compiled.first().first().toString(), base() + "/out_run");
        QVERIFY2(d.summaryLabel()->text().startsWith("2 libraries (2 ok, 0 with failed files): 4 files ok"),
                 qPrintable(d.summaryLabel()->text()));
        QVERIFY(d.summaryLabel()->text().contains("exit code 0"));
        QCOMPARE(d.lastSummary().rows.size(), size_t(2));
        const QString log = d.log()->toPlainText();
        QVERIFY(log.contains("altera_mf (VHDL megafunctions)"));
        QVERIFY2(log.contains("some warning"), "stderr is shown");
        QVERIFY(log.contains("--- exit code 0"));
        QCOMPARE(d.progress()->value(), d.progress()->maximum());
        QCOMPARE(d.progressLabel()->text(), QString("Finished"));
        QVERIFY(d.startButton()->isEnabled());
        QVERIFY(!d.cancelButton()->isEnabled());
        // the arguments reached the script intact, including the spaces of the paths
        const QString args = readFile(dir + "/args.txt");
        QVERIFY2(args.contains("--quartus") && args.contains("altera_mf,lpm"), qPrintable(args));
        QVERIFY2(args.contains("out_run"), qPrintable(args));
        QVERIFY(QFileInfo(base() + "/out_run").isDir()); // created by the dialog
    }

    void spacesInThePathsSurviveTheShell() {
        const QString dir = base() + "/dir with space";
        const QString script = makeScript(dir, 0, false);
        CompileLibsDialog d(config("spaces", script));
        d.setVendor(Vendor::Altera);
        d.setInstallDir(base() + "/roots/intelFPGA_pro/24.2");
        d.setOutputDir(base() + "/out with space");
        QVERIFY(d.start());
        QVERIFY(d.waitUntilFinished(30000));
        QCOMPARE(d.lastExitCode(), 0);
        QVERIFY2(readFile(dir + "/args.txt").contains("out with space"),
                 qPrintable(readFile(dir + "/args.txt")));
    }

    void useLibrariesOffEmitsNothing() {
        const QString script = makeScript(base() + "/s_off", 0, false);
        CompileLibsDialog d(config("off", script));
        d.setVendor(Vendor::Altera);
        d.setInstallDir(base() + "/roots/intelFPGA_pro/24.2");
        d.setOutputDir(base() + "/out_off");
        d.setUseLibraries(false);
        QSignalSpy compiled(&d, &CompileLibsDialog::librariesCompiled);
        QVERIFY(d.start());
        QVERIFY(d.waitUntilFinished(30000));
        QCOMPARE(compiled.count(), 0);
    }

    void failedFilesAndExitCodeAreReported() {
        const QString script = makeScript(base() + "/s_fail", 1, false, true);
        CompileLibsDialog d(config("fail", script));
        d.setVendor(Vendor::Altera);
        d.setInstallDir(base() + "/roots/intelFPGA_pro/24.2");
        d.setOutputDir(base() + "/out_fail");
        QVERIFY(d.start());
        QVERIFY(d.waitUntilFinished(30000));
        QCOMPARE(d.lastExitCode(), 1);
        QVERIFY2(d.summaryLabel()->text().contains("1 with failed files"),
                 qPrintable(d.summaryLabel()->text()));
        QVERIFY(d.summaryLabel()->text().contains("exit code 1"));
        QVERIFY(d.log()->toPlainText().contains("--- exit code 1"));
    }

    void cancelStopsTheRun() {
        const QString script = makeScript(base() + "/s_slow", 0, true);
        CompileLibsDialog d(config("cancel", script));
        d.setVendor(Vendor::Altera);
        d.setInstallDir(base() + "/roots/intelFPGA_pro/24.2");
        d.setOutputDir(base() + "/out_cancel");
        QSignalSpy compiled(&d, &CompileLibsDialog::librariesCompiled);
        QVERIFY(d.start());
        QElapsedTimer t;
        t.start();
        while (!d.log()->toPlainText().contains("altera_mf: 2 ok") && t.elapsed() < 20000)
            QTest::qWait(50);
        QVERIFY(d.log()->toPlainText().contains("altera_mf: 2 ok"));
        QVERIFY(d.running());
        QVERIFY(d.progressLabel()->text().contains("Compiling"));
        t.restart();
        d.cancelButton()->click();
        QVERIFY(d.waitUntilFinished(15000));
        QVERIFY2(t.elapsed() < 12000, "cancel returned late");
        QVERIFY(d.lastRunCancelled());
        QVERIFY(d.summaryLabel()->text().startsWith("Cancelled"));
        QCOMPARE(compiled.count(), 0);
        QVERIFY(d.startButton()->isEnabled());
    }

    void closeWhileRunningCancelsFirst() {
        const QString script = makeScript(base() + "/s_slow2", 0, true);
        auto* d = new CompileLibsDialog(config("closing", script));
        d->setVendor(Vendor::Altera);
        d->setInstallDir(base() + "/roots/intelFPGA_pro/24.2");
        d->setOutputDir(base() + "/out_closing");
        QPointer<CompileLibsDialog> guard(d);
        QSignalSpy ended(d, &CompileLibsDialog::runFinished);
        QVERIFY(d->start());
        QTest::qWait(300);
        d->closeButton()->click();
        QElapsedTimer t;
        t.start();
        while (ended.isEmpty() && t.elapsed() < 15000)
            QTest::qWait(50);
        QCOMPARE(ended.count(), 1);
        QVERIFY(!d->running());
        delete d;
    }

    void settingsAreRemembered() {
        const auto c = config("persist");
        {
            CompileLibsDialog d(c);
            d.setVendor(Vendor::Altera);
            d.setOutputDir(base() + "/remembered");
            d.setInstallDir(base() + "/roots/intelFPGA_pro/23.4");
            d.setSelectedLibraries({"lpm"});
            d.setUseLibraries(false);
            d.closeButton()->click();
        }
        CompileLibsDialog d(c);
        QCOMPARE(int(d.vendor()), int(Vendor::Altera));
        QCOMPARE(d.outputDir(), base() + "/remembered");
        QCOMPARE(d.installDir(), base() + "/roots/intelFPGA_pro/23.4");
        QCOMPARE(d.selectedLibraries(), (QStringList{"lpm"}));
        QVERIFY(!d.useLibraries());
        d.setVendor(Vendor::Xilinx); // the other vendor keeps its own values
        QCOMPARE(d.selectedLibraries(), (QStringList{"unisim", "unimacro"}));
        d.setVendor(Vendor::Altera);
        QCOMPARE(d.selectedLibraries(), (QStringList{"lpm"}));
    }

    void simWindowSearchesTheLibraryFolders() {
        const QString libs = base() + "/libfolder";
        for (const char* n : {"altera_mf", "lpm", "unisim"})
            QVERIFY(writeFile(libs + "/" + n + "/_NVC_LIB", "x"));
        QVERIFY(writeFile(libs + "/logs/failed.txt", "x"));
        const QString settings = base() + "/simwin.ini";
        {
            SimWindow w(lvd::tools::discoverEnvironment(), settings);
            w.setDialogsEnabled(false);
            QVERIFY(w.libraryFolders().isEmpty());
            w.addLibraryFolder(libs);
            w.addLibraryFolder(libs + "/"); // same folder
            QCOMPARE(w.libraryFolders(), QStringList{libs});
            QStringList args;
            const auto ctx = w.planContext(&args, {"-L", "UNISIM", "work.top"});
            QCOMPARE(args, (QStringList{"-L", "altera_mf", "-L", "lpm"}));
            QVERIFY(ctx.libs.pathOf("lpm").has_value());
            QVERIFY(!ctx.libs.pathOf("unisim").has_value());
            QVERIFY(w.library()->libraryCount() >= 3);
        }
        {
            SimWindow w(lvd::tools::discoverEnvironment(), settings); // kept between runs
            QCOMPARE(w.libraryFolders(), QStringList{libs});
            w.removeLibraryFolder(libs);
            QVERIFY(w.libraryFolders().isEmpty());
        }
    }

    void toolsMenuOpensTheDialogAndFolderListFollows() {
        SimWindow w(lvd::tools::discoverEnvironment(), base() + "/menu.ini");
        w.setDialogsEnabled(false);
        QMenu* tools = nullptr;
        for (QAction* a : w.menuBar()->actions())
            if (a->text().remove('&') == "Tools")
                tools = a->menu();
        QVERIFY(tools);
        QAction* compile = nullptr;
        for (QAction* a : tools->actions())
            if (a->text().remove('&').startsWith("Compile libraries"))
                compile = a;
        QVERIFY(compile);
        compile->trigger();
        auto* d = w.findChild<CompileLibsDialog*>();
        QVERIFY(d);
        QVERIFY(d->isVisible());
        w.addLibraryFolder(base() + "/somewhere");
        QCOMPARE(d->folderList()->count(), 1);
        QCOMPARE(d->folderList()->item(0)->text(), base() + "/somewhere");
        compile->trigger(); // raised, not duplicated
        QCOMPARE(w.findChildren<CompileLibsDialog*>().size(), 1);
        d->close();
    }
};

QTEST_MAIN(CompileLibsDialogTest)
#include "test_compile_libs_dialog.moc"
