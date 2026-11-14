// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sim/nvc_tool.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <compile_runner.hpp>
#include <diagnostics.hpp>
#include <environment.hpp>
#include <memory>
#include <project.hpp>
#include <regression.hpp>
#include <set>
#include <text_util.hpp>

namespace lvd::gui {

/// One message of the last analysis, with the file made absolute for the Source pane.
struct Problem {
    lvd::tools::diag::Severity severity = lvd::tools::diag::Severity::Error;
    QString message;
    QString file; ///< absolute; empty if the message has no location
    int line = 0;
    int column = 0;
    QString detail;
    QString source; ///< the project file being analysed when it was printed
};

/// The open project of the GUI: the project file and its edits, analysis (lvd::tools::build::Runner) and the
/// regression (lvd::tools::regress::Runner), both driven by a timer so the window stays responsive. The
/// simulator is never linked: both run the nvc binary as child processes.
class ProjectSession : public QObject {
    Q_OBJECT
public:
    explicit ProjectSession(lvd::tools::Environment env, QObject* parent = nullptr);
    ~ProjectSession() override;

    // ---- file
    bool open(const QString& path);
    /// A new project with one library `work`, written at once.
    bool create(const QString& path, const QString& name = {});
    bool save();
    bool saveAs(const QString& path);
    void close();
    bool isOpen() const noexcept { return open_; }
    bool modified() const noexcept { return modified_; }
    QString path() const { return path_; }
    QString dir() const { return dir_; }
    QString displayName() const;
    QString lastError() const { return error_; }
    const lvd::tools::proj::Project& project() const noexcept { return project_; }

    // ---- edits (each marks the project modified and emits projectChanged)
    bool addLibrary(const QString& name);
    bool removeLibrary(int library);
    /// Adds files (absolute or relative to the current directory) at the end of the library; stored relative
    /// to the project directory. Files already in the library are skipped. Returns the number added.
    int addFiles(int library, const QStringList& files);
    bool removeFile(int library, int file);
    /// Moves a file up (delta < 0) or down within its library.
    bool moveFile(int library, int file, int delta);
    bool addTest(const lvd::tools::proj::Test& test);
    bool removeTest(int test);
    /// Replaces the whole project (import); the path stays.
    void setProject(lvd::tools::proj::Project p);

    /// Absolute path of a project file path.
    QString absolute(const QString& projectPath) const;

    // ---- analysis
    /// Status of every file (projectFiles order), as of the last refreshStatus().
    const std::vector<lvd::tools::build::FileStatus>& statuses() const noexcept { return statuses_; }
    void refreshStatus();
    /// Index in projectFiles order of a library/file pair, -1 if out of range.
    int fileIndex(int library, int file) const;
    /// Starts analysing (asynchronously). False when nothing could start (error in lastError(), or already
    /// busy).
    bool compile(lvd::tools::build::Mode mode, const std::set<int>& selected = {});
    bool compiling() const noexcept { return compiler_ != nullptr; }
    void cancelCompile();
    bool waitForCompile(int timeoutMs = 120000);
    /// File index (projectFiles order) being analysed now, -1 if none.
    int compilingFile() const noexcept { return compilingFile_; }
    const QVector<Problem>& problems() const noexcept { return problems_; }
    int lastCompileFailures() const noexcept { return lastFailures_; }

    // ---- regression
    /// Runs the tests (all, or those named: test names or job ids), after analysing what is out of date (the
    /// regression starts when that succeeded). False if nothing could start.
    bool runRegression(const QStringList& only = {});
    bool rerunFailed();
    void cancelRegression();
    bool regressionRunning() const noexcept { return regressing_; }
    bool waitForRegression(int timeoutMs = 300000);
    const lvd::tools::regress::Runner* regression() const noexcept { return regression_.get(); }
    QStringList failedIds() const;
    int jobs() const noexcept { return jobs_; }
    void setJobs(int n);
    /// The merged coverage database of the last regression, empty if none.
    QString mergedCoverage() const { return mergedCoverage_; }

    // ---- simulation
    /// vsim arguments that start a test interactively (generics, script, top level).
    QStringList vsimArgsFor(const QString& test) const;
    /// Library name -> absolute directory of every project library (and its external mappings).
    std::vector<std::pair<std::string, std::string>> libraryMappings() const;
    const lvd::tools::Environment& environment() const noexcept { return env_; }

signals:
    void projectChanged(); ///< opened, closed or edited
    void statusChanged();
    void compileStarted(int steps);
    void compileProgress(int fileIndex); ///< a file started or finished
    void compileFinished(int failed);
    void regressionStarted(int jobs);
    void jobChanged(int job);
    void regressionFinished(int passed, int total);
    void coverageMerged(const QString& database);
    /// Text for the transcript (commands, nvc output).
    void message(const QString& text);

private:
    void setError(const QString& e);
    void touched();
    void compileTick();
    void regressionTick();
    void regressionDone();
    lvd::tools::regress::PlanOptions planOptions() const;
    bool startRegression(const QStringList& only);
    void afterCompile();

    lvd::tools::Environment env_;
    lvd::tools::proj::Project project_;
    bool open_ = false;
    bool modified_ = false;
    QString path_, dir_, error_;
    std::vector<lvd::tools::build::FileStatus> statuses_;
    std::unique_ptr<lvd::tools::build::Runner> compiler_;
    QTimer compileTimer_;
    int compilingFile_ = -1;
    QVector<Problem> problems_;
    int lastFailures_ = 0;
    std::unique_ptr<lvd::tools::regress::Runner> regression_;
    QTimer regressionTimer_;
    bool regressing_ = false;
    bool regressionQueued_ = false; ///< waits for the analysis started by runRegression
    QStringList queuedTests_;
    int jobs_ = 0;
    QString outDir_;
    QString mergedCoverage_;
    NvcTool* tool_ = nullptr;
};

} // namespace lvd::gui
