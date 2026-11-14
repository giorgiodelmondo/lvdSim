// SPDX-License-Identifier: GPL-3.0-or-later
#include "sim/output_filter.hpp"

#include <lvdwave/wave_commands.hpp>

namespace lvd::gui {

const QByteArray& OutputFilter::rawPrompt() {
    static const QByteArray p = QByteArray("\x01\x02%\x01\x02 ", 6);
    return p;
}

FilteredOutput OutputFilter::feed(const QByteArray& chunk) {
    FilteredOutput out;

    QByteArray data = pending_ + chunk;
    pending_.clear();

    // A CR LF pair cut in half by the end of this chunk is completed by the next one.
    if (data.endsWith('\r')) {
        pending_ = "\r";
        data.chop(1);
    }

    // A prompt that is cut in half by the end of this chunk is completed by the next one.
    for (int k = pending_.isEmpty() ? rawPrompt().size() - 1 : 0; k >= 1; --k) {
        if (data.endsWith(rawPrompt().left(k))) {
            pending_ = rawPrompt().left(k);
            data.chop(k);
            break;
        }
    }

    // Prompts become a marker byte, so that their position survives; they are shown as "% ".
    const char marker = '\x03';
    for (int pos = 0; (pos = data.indexOf(rawPrompt(), pos)) >= 0; pos += rawPrompt().size())
        ++out.prompts;
    promptsTotal_ += out.prompts;
    data.replace(rawPrompt(), QByteArray(1, marker));
    data.replace(QByteArray("\x01", 1), QByteArray());
    data.replace(QByteArray("\x02", 1), QByteArray());
    data.replace("\r\n", "\n");
    auto show = [marker](QByteArray b) { return QString::fromUtf8(b.replace(QByteArray(1, marker), "% ")); };
    auto stripMarkers = [marker](QByteArray b) {
        while (b.startsWith(marker))
            b = b.mid(1);
        return b;
    };

    const QByteArray prefix(kNoticePrefix);
    const QByteArray json(kReplyPrefix);
    int start = 0;
    for (;;) {
        const int nl = data.indexOf('\n', start);
        if (nl < 0)
            break;
        const QByteArray line = data.mid(start, nl - start);

        // The shell prints its prompt before the command runs, so a notice produced by that command
        // follows the prompt on the same line. The prompt stays visible, the notice does not.
        const QByteArray rest = stripMarkers(line);
        const QByteArray lead = line.left(line.size() - rest.size());
        if (rest.startsWith(prefix)) {
            QStringList words;
            for (const std::string& w : lvd::wave::parseTclList(rest.mid(prefix.size()).toStdString()))
                words << QString::fromStdString(w);
            out.notices.push_back(words);
            out.text += show(lead);
        } else if (rest.startsWith(json)) {
            // A -json reply: its own line, cut out of the text. It belongs to the command whose prompt was
            // the last one before it, so count the prompts that are still ahead in this data.
            // The prompts on this line before the reply are not ahead of it.
            JsonLine r;
            r.text = QString::fromUtf8(rest);
            r.prompt = promptsTotal_ - static_cast<int>(data.mid(start + lead.size()).count(marker));
            out.text += show(lead);
            r.textOffset = out.text.size();
            out.replies.push_back(std::move(r));
        } else {
            out.text += show(line) + '\n';
        }
        start = nl + 1;
    }

    // An unfinished line. The shell's prompt never ends in a newline, so it must be shown at once;
    // only a line that could still become a notice or a reply is held back.
    const QByteArray tail = data.mid(start);
    const QByteArray tailRest = stripMarkers(tail);
    auto couldBe = [&tailRest](const QByteArray& p) {
        return !tailRest.isEmpty() && (p.startsWith(tailRest) || tailRest.startsWith(p));
    };
    if (!tail.isEmpty()) {
        // Held back in front of what is already pending (half a prompt or a CR): losing half a prompt would
        // leave the session busy for ever.
        if (couldBe(prefix) || couldBe(json))
            pending_ = tail + pending_;
        else
            out.text += show(tail);
    }
    return out;
}

} // namespace lvd::gui
