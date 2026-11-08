// SPDX-License-Identifier: GPL-3.0-or-later
// Portable mode decision: marker, environment, non-writable fallback, settings and library locations.
#include "app/compile_libs_dialog.hpp"
#include "app/portable_mode.hpp"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

using namespace lvd::gui;

namespace {
void touch(const QString& path) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
}

struct EnvGuard {
    QByteArray old;
    bool had;
    EnvGuard()
        : old(qgetenv("LVDSIM_PORTABLE"))
        , had(qEnvironmentVariableIsSet("LVDSIM_PORTABLE")) {}
    ~EnvGuard() {
        if (had)
            qputenv("LVDSIM_PORTABLE", old);
        else
            qunsetenv("LVDSIM_PORTABLE");
    }
};
} // namespace

class TestPortableMode : public QObject {
    Q_OBJECT
private slots:
    void noMarkerNoEnvIsNotPortable() {
        QTemporaryDir d;
        const auto r = resolvePortable(d.path(), "");
        QVERIFY(!r.portable);
        QVERIFY(!r.requested);
        QVERIFY(r.settingsFile.isEmpty());
        QVERIFY(r.librariesDir.isEmpty());
        QVERIFY(!QFileInfo::exists(d.path() + "/data")); // nothing created
    }
    void markerSelectsIniAndLibraries() {
        QTemporaryDir d;
        touch(d.path() + "/portable.txt");
        const auto r = resolvePortable(d.path(), "");
        QVERIFY(r.portable);
        QCOMPARE(r.settingsFile, QDir::cleanPath(d.path()) + "/data/lvdsim-gui.ini");
        QCOMPARE(r.librariesDir, QDir::cleanPath(d.path()) + "/data/libraries");
        QVERIFY(QFileInfo(d.path() + "/data").isDir()); // created on demand
        QVERIFY(r.note.isEmpty());
    }
    void environmentOverrides_data() {
        QTest::addColumn<QString>("value");
        QTest::addColumn<bool>("expected");
        QTest::newRow("1") << "1" << true;
        QTest::newRow("true") << "TRUE" << true;
        QTest::newRow("yes") << "yes" << true;
        QTest::newRow("0") << "0" << false;
        QTest::newRow("empty") << "" << false;
        QTest::newRow("junk") << "maybe" << false;
    }
    void environmentOverrides() {
        QFETCH(QString, value);
        QFETCH(bool, expected);
        QTemporaryDir d;
        QCOMPARE(resolvePortable(d.path(), value).portable, expected);
    }
    void notWritableFallsBackWithNote() {
        QTemporaryDir d;
        touch(d.path() + "/portable.txt");
        const auto r = resolvePortable(d.path(), "", [](const QString&) { return false; });
        QVERIFY(!r.portable);
        QVERIFY(r.requested);
        QVERIFY(r.settingsFile.isEmpty());
        QVERIFY(r.note.contains("not writable"));
    }
    void realUnwritableDataDirFallsBack() {
        // `data` exists as a plain file: it cannot become a directory, so the probe fails on every platform.
        QTemporaryDir d;
        touch(d.path() + "/portable.txt");
        touch(d.path() + "/data");
        const auto r = resolvePortable(d.path(), "");
        QVERIFY(!r.portable);
        QVERIFY(!r.note.isEmpty());
    }
    void linuxBundleLayoutUsesTheBundleRoot() {
        QTemporaryDir d;
        QVERIFY(QDir().mkpath(d.path() + "/bin"));
        touch(d.path() + "/portable.txt");
        QCOMPARE(portableBaseDir(d.path() + "/bin"), QDir::cleanPath(d.path() + "/bin"));
        touch(d.path() + "/AppRun");
        QCOMPARE(portableBaseDir(d.path() + "/bin"), QDir::cleanPath(d.path()));
        QVERIFY(resolvePortable(portableBaseDir(d.path() + "/bin"), "").portable);
        QCOMPARE(resolvePortable(portableBaseDir(d.path() + "/bin"), "").settingsFile,
                 QDir::cleanPath(d.path()) + "/data/lvdsim-gui.ini");
    }
    void explicitSettingsFileWins() {
        EnvGuard g;
        qputenv("LVDSIM_PORTABLE", "1");
        QTemporaryDir d;
        const QString f = d.path() + "/own.ini";
        auto s = openGuiSettings(f);
        QCOMPARE(QDir::cleanPath(s->fileName()), QDir::cleanPath(f));
        QCOMPARE(s->format(), QSettings::IniFormat);
    }
    void settingsGoToThePortableIni() {
        // The running program's directory is the test's build folder: use the marker through the environment
        // and clean the data folder afterwards.
        EnvGuard g;
        qputenv("LVDSIM_PORTABLE", "1");
        const auto dec = portableDecision();
        QVERIFY(dec.portable);
        const bool existed = QFileInfo::exists(dec.dataDir);
        {
            auto s = openGuiSettings({});
            QCOMPARE(QDir::cleanPath(s->fileName()), dec.settingsFile);
            s->setValue("probe/x", 1);
            s->sync();
        }
        QVERIFY(QFileInfo::exists(dec.settingsFile));
        QCOMPARE(defaultLibrariesDir(), dec.librariesDir);
        QFile::remove(dec.settingsFile);
        if (!existed)
            QDir(dec.dataDir).removeRecursively();
    }
    void notPortableUsesUserStoreAndStandardLibraries() {
        EnvGuard g;
        qunsetenv("LVDSIM_PORTABLE");
        if (QFileInfo::exists(QCoreApplication::applicationDirPath() + "/portable.txt"))
            QSKIP("a portable.txt sits next to the test program");
        QVERIFY(!portableDecision().portable);
        QVERIFY(defaultLibrariesDir().endsWith("/lvdsim/libraries"));
    }
    void compileDialogDefaultFollowsPortableMode() {
        EnvGuard g;
        qputenv("LVDSIM_PORTABLE", "1");
        const auto dec = portableDecision();
        QVERIFY(dec.portable);
        const bool existed = QFileInfo::exists(dec.dataDir);
        QTemporaryDir t;
        CompileLibsDialog::Config c;
        c.settingsFile = t.path() + "/s.ini"; // explicit file: the output folder is not stored yet
        CompileLibsDialog dlg(c);
        QCOMPARE(QDir::cleanPath(dlg.outputDir()), dec.librariesDir);
        if (!existed)
            QDir(dec.dataDir).removeRecursively();
    }
};

QTEST_MAIN(TestPortableMode)
#include "test_portable_mode.moc"
