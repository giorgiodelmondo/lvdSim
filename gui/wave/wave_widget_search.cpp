// SPDX-License-Identifier: GPL-3.0-or-later
// WaveWidget: edge navigation and value search. The searching itself is in libwave (lvdwave/search.hpp); this
// file maps rows to search targets, picks the nearest hit over the rows and moves the cursor.
#include "wave/find_value_dialog.hpp"
#include "wave/view_state.hpp"
#include "wave/wave_widget.hpp"

#include <cmath>
#include <tuple>
#include <utility>

namespace lvd::gui {

using namespace lvd::wave;

namespace {

/// A virtual bus row as a libwave bits sequence.
class BusBits final : public BitsSequence {
public:
    explicit BusBits(const BusSource& bus)
        : bus_(bus) {}
    std::size_t count() const override { return bus_.count(); }
    Time time(std::size_t change) const override { return bus_.time(change); }
    std::string bits(std::size_t change) const override { return bus_.bits(change); }

private:
    const BusSource& bus_;
};

/// Start of a search from a cursor at time t (a cursor may sit between two integer times): forward searches
/// look strictly after floor(t), backward ones strictly before ceil(t), so an edge exactly under the cursor
/// is skipped either way.
std::optional<Time> searchFrom(std::optional<double> cursor, int dir) {
    if (!cursor)
        return std::nullopt;
    const double t = std::max(0.0, *cursor);
    return static_cast<Time>(dir > 0 ? std::floor(t) : std::ceil(t));
}

bool better(std::optional<Time> candidate, std::optional<Time> best, int dir) {
    return candidate && (!best || (dir > 0 ? *candidate < *best : *candidate > *best));
}

const char* edgeWord(EdgeKind kind) {
    switch (kind) {
    case EdgeKind::Rising:
        return "rising edge";
    case EdgeKind::Falling:
        return "falling edge";
    case EdgeKind::Any:
        break;
    }
    return "transition";
}

} // namespace

std::vector<int> WaveWidget::searchRows() const {
    std::vector<int> out;
    for (int i = 0; i < rowCount(); ++i) {
        const Row& r = rows_[static_cast<std::size_t>(i)];
        if (r.source && isSelected(r))
            out.push_back(i);
    }
    return out;
}

Radix WaveWidget::rowRadix(int row) const {
    const Row& r = rows_.at(static_cast<std::size_t>(row));
    if (r.bit >= 0)
        return Radix::Binary;
    if (r.item->radix != Radix::Default)
        return r.item->radix;
    if (r.item->kind == ItemKind::VirtualBus)
        return Radix::Hex;
    if (r.signal != nullptr && r.signal->width() == 1)
        return Radix::Binary;
    return Radix::Hex;
}

bool WaveWidget::rowIsNumeric(int row) const {
    const Row& r = rows_.at(static_cast<std::size_t>(row));
    if (r.bit >= 0 || r.signal == nullptr)
        return false;
    const Signal& s = *r.signal;
    return s.kind() == SignalKind::Real || (s.kind() == SignalKind::Logic && s.width() <= 64);
}

QString WaveWidget::timeText(double t) const {
    return QString::fromStdString(formatTime(t, store_ ? store_->timescaleExponent() : -15));
}

void WaveWidget::moveActiveCursorTo(double t) {
    setCursorTime(cursorCount() == 0 ? 0 : activeCursor_, t);
    ensureVisible(t);
}

void WaveWidget::setEdgeKind(EdgeKind kind) {
    if (kind == edgeKind_)
        return;
    edgeKind_ = kind;
    emit edgeKindChanged(kind);
}

bool WaveWidget::jumpToEdge(int dir) {
    return jumpToEdge(dir, edgeKind_);
}

bool WaveWidget::jumpToEdge(int dir, EdgeKind kind) {
    if (dir == 0 || !store_)
        return false;
    std::vector<int> rows = searchRows();
    const bool selection = !rows.empty();
    if (!selection)
        for (int i = 0; i < rowCount(); ++i)
            if (rows_[static_cast<std::size_t>(i)].source && rows_[static_cast<std::size_t>(i)].bit < 0)
                rows.push_back(i);

    const auto cursor = activeCursorTime();
    const std::optional<double> ref = cursor ? cursor : std::optional<double>(view_.startTime);
    const std::optional<Time> from = searchFrom(ref, dir);
    const SearchDirection sd = dir > 0 ? SearchDirection::Forward : SearchDirection::Backward;

    std::optional<Time> best;
    bool anyUsable = false;
    for (int i : rows) {
        const Row& r = rows_[static_cast<std::size_t>(i)];
        std::optional<Time> hit;
        const Signal* data = r.signal; // per-bit rows carry the data of their vector
        if (data != nullptr) {
            const Signal& s = *data;
            const bool singleBit = s.kind() == SignalKind::Logic && (r.bit >= 0 || s.width() == 1);
            if (kind != EdgeKind::Any && !singleBit)
                continue;
            anyUsable = true;
            hit = findEdge(SearchTarget{&s, r.bit}, from, sd, kind);
        } else if (const auto* bus = dynamic_cast<const BusSource*>(r.source.get());
                   bus && kind == EdgeKind::Any) {
            anyUsable = true;
            hit = findEdge(BusBits(*bus), from, sd);
        }
        if (better(hit, best, dir))
            best = hit;
    }
    if (!best) {
        const QString where = selection ? tr("the selected rows") : tr("any row");
        if (!anyUsable && kind != EdgeKind::Any)
            emit navigationMessage(tr("Rising and falling edges need a single-bit row (a 1 bit signal or a "
                                      "bit of an expanded vector)"));
        else
            emit navigationMessage(tr("No %1 %2 %3 on %4")
                                       .arg(QLatin1String(edgeWord(kind)),
                                            dir > 0 ? tr("after") : tr("before"), timeText(ref.value_or(0.0)),
                                            where));
        return false;
    }
    moveActiveCursorTo(static_cast<double>(*best));
    QString word = QLatin1String(edgeWord(kind));
    word[0] = word[0].toUpper();
    emit navigationMessage(tr("%1 at %2").arg(word, timeText(static_cast<double>(*best))));
    return true;
}

WaveWidget::FindResult WaveWidget::findValue(const FindSpec& spec, int dir, bool wrap) {
    FindResult res;
    lastFind_ = spec;
    lastFindWrap_ = wrap;
    if (!store_ || dir == 0) {
        res.message = tr("Nothing to search");
        emit navigationMessage(res.message);
        return res;
    }
    const std::vector<int> rows = searchRows();
    if (rows.empty()) {
        res.message = tr("Select the signals to search first");
        emit navigationMessage(res.message);
        return res;
    }
    const bool crossing = spec.condition == SearchCondition::CrossRising ||
                          spec.condition == SearchCondition::CrossFalling ||
                          spec.condition == SearchCondition::CrossEither;
    double threshold = 0.0;
    if (crossing) {
        bool ok = false;
        threshold = spec.value.trimmed().toDouble(&ok);
        if (!ok) {
            res.message = tr("The threshold \"%1\" is not a number").arg(spec.value.trimmed());
            emit navigationMessage(res.message);
            return res;
        }
    }

    // One query per row: the value is read in the row's radix and width.
    struct Prepared {
        int row;
        ValueQuery query;
        const Signal* signal = nullptr;
        int bit = -1;
        const BusSource* bus = nullptr;
        const NumericIndex* index = nullptr;
    };
    std::vector<Prepared> prepared;
    QStringList skipped;
    for (int i : rows) {
        const Row& r = rows_[static_cast<std::size_t>(i)];
        Prepared p{i, {}, nullptr, -1, nullptr, nullptr};
        p.query.condition = spec.condition;
        SignalKind kind = SignalKind::Logic;
        std::uint32_t width = 1;
        const Signal* data = r.signal;
        if (data != nullptr) {
            p.signal = data;
            p.bit = r.bit;
            kind = p.signal->kind();
            width = r.bit >= 0 ? 1u : p.signal->width();
            if (crossing) {
                if (!rowIsNumeric(i)) {
                    skipped << QString::fromStdString(nameOf(r)) + tr(" (not numeric)");
                    continue;
                }
                p.query.threshold = threshold;
                p.query.isSigned =
                    r.item->radix == Radix::Signed || (r.item->analog.enabled && r.item->analog.isSigned);
                p.index = numericIndexFor(*data, p.query.isSigned);
            }
        } else if (const auto* bus = dynamic_cast<const BusSource*>(r.source.get())) {
            if (crossing || bus->count() == 0) {
                skipped << QString::fromStdString(nameOf(r)) +
                               (crossing ? tr(" (not numeric)") : tr(" (no data)"));
                continue;
            }
            p.bus = bus;
            width = static_cast<std::uint32_t>(bus->bits(0).size());
        } else {
            continue;
        }
        if (!crossing) {
            auto pat = ValuePattern::parse(spec.value.toStdString(), rowRadix(i), kind, width);
            if (!pat.ok()) {
                skipped << QString::fromStdString(nameOf(r)) + ": " + QString::fromStdString(pat.error());
                continue;
            }
            p.query.pattern = std::move(pat).value();
        }
        prepared.push_back(std::move(p));
    }
    if (prepared.empty()) {
        res.message = tr("Nothing to search: %1").arg(skipped.join(QStringLiteral("; ")));
        emit navigationMessage(res.message);
        return res;
    }

    const SearchDirection sd = dir > 0 ? SearchDirection::Forward : SearchDirection::Backward;
    auto run = [&](std::optional<Time> from) {
        std::optional<Time> best;
        int bestRow = -1;
        for (const Prepared& p : prepared) {
            const std::optional<Time> hit =
                p.bus != nullptr
                    ? lvd::wave::findValue(BusBits(*p.bus), p.query, from, sd)
                    : lvd::wave::findValue(SearchTarget{p.signal, p.bit}, p.query, from, sd, p.index);
            if (better(hit, best, dir)) {
                best = hit;
                bestRow = p.row;
            }
        }
        return std::make_pair(best, bestRow);
    };
    auto [best, bestRow] = run(searchFrom(activeCursorTime(), dir));
    if (!best && wrap && activeCursorTime()) {
        std::tie(best, bestRow) = run(std::nullopt);
        res.wrapped = best.has_value();
    }
    if (!best) {
        res.message = activeCursorTime()
                          ? tr("Not found %1 %2")
                                .arg(dir > 0 ? tr("after") : tr("before"), timeText(*activeCursorTime()))
                          : tr("Not found");
    } else {
        res.found = true;
        res.time = static_cast<double>(*best);
        res.row = bestRow;
        moveActiveCursorTo(res.time);
        res.message = tr("Found at %1 in %2")
                          .arg(timeText(res.time),
                               QString::fromStdString(nameOf(rows_[static_cast<std::size_t>(bestRow)])));
        if (res.wrapped)
            res.message += tr(" (search wrapped)");
    }
    if (!skipped.isEmpty())
        res.message += tr("; skipped %1").arg(skipped.join(QStringLiteral("; ")));
    emit navigationMessage(res.message);
    return res;
}

WaveWidget::FindResult WaveWidget::findAgain(int dir) {
    if (!lastFind_) {
        showFindDialog();
        return {};
    }
    const FindSpec spec = *lastFind_;
    return findValue(spec, dir, lastFindWrap_);
}

FindValueDialog* WaveWidget::showFindDialog() {
    if (findDialog_ == nullptr)
        findDialog_ = new FindValueDialog(this);
    findDialog_->refreshSignals();
    findDialog_->show();
    findDialog_->raise();
    findDialog_->activateWindow();
    findDialog_->focusValue();
    return findDialog_;
}

} // namespace lvd::gui
