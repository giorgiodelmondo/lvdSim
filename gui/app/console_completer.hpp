// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QStringList>

namespace lvd::wave {
class WaveStore;
}

namespace lvd::gui {

/// What Tab can put at the cursor of the command line.
struct Completion {
    enum class Kind { None, Command, Subcommand, Option, Value, Path, File };
    Kind kind = Kind::None;
    int start = 0;          ///< the word being completed is text[start, cursor)
    QString prefix;         ///< that word
    QStringList candidates; ///< whole replacement words, in the order shown
    QStringList labels;     ///< what the popup shows for each candidate (a path shows its last part)

    /// The longest start all candidates share (case-insensitive for paths and files, in the spelling of the
    /// first candidate). At least `prefix` when there are candidates.
    QString commonPrefix() const;
    /// True when the candidate is a container (scope, directory) that Tab can go into: no space after it.
    static bool isContainer(const QString& candidate) { return candidate.endsWith(QLatin1Char('/')); }
};

/// Tab completion of the console: commands (the ones built in, plus `info commands` of the running shell),
/// sub-commands and options of the common commands, `-radix` values, design paths from the waveform's
/// hierarchy (as the shell names them: relative to the design top, `/u_sub/a`), and file names for `do`,
/// `source`, `vcom`, `vlog`. Pure: no widgets, no shell access.
class ConsoleCompleter {
public:
    ConsoleCompleter();

    /// Commands the shell reported (`info commands`); merged with the built-in list.
    void setShellCommands(const QStringList& commands);
    bool hasShellCommands() const noexcept { return haveShell_; }
    /// Forget the shell's list (after `proc`, `do`, `source` may have defined commands); the built-in list
    /// stays.
    void invalidateShellCommands() noexcept { haveShell_ = false; }
    QStringList commands() const { return commands_; }

    /// The design hierarchy (may be null). The store must outlive its use here.
    void setStore(const lvd::wave::WaveStore* store) noexcept { store_ = store; }
    /// Base of relative file names (default: the current directory).
    void setWorkingDir(const QString& dir) { workingDir_ = dir; }

    Completion complete(const QString& text, int cursor) const;

    /// The commands known without asking the shell: the native shell commands, the command compatibility
    /// layer and the ones the GUI handles itself.
    static QStringList builtinCommands();
    /// Options offered after a command (key "add wave" for sub-commands), sorted.
    static QStringList optionsFor(const QString& commandKey);
    static QStringList subcommandsFor(const QString& command);

private:
    QStringList completePath(const QString& prefix, QStringList* labels) const;
    QStringList completeFile(const QString& prefix, const QStringList& extensions, QStringList* labels) const;

    QStringList commands_; ///< sorted, unique
    bool haveShell_ = false;
    const lvd::wave::WaveStore* store_ = nullptr;
    QString workingDir_;
};

} // namespace lvd::gui
