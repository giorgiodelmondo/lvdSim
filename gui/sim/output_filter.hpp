// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <vector>

namespace lvd::gui {

/// A one-line JSON reply (`-json` mode of the fork, see docs/protocol.md). It is removed from the text.
struct JsonLine {
    QString text;       ///< the line, without the line end
    int prompt = 0;     ///< how many prompts the shell had shown when it was printed: the reply belongs to
                        ///< the command sent after prompt number `prompt` (command index prompt - 1)
    qsizetype textOffset = 0; ///< where in FilteredOutput::text it was cut out
};

/// What came out of one chunk of the simulator's output.
struct FilteredOutput {
    QString text;                     ///< what to show in the transcript
    std::vector<QStringList> notices; ///< "@@lvd ..." lines from the command compatibility layer, as word lists
    int prompts = 0;                  ///< shell prompts seen: the shell is ready for the next command
    std::vector<JsonLine> replies;    ///< JSON replies, in order, cut out of `text`
};

/// Turns the raw output of `nvc -i` into transcript text. The shell wraps its prompt in the readline
/// markers 0x01 0x02, uses CR LF, and the command compatibility layer reports GUI relevant commands (add wave)
/// as lines starting with "@@lvd " followed by a Tcl list. Those lines are removed from the text and
/// returned as notices. Lines that start with {"v": are replies of the `-json` commands and are returned
/// separately too, with the number of the prompt they follow. Chunks may end anywhere, even inside such a line.
class OutputFilter {
public:
    FilteredOutput feed(const QByteArray& chunk);

    static constexpr const char* kNoticePrefix = "@@lvd ";
    static constexpr const char* kReplyPrefix = "{\"v\":";
    /// The prompt as the shell writes it (percent sign between readline markers).
    static const QByteArray& rawPrompt();

private:
    QByteArray pending_; ///< a partial line that might still turn out to be a notice or a reply
    int promptsTotal_ = 0; ///< prompts seen since the start
};

} // namespace lvd::gui
