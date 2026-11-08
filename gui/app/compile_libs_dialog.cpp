// SPDX-License-Identifier: GPL-3.0-or-later
#include "compile_libs_dialog.hpp"
#include "app/portable_mode.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QStandardPaths>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QVBoxLayout>

#ifdef Q_OS_UNIX
#include <csignal>
#include <unistd.h>
#endif

namespace lvd::gui {

using lvd::tools::Vendor;

namespace {

constexpr bool kWindows =
#ifdef Q_OS_WIN
    true;
#else
    false;
#endif

std::vector<std::string> toStd(const QStringList& l) {
    std::vector<std::string> r;
    for (const QString& s : l)
        r.push_back(s.toStdString());
    return r;
}

QStringList toQt(const std::vector<std::string>& v) {
    QStringList r;
    for (const std::string& s : v)
        r << QString::fromStdString(s);
    return r;
}

const char* vendorKey(Vendor v) {
    return v == Vendor::Xilinx ? "xilinx" : "altera";
}

QString nativePath(const QString& p) {
    return QDir::toNativeSeparators(p);
}

} // namespace

CompileLibsDialog::Config CompileLibsDialog::configFor(const lvd::tools::Environment& env,
                                                       const QString& settingsFile) {
    Config c;
    c.settingsFile = settingsFile;
    c.appDir = QCoreApplication::applicationDirPath();
    c.toolsDir = c.appDir;
    // Tell the tools where the GUI found nvc, unless the user's own variables say otherwise.
    const QProcessEnvironment pe = QProcessEnvironment::systemEnvironment();
    const QString nvc = QString::fromStdString(env.nvc);
    if (!pe.contains("LVDSIM_NVC") && (nvc.contains('/') || nvc.contains('\\')))
        c.extraEnvironment << "LVDSIM_NVC=" + nvc;
    const auto& g = env.ctx.nvcGlobalArgs;
    if (!pe.contains("LVDSIM_NVC_LIBDIR") && g.size() >= 2 && g[0] == "-L")
        c.extraEnvironment << "LVDSIM_NVC_LIBDIR=" + QString::fromStdString(g[1]);
    if (!pe.contains("LVDSIM_NVC_DLLDIR") && !env.nvcDllDir.empty())
        c.extraEnvironment << "LVDSIM_NVC_DLLDIR=" + QString::fromStdString(env.nvcDllDir);
    return c;
}

CompileLibsDialog::CompileLibsDialog(Config config, QWidget* parent)
    : QDialog(parent)
    , config_(std::move(config)) {
    setWindowTitle(tr("Compile libraries"));
    if (config_.appDir.isEmpty())
        config_.appDir = QCoreApplication::applicationDirPath();
    if (config_.toolsDir.isEmpty())
        config_.toolsDir = config_.appDir;
    if (!config_.scriptPath.isEmpty()) {
        script_ = config_.scriptPath;
    } else {
        script_ = QString::fromStdString(
            lvd::tools::findLibsScript(config_.appDir.toStdString(), kWindows, [](const std::string& p) {
                return QFileInfo::exists(QString::fromStdString(p));
            }));
    }
    killFallback_.setSingleShot(true);
    killFallback_.setInterval(5000);
    connect(&killFallback_, &QTimer::timeout, this, [this] {
        if (process_ && process_->state() != QProcess::NotRunning)
            process_->kill();
    });
    buildUi();
    loadSettings();
    if (script_.isEmpty()) {
        setSummary(tr("The script %1 was not found next to the program (scripts/libs) or in the source tree. "
                      "Reinstall lvdsim or run it from a source checkout.")
                       .arg(kWindows ? "compile_libs.bat" : "compile_libs.sh"),
                   true);
        startButton_->setEnabled(false);
    }
}

CompileLibsDialog::~CompileLibsDialog() {
    if (process_ && process_->state() != QProcess::NotRunning) {
        process_->disconnect(this);
        killTree();
        if (!process_->waitForFinished(5000))
            process_->kill();
        process_->waitForFinished(2000);
    }
    if (killer_) {
        killer_->waitForFinished(3000);
        killer_ = nullptr;
    }
}

// ---------------------------------------------------------------------------------------------
// UI

void CompileLibsDialog::buildUi() {
    auto* top = new QVBoxLayout(this);
    auto* form = new QFormLayout;

    vendorCombo_ = new QComboBox;
    vendorCombo_->addItem(tr("Xilinx Vivado"), static_cast<int>(Vendor::Xilinx));
    vendorCombo_->addItem(tr("Altera Quartus (Intel FPGA)"), static_cast<int>(Vendor::Altera));
    form->addRow(tr("&Vendor:"), vendorCombo_);

    installCombo_ = new QComboBox;
    installCombo_->setEditable(true);
    installCombo_->setInsertPolicy(QComboBox::NoInsert);
    installCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    installCombo_->setMinimumContentsLength(40);
    installBrowse_ = new QPushButton(tr("Browse..."));
    auto* installRow = new QHBoxLayout;
    installRow->addWidget(installCombo_, 1);
    installRow->addWidget(installBrowse_);
    form->addRow(tr("Installation folder:"), installRow);
    detectedLabel_ = new QLabel;
    detectedLabel_->setEnabled(false);
    form->addRow(QString(), detectedLabel_);

    outputEdit_ = new QLineEdit;
    outputBrowse_ = new QPushButton(tr("Browse..."));
    auto* outRow = new QHBoxLayout;
    outRow->addWidget(outputEdit_, 1);
    outRow->addWidget(outputBrowse_);
    form->addRow(tr("Output folder:"), outRow);
    top->addLayout(form);

    libraryBox_ = new QGroupBox(tr("Libraries"));
    libraryLayout_ = new QVBoxLayout(libraryBox_);
    top->addWidget(libraryBox_);

    useLibrariesCheck_ = new QCheckBox(
        tr("Use these libraries in simulations (add the output folder to the library folders)"));
    useLibrariesCheck_->setChecked(true);
    top->addWidget(useLibrariesCheck_);

    folders_ = new QListWidget;
    folders_->setMaximumHeight(64);
    folders_->setToolTip(tr("Library folders every simulation started from this window searches (-L)"));
    removeFolder_ = new QPushButton(tr("Remove"));
    removeFolder_->setEnabled(false);
    auto* folderRow = new QHBoxLayout;
    folderRow->addWidget(folders_, 1);
    folderRow->addWidget(removeFolder_, 0, Qt::AlignTop);
    auto* folderBox = new QGroupBox(tr("Library folders in use"));
    folderBox->setLayout(folderRow);
    top->addWidget(folderBox);

    auto* progressRow = new QHBoxLayout;
    progressLabel_ = new QLabel(tr("Idle"));
    progress_ = new QProgressBar;
    progress_->setRange(0, 1);
    progress_->setValue(0);
    progress_->setTextVisible(false);
    progress_->setMaximumHeight(10);
    progressRow->addWidget(progressLabel_, 1);
    top->addLayout(progressRow);
    top->addWidget(progress_);

    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(20000);
    log_->setLineWrapMode(QPlainTextEdit::NoWrap);
    log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    log_->setMinimumHeight(180);
    top->addWidget(log_, 1);

    summaryLabel_ = new QLabel;
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    top->addWidget(summaryLabel_);

    auto* buttons = new QHBoxLayout;
    startButton_ = new QPushButton(tr("&Start"));
    startButton_->setDefault(true);
    cancelButton_ = new QPushButton(tr("&Cancel run"));
    cancelButton_->setEnabled(false);
    closeButton_ = new QPushButton(tr("C&lose"));
    buttons->addStretch(1);
    buttons->addWidget(startButton_);
    buttons->addWidget(cancelButton_);
    buttons->addWidget(closeButton_);
    top->addLayout(buttons);

    connect(vendorCombo_, &QComboBox::currentIndexChanged, this, [this] {
        if (loading_)
            return;
        saveSettings();
        loading_ = true;
        loadSettings();
        loading_ = false;
    });
    connect(installBrowse_, &QPushButton::clicked, this, [this] { browseInstall(); });
    connect(outputBrowse_, &QPushButton::clicked, this, [this] { browseOutput(); });
    connect(startButton_, &QPushButton::clicked, this, [this] { start(); });
    connect(cancelButton_, &QPushButton::clicked, this, [this] { cancel(); });
    connect(closeButton_, &QPushButton::clicked, this, [this] { reject(); });
    connect(folders_, &QListWidget::itemSelectionChanged, this,
            [this] { removeFolder_->setEnabled(!folders_->selectedItems().isEmpty()); });
    connect(removeFolder_, &QPushButton::clicked, this, [this] {
        const auto items = folders_->selectedItems();
        for (QListWidgetItem* it : items) {
            const QString f = it->text();
            delete it;
            emit libraryFolderRemoved(f);
        }
    });
    resize(760, 640);
}

void CompileLibsDialog::setLibraryFolders(const QStringList& folders) {
    folders_->clear();
    folders_->addItems(folders);
    if (folders.isEmpty()) {
        auto* it = new QListWidgetItem(tr("(none yet)"));
        it->setFlags(Qt::NoItemFlags);
        folders_->addItem(it);
    }
}

void CompileLibsDialog::rebuildLibraryChecks() {
    for (QCheckBox* c : std::as_const(checks_))
        delete c;
    checks_.clear();
    for (const auto& choice : lvd::tools::libraryChoices(vendor())) {
        auto* cb = new QCheckBox(QString::fromStdString(choice.description));
        cb->setProperty("libraryId", QString::fromStdString(choice.id));
        cb->setChecked(choice.defaultOn);
        libraryLayout_->addWidget(cb);
        checks_ << cb;
    }
}

QString CompileLibsDialog::effectiveOutputDefault() const {
    if (!config_.defaultOutputDir.isEmpty())
        return config_.defaultOutputDir;
    return defaultLibrariesDir();
}

Vendor CompileLibsDialog::vendor() const {
    return static_cast<Vendor>(vendorCombo_->currentData().toInt());
}

void CompileLibsDialog::setVendor(Vendor v) {
    const int i = vendorCombo_->findData(static_cast<int>(v));
    if (i >= 0)
        vendorCombo_->setCurrentIndex(i);
}

QString CompileLibsDialog::installDir() const {
    return installCombo_->currentText().trimmed();
}
void CompileLibsDialog::setInstallDir(const QString& dir) {
    installCombo_->setCurrentText(dir);
}
QString CompileLibsDialog::outputDir() const {
    return outputEdit_->text().trimmed();
}
void CompileLibsDialog::setOutputDir(const QString& dir) {
    outputEdit_->setText(dir);
}
bool CompileLibsDialog::useLibraries() const {
    return useLibrariesCheck_->isChecked();
}
void CompileLibsDialog::setUseLibraries(bool on) {
    useLibrariesCheck_->setChecked(on);
}

QStringList CompileLibsDialog::selectedLibraries() const {
    QStringList ids;
    for (QCheckBox* c : checks_)
        if (c->isChecked())
            ids << c->property("libraryId").toString();
    return ids;
}

void CompileLibsDialog::setSelectedLibraries(const QStringList& ids) {
    for (QCheckBox* c : checks_)
        c->setChecked(ids.contains(c->property("libraryId").toString()));
}

QStringList CompileLibsDialog::detectedInstalls() const {
    QStringList l;
    for (const auto& c : installs_)
        l << QString::fromStdString(c.path);
    return l;
}

void CompileLibsDialog::detectInstalls() {
    const Vendor v = vendor();
    if (config_.installRoots || config_.environmentValues) {
        installs_ = lvd::tools::detectInstalls(
            v, config_.environmentValues ? toStd(*config_.environmentValues) : std::vector<std::string>{},
            config_.installRoots ? toStd(*config_.installRoots) : lvd::tools::defaultInstallRoots(v));
    } else {
        installs_ = lvd::tools::detectInstalls(v);
    }
    const QString keep = installDir();
    installCombo_->clear();
    for (const auto& c : installs_)
        installCombo_->addItem(QString::fromStdString(c.path));
    if (!keep.isEmpty())
        installCombo_->setCurrentText(keep);
    else if (!installs_.empty())
        installCombo_->setCurrentIndex(0);
    detectedLabel_->setText(installs_.empty()
                                ? tr("No installation found automatically: browse to the folder.")
                                : tr("%n installation(s) found", nullptr, int(installs_.size())));
}

void CompileLibsDialog::loadSettings() {
    auto s = openGuiSettings(config_.settingsFile);
    const bool first = vendorCombo_->property("loaded").isNull();
    if (first) {
        vendorCombo_->setProperty("loaded", true);
        loading_ = true;
        setVendor(
            static_cast<Vendor>(s->value("compileLibs/vendor", static_cast<int>(Vendor::Xilinx)).toInt()));
        loading_ = false;
        outputEdit_->setText(s->value("compileLibs/outputDir", effectiveOutputDefault()).toString());
        useLibrariesCheck_->setChecked(s->value("compileLibs/useLibraries", true).toBool());
    }
    shown_ = vendor();
    const QString key = vendorKey(shown_);
    rebuildLibraryChecks();
    if (s->contains(QString("compileLibs/libraries/") + key))
        setSelectedLibraries(s->value(QString("compileLibs/libraries/") + key).toStringList());
    installCombo_->setCurrentText(s->value(QString("compileLibs/install/") + key).toString());
    detectInstalls();
}

void CompileLibsDialog::saveSettings() const {
    auto s = openGuiSettings(config_.settingsFile);
    const QString key = vendorKey(shown_);
    s->setValue("compileLibs/vendor", static_cast<int>(vendor()));
    s->setValue("compileLibs/outputDir", outputDir());
    s->setValue("compileLibs/useLibraries", useLibraries());
    s->setValue(QString("compileLibs/install/") + key, installDir());
    s->setValue(QString("compileLibs/libraries/") + key, selectedLibraries());
    s->sync();
}

void CompileLibsDialog::browseInstall() {
    const QString start = installDir().isEmpty() ? QDir::homePath() : installDir();
    const QString d = QFileDialog::getExistingDirectory(
        this,
        vendor() == Vendor::Xilinx ? tr("Vivado installation folder") : tr("Quartus installation folder"),
        start);
    if (!d.isEmpty())
        installCombo_->setCurrentText(d);
}

void CompileLibsDialog::browseOutput() {
    const QString d =
        QFileDialog::getExistingDirectory(this, tr("Output folder for the compiled libraries"), outputDir());
    if (!d.isEmpty())
        outputEdit_->setText(d);
}

void CompileLibsDialog::setSummary(const QString& text, bool bad) {
    summaryLabel_->setText(text);
    summaryLabel_->setStyleSheet(bad ? "color: #b00020; font-weight: bold;" : "font-weight: bold;");
}

void CompileLibsDialog::setRunning(bool on) {
    startButton_->setEnabled(!on && !script_.isEmpty());
    cancelButton_->setEnabled(on);
    for (QWidget* w :
         std::initializer_list<QWidget*>{vendorCombo_, installCombo_, installBrowse_, outputEdit_,
                                         outputBrowse_, libraryBox_, useLibrariesCheck_})
        w->setEnabled(!on);
}

// ---------------------------------------------------------------------------------------------
// Running the script

lvd::tools::ScriptRequest CompileLibsDialog::request() const {
    lvd::tools::ScriptRequest r;
    r.vendor = vendor();
    r.installDir = installDir().toStdString();
    r.outDir = outputDir().toStdString();
    r.libraries = toStd(selectedLibraries());
    return r;
}

QString CompileLibsDialog::commandText(QString* error) const {
    std::string err;
    const auto args = lvd::tools::buildScriptArgs(request(), &err);
    if (args.empty()) {
        if (error)
            *error = QString::fromStdString(err);
        return {};
    }
    if (script_.isEmpty()) {
        if (error)
            *error = tr("script not found");
        return {};
    }
    const auto cmd = lvd::tools::scriptCommand(kWindows, script_.toStdString(), args);
    if (error)
        error->clear();
    if (kWindows)
        return QString::fromStdString(cmd.program) + " " + QString::fromStdString(cmd.nativeArguments);
    return QString::fromStdString(cmd.program) + " " + toQt(cmd.args).join(' ');
}

bool CompileLibsDialog::running() const {
    return process_ && process_->state() != QProcess::NotRunning;
}

bool CompileLibsDialog::start() {
    if (running())
        return false;
    QString error;
    const QString text = commandText(&error);
    if (text.isEmpty()) {
        setSummary(tr("Cannot start: %1").arg(error), true);
        return false;
    }
    if (!lvd::tools::looksLikeInstall(vendor(), installDir().toStdString())) {
        setSummary(
            vendor() == Vendor::Xilinx
                ? tr("'%1' does not look like a Vivado installation (no data/vhdl/src/unisims inside).")
                      .arg(installDir())
                : tr("'%1' does not look like a Quartus installation (no eda/sim_lib inside).")
                      .arg(installDir()),
            true);
        return false;
    }
    if (!QDir().mkpath(outputDir())) {
        setSummary(tr("Cannot create the output folder %1").arg(outputDir()), true);
        return false;
    }
    saveSettings();

    const auto cmd =
        lvd::tools::scriptCommand(kWindows, script_.toStdString(), lvd::tools::buildScriptArgs(request()));
    delete process_;
    process_ = new QProcess(this);
    process_->setProgram(QString::fromStdString(cmd.program));
    if (kWindows) {
#ifdef Q_OS_WIN
        process_->setNativeArguments(QString::fromStdString(cmd.nativeArguments));
#endif
    } else {
        process_->setArguments(toQt(cmd.args));
    }
    process_->setWorkingDirectory(outputDir());
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString path = nativePath(config_.toolsDir) + QDir::listSeparator() + env.value("PATH");
#ifdef Q_OS_WIN
    // The script needs findstr, where, ping ... even if the GUI was started with a reduced PATH.
    const QString root = nativePath(env.value("SystemRoot", "C:/Windows"));
    path += QString(QDir::listSeparator()) + nativePath(root + "/System32") + QDir::listSeparator() + root;
#endif
    env.insert("PATH", path);
    for (const QString& kv : std::as_const(config_.extraEnvironment)) {
        const int eq = kv.indexOf('=');
        if (eq > 0 && !env.contains(kv.left(eq)))
            env.insert(kv.left(eq), kv.mid(eq + 1));
    }
    process_->setProcessEnvironment(env);
#ifdef Q_OS_UNIX
    process_->setChildProcessModifier(
        [] { ::setsid(); }); // own process group, so cancel can signal the group
#endif
    connect(process_, &QProcess::readyReadStandardOutput, this, [this] { onReadyOutput(false); });
    connect(process_, &QProcess::readyReadStandardError, this, [this] { onReadyOutput(true); });
    connect(process_, &QProcess::finished, this, &CompileLibsDialog::onFinished);
    connect(process_, &QProcess::errorOccurred, this, &CompileLibsDialog::onError);

    log_->clear();
    output_.clear();
    outBuf_.clear();
    errBuf_.clear();
    summary_ = {};
    startedCount_ = 0;
    cancelled_ = false;
    failedToStart_ = false;
    exitCode_ = -1;
    expected_ = lvd::tools::expectedLibraryCount(toStd(selectedLibraries()));
    progress_->setRange(0, std::max(1, expected_));
    progress_->setValue(0);
    progressLabel_->setText(tr("Starting..."));
    setSummary(tr("Running..."), false);
    appendLine("> " + text, false);
    setRunning(true);
    process_->start();
    return true;
}

void CompileLibsDialog::appendLine(const QString& text, bool error) {
    QScrollBar* sb = log_->verticalScrollBar();
    const bool atEnd = sb->value() >= sb->maximum() - 2;
    QTextCharFormat fmt;
    if (error)
        fmt.setForeground(QColor("#b00020"));
    QTextCursor c(log_->document());
    c.movePosition(QTextCursor::End);
    if (!log_->document()->isEmpty())
        c.insertBlock();
    c.insertText(text, fmt);
    if (atEnd)
        sb->setValue(sb->maximum());
}

void CompileLibsDialog::consume(QByteArray& buffer, bool error, bool flush) {
    int nl;
    while ((nl = buffer.indexOf('\n')) >= 0) {
        QString line = QString::fromLocal8Bit(buffer.left(nl));
        buffer.remove(0, nl + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        appendLine(line, error);
        if (error)
            continue;
        output_ += line + '\n';
        const lvd::tools::ScriptLine l = lvd::tools::parseScriptLine(line.toStdString());
        if (l.kind == lvd::tools::LineKind::Started) {
            progressLabel_->setText(tr("Compiling %1 (%2 of %3)")
                                        .arg(QString::fromStdString(l.library))
                                        .arg(std::min(startedCount_ + 1, std::max(1, expected_)))
                                        .arg(expected_));
        } else if (l.kind == lvd::tools::LineKind::LibraryDone) {
            ++startedCount_;
            progress_->setValue(std::min(startedCount_, progress_->maximum()));
        }
    }
    if (flush && !buffer.isEmpty()) {
        const QString line = QString::fromLocal8Bit(buffer);
        buffer.clear();
        appendLine(line, error);
        if (!error)
            output_ += line + '\n';
    }
}

void CompileLibsDialog::onReadyOutput(bool error) {
    if (!process_)
        return;
    if (error) {
        errBuf_ += process_->readAllStandardError();
        consume(errBuf_, true, false);
    } else {
        outBuf_ += process_->readAllStandardOutput();
        consume(outBuf_, false, false);
    }
}

void CompileLibsDialog::onError(QProcess::ProcessError e) {
    if (e != QProcess::FailedToStart)
        return;
    failedToStart_ = true;
    appendLine(tr("Cannot start %1: %2").arg(process_->program(), process_->errorString()), true);
    setRunning(false);
    progressLabel_->setText(tr("Idle"));
    setSummary(tr("Could not start the script: %1").arg(process_->errorString()), true);
    emit runFinished(-1, false);
}

void CompileLibsDialog::onFinished(int exitCode, QProcess::ExitStatus status) {
    killFallback_.stop();
    outBuf_ += process_->readAllStandardOutput();
    errBuf_ += process_->readAllStandardError();
    consume(outBuf_, false, true);
    consume(errBuf_, true, true);
    exitCode_ = exitCode;
    summary_ = lvd::tools::parseSummary(output_.toStdString());
    setRunning(false);
    progressLabel_->setText(cancelled_ ? tr("Cancelled") : tr("Finished"));
    if (cancelled_) {
        setSummary(tr("Cancelled. Libraries compiled before the cancel stay in %1.").arg(outputDir()), true);
    } else if (status == QProcess::CrashExit) {
        setSummary(tr("The script ended abnormally (%1).")
                       .arg(QString::fromStdString(lvd::tools::summaryText(summary_))),
                   true);
    } else {
        setSummary(tr("%1; exit code %2.")
                       .arg(QString::fromStdString(lvd::tools::summaryText(summary_)))
                       .arg(exitCode),
                   exitCode != 0);
        if (exitCode == 0)
            progress_->setValue(progress_->maximum());
    }
    appendLine(tr("--- exit code %1%2").arg(exitCode).arg(cancelled_ ? tr(" (cancelled)") : QString()),
               exitCode != 0);
    const bool offer = !cancelled_ && useLibraries() && (exitCode == 0 || summary_.filesOk > 0);
    emit runFinished(exitCode, cancelled_);
    if (offer)
        emit librariesCompiled(outputDir());
    if (closeWhenDone_)
        QDialog::reject();
}

void CompileLibsDialog::cancel() {
    if (!running())
        return;
    cancelled_ = true;
    progressLabel_->setText(tr("Cancelling..."));
    cancelButton_->setEnabled(false);
    killTree();
}

void CompileLibsDialog::killTree() {
    if (!process_ || process_->state() == QProcess::NotRunning)
        return;
    const qint64 pid = process_->processId();
    killFallback_.start();
#ifdef Q_OS_WIN
    if (pid > 0) {
        // taskkill by process id with /T takes the whole tree (cmd, vlib/vcom/vlog, nvc). Never by image
        // name.
        auto* k = new QProcess(this);
        killer_ = k;
        connect(k, &QProcess::finished, this, [this, k] {
            if (killer_ == k)
                killer_ = nullptr;
            k->deleteLater();
        });
        connect(k, &QProcess::errorOccurred, this, [this] {
            if (process_ && process_->state() != QProcess::NotRunning)
                process_->kill();
        });
        k->start(QProcessEnvironment::systemEnvironment().value("SystemRoot", "C:/Windows") +
                     "/System32/taskkill.exe",
                 {"/PID", QString::number(pid), "/T", "/F"});
        return;
    }
#else
    if (pid > 0) {
        ::kill(-static_cast<pid_t>(pid), SIGTERM); // the group made by setsid
        return;
    }
#endif
    process_->kill();
}

bool CompileLibsDialog::waitUntilFinished(int timeoutMs) {
    QElapsedTimer t;
    t.start();
    while (running() && t.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QCoreApplication::processEvents();
    return !running();
}

void CompileLibsDialog::reject() {
    if (running()) {
        closeWhenDone_ = true;
        cancel();
        return;
    }
    saveSettings();
    QDialog::reject();
}

void CompileLibsDialog::closeEvent(QCloseEvent* e) {
    if (running()) {
        closeWhenDone_ = true;
        cancel();
        e->ignore();
        return;
    }
    saveSettings();
    QDialog::closeEvent(e);
}

} // namespace lvd::gui
