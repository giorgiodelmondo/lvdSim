// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QDialog>
#include <QList>
#include <QProcess>
#include <QStringList>
#include <QTimer>

#include <environment.hpp>
#include <memory>
#include <optional>
#include <vendor_libs.hpp>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QVBoxLayout;

namespace lvd::gui {

/// Tools > Compile libraries: runs scripts/libs/compile_libs.{bat,sh} for the Xilinx (Vivado) or Altera
/// (Quartus) simulation libraries as a child process and shows its output live. The script and its sources
/// stay where they are; compiled libraries go to the output folder only. The dialog never blocks the event
/// loop; Cancel kills the whole process tree (taskkill /T on the process id on Windows, the process group
/// elsewhere).
class CompileLibsDialog : public QDialog {
    Q_OBJECT
public:
    struct Config {
        QString settingsFile; ///< ini file as for SimWindow, empty for the user's settings
        QString appDir;       ///< where to look for scripts/libs (default: the program directory)
        QString toolsDir;     ///< directory with vlib, vcom, vlog, ... put first on PATH (default appDir)
        QString scriptPath;   ///< run this script instead of searching (tests)
        QStringList extraEnvironment; ///< NAME=VALUE for the child, only for names the process does not set
        QString defaultOutputDir;     ///< default: <user data>/lvdsim/libraries
        std::optional<QStringList>
            installRoots; ///< folders to scan for installations (default: the usual ones)
        std::optional<QStringList>
            environmentValues; ///< values of XILINX_VIVADO etc. (default: the real environment)
    };

    /// Config for the running GUI: program directory as tools directory and the nvc settings the GUI found.
    static Config configFor(const lvd::tools::Environment& environment, const QString& settingsFile);

    explicit CompileLibsDialog(Config config, QWidget* parent = nullptr);
    ~CompileLibsDialog() override;

    lvd::tools::Vendor vendor() const;
    void setVendor(lvd::tools::Vendor vendor);
    QString installDir() const;
    void setInstallDir(const QString& dir);
    QString outputDir() const;
    void setOutputDir(const QString& dir);
    QStringList selectedLibraries() const;
    void setSelectedLibraries(const QStringList& ids);
    bool useLibraries() const;
    void setUseLibraries(bool on);

    /// The request the Start button would run (install, output and libraries as shown).
    lvd::tools::ScriptRequest request() const;
    /// "program arguments" as it will be started, empty with the reason in `error` if it cannot be.
    QString commandText(QString* error = nullptr) const;
    QString scriptPath() const { return script_; }

    /// Re-reads the installations (environment variables and the folders scanned).
    void detectInstalls();
    QStringList detectedInstalls() const;

    bool running() const;
    bool start();  ///< false (reason in the summary line) if the input is invalid or the script is missing
    void cancel(); ///< kills the script and everything it started
    /// Spin the event loop until the script has ended. False on timeout.
    bool waitUntilFinished(int timeoutMs = 120000);

    QPlainTextEdit* log() const noexcept { return log_; }
    QPushButton* startButton() const noexcept { return startButton_; }
    QPushButton* cancelButton() const noexcept { return cancelButton_; }
    QPushButton* closeButton() const noexcept { return closeButton_; }
    QComboBox* vendorCombo() const noexcept { return vendorCombo_; }
    QComboBox* installCombo() const noexcept { return installCombo_; }
    QLineEdit* outputEdit() const noexcept { return outputEdit_; }
    QList<QCheckBox*> libraryChecks() const { return checks_; }
    QCheckBox* useLibrariesCheck() const noexcept { return useLibrariesCheck_; }
    QLabel* summaryLabel() const noexcept { return summaryLabel_; }
    QLabel* progressLabel() const noexcept { return progressLabel_; }
    QProgressBar* progress() const noexcept { return progress_; }
    QListWidget* folderList() const noexcept { return folders_; }

    int lastExitCode() const noexcept { return exitCode_; }
    bool lastRunCancelled() const noexcept { return cancelled_; }
    lvd::tools::Summary lastSummary() const { return summary_; }

    /// Library folders the simulator searches (kept by the main window); shown in the dialog when set.
    void setLibraryFolders(const QStringList& folders);

signals:
    /// The script ended and compiled at least one library. `outputDir` holds the library folders and
    /// lvdsim.ini. Only emitted when "use these libraries in simulations" is checked.
    void librariesCompiled(const QString& outputDir);
    void runFinished(int exitCode, bool cancelled);
    /// "Remove" in the folder list.
    void libraryFolderRemoved(const QString& folder);

protected:
    void reject() override;
    void closeEvent(QCloseEvent*) override;

private:
    void buildUi();
    void rebuildLibraryChecks();
    void loadSettings();
    void saveSettings() const;
    void browseInstall();
    void browseOutput();
    void onReadyOutput(bool error);
    void consume(QByteArray& buffer, bool error, bool flush);
    void appendLine(const QString& text, bool error);
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void onError(QProcess::ProcessError error);
    void killTree();
    void setRunning(bool on);
    void setSummary(const QString& text, bool bad);
    QString effectiveOutputDefault() const;

    Config config_;
    QString script_;
    QProcess* process_ = nullptr;
    QProcess* killer_ = nullptr;
    QTimer killFallback_;
    QByteArray outBuf_, errBuf_;
    QString output_;       ///< stdout text, for the summary
    int startedCount_ = 0; ///< libraries finished so far
    int expected_ = 0;
    bool cancelled_ = false;
    bool closeWhenDone_ = false;
    bool failedToStart_ = false;
    int exitCode_ = -1;
    lvd::tools::Summary summary_;
    std::vector<lvd::tools::InstallCandidate> installs_;

    QComboBox* vendorCombo_ = nullptr;
    QComboBox* installCombo_ = nullptr;
    QPushButton* installBrowse_ = nullptr;
    QLineEdit* outputEdit_ = nullptr;
    QPushButton* outputBrowse_ = nullptr;
    QGroupBox* libraryBox_ = nullptr;
    QVBoxLayout* libraryLayout_ = nullptr;
    QList<QCheckBox*> checks_;
    QCheckBox* useLibrariesCheck_ = nullptr;
    QLabel* detectedLabel_ = nullptr;
    QLabel* progressLabel_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QPlainTextEdit* log_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    QListWidget* folders_ = nullptr;
    QPushButton* removeFolder_ = nullptr;
    QPushButton* startButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
    QPushButton* closeButton_ = nullptr;
    bool loading_ = false;
    lvd::tools::Vendor shown_ = lvd::tools::Vendor::Xilinx; ///< vendor whose values the widgets hold
};

} // namespace lvd::gui
