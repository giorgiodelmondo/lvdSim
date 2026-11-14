// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "sim/parsers/json_replies.hpp"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace lvd::gui {

/// A place in a source file to show: the file as the kernel or a project names it, a line (1 based) and,
/// when known, the column (1 based) and the identifier to select there.
struct SourceLocation {
    QString file;
    int line = 0;
    int column = 0;
    QString name; ///< identifier at the column; empty to select nothing
};

/// The `describe` path of a signal of the waveform store: "top.uut.clk" -> "/uut/clk". The store names the
/// top-level region, the shell does not. A trailing index range of the last name ("mem[3]", "d[3:0]") is
/// dropped: the declaration is the one of the whole signal.
QString shellPathOfStoreSignal(const QString& dotted);
/// The `describe` path of a region of the store: "top.uut.g(0)" -> "/uut/g(0)/", "top" -> "/".
QString shellPathOfStoreScope(const QString& dotted);
/// `describe -json -location` for one path, the path in Tcl braces.
QString describeLocationCommand(const QString& shellPath);

/// Asks the simulator where objects and regions are declared (`describe -json -location`, kernel PR 10) and
/// keeps the answers for the session: each path is asked once. A probe at the start of a session tells
/// whether the kernel knows the option; older kernels answer without the location members (or refuse the
/// option), and then the View Declaration entries are disabled with a tool tip that says why.
class DeclarationLocator {
public:
    enum class Support { Unknown, Yes, No };

    /// Sends a command to the shell: `ok` gets the JSON reply, `failed` the error when the command fails.
    using Request = std::function<void(const QString& command, std::function<void(const QString&)> ok,
                                       std::function<void(const QString&)> failed)>;
    using Done = std::function<void(const lvd::Result<DescribedObject>&)>;

    void setRequest(Request request) { request_ = std::move(request); }

    /// A new session (or a restart): forget the answers and whether the kernel knows the option.
    void reset();
    /// Asks for the top-level region once, to learn whether the kernel reports locations.
    void probe();
    Support support() const noexcept { return support_; }
    /// Why View Declaration cannot be used now (empty when it can).
    QString unavailableReason(bool simulationRunning) const;

    /// The location of a path (from the cache, or asked now). `done` is called later, from the reply; never
    /// for an answer to a session that was reset in between.
    void describe(const QString& shellPath, Done done);
    int cachedCount() const noexcept { return static_cast<int>(cache_.size()); }
    bool isCached(const QString& shellPath) const { return cache_.contains(shellPath); }
    /// Requests sent so far (for tests: a cached path sends nothing).
    int requestsSent() const noexcept { return sent_; }

private:
    void learnSupport(const DescribedObject& o);

    Request request_;
    Support support_ = Support::Unknown;
    QHash<QString, DescribedObject> cache_;
    QHash<QString, QVector<Done>> waiting_; ///< path -> callbacks of the request on its way
    int generation_ = 0;
    int sent_ = 0;
    bool probing_ = false;
};

} // namespace lvd::gui
