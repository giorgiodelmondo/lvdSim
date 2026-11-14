// SPDX-License-Identifier: GPL-3.0-or-later
#include <lvdwave/list_index.hpp>

#include <algorithm>

namespace lvd::wave {

ListIndex::ListIndex(const WaveStore& store, std::vector<SignalId> columns, ListOptions options)
    : store_(&store)
    , options_(options) {
    std::vector<SignalId> ids;
    if (options_.trigger == ListTrigger::Signal) {
        if (options_.triggerSignal != kNoSignal && options_.triggerSignal < store.signalCount())
            ids.push_back(store.resolve(options_.triggerSignal));
    } else if (options_.trigger == ListTrigger::AnyChange) {
        for (SignalId id : columns)
            if (id < store.signalCount())
                ids.push_back(store.resolve(id));
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    }
    for (SignalId id : ids)
        sources_.push_back(&store.signal(id));
    frontier_.assign(sources_.size(), 0);
    update();
}

ListIndex::ListIndex(const WaveStore& store, std::vector<const Signal*> columns, const Signal* trigger,
                     ListOptions options)
    : store_(&store)
    , options_(options) {
    if (options_.trigger == ListTrigger::Signal) {
        if (trigger != nullptr)
            sources_.push_back(trigger);
    } else if (options_.trigger == ListTrigger::AnyChange) {
        for (const Signal* s : columns)
            if (s != nullptr && std::find(sources_.begin(), sources_.end(), s) == sources_.end())
                sources_.push_back(s);
    }
    frontier_.assign(sources_.size(), 0);
    update();
}

bool ListIndex::nextTime(const std::vector<std::size_t>& pos, Time& t) const {
    bool any = false;
    for (std::size_t i = 0; i < sources_.size(); ++i) {
        const Signal& s = *sources_[i];
        if (pos[i] >= s.changeCount())
            continue;
        const Time ti = s.time(pos[i]);
        if (!any || ti < t)
            t = ti;
        any = true;
    }
    return any;
}

void ListIndex::consume(std::vector<std::size_t>& pos, Time t) const {
    for (std::size_t i = 0; i < sources_.size(); ++i) {
        const Signal& s = *sources_[i];
        while (pos[i] < s.changeCount() && s.time(pos[i]) == t)
            ++pos[i];
    }
}

std::size_t ListIndex::update() {
    if (store_ == nullptr)
        return 0;
    std::uint64_t generation = 0;
    for (const Signal* s : sources_)
        generation += s->generation();
    if (generation != generation_) {
        // A source was cleared (a restarted live run): the rows start again
        rows_ = 0;
        std::fill(frontier_.begin(), frontier_.end(), 0);
        lastTime_ = 0;
        checkpoints_.clear();
        checkpointTimes_.clear();
        cursorValid_ = false;
        generation_ = generation;
    }
    const std::size_t before = rows_;
    if (options_.trigger == ListTrigger::Strobe) {
        const Time end = store_->endTime();
        const Time p = options_.strobePeriod;
        rows_ = (p == 0 || store_->totalChanges() == 0 || end < options_.strobeStart)
                    ? 0
                    : static_cast<std::size_t>((end - options_.strobeStart) / p) + 1;
        return rows_ > before ? rows_ - before : 0;
    }
    Time t = 0;
    while (nextTime(frontier_, t)) {
        if (rows_ > 0 &&
            t == lastTime_) { // more delta cycles of the last row, appended since the last update
            consume(frontier_, t);
            continue;
        }
        if (rows_ % kCheckpointEvery == 0) {
            checkpoints_.insert(checkpoints_.end(), frontier_.begin(), frontier_.end());
            checkpointTimes_.push_back(t);
        }
        consume(frontier_, t);
        lastTime_ = t;
        ++rows_;
    }
    return rows_ - before;
}

void ListIndex::seek(std::size_t row) const {
    const std::size_t cp = row / kCheckpointEvery;
    const bool reuse = cursorValid_ && cursor_.row <= row && cursor_.row / kCheckpointEvery == cp;
    if (!reuse) {
        const std::size_t n = sources_.size();
        cursor_.row = cp * kCheckpointEvery;
        cursor_.pos.assign(checkpoints_.begin() + static_cast<std::ptrdiff_t>(cp * n),
                           checkpoints_.begin() + static_cast<std::ptrdiff_t>((cp + 1) * n));
        cursorValid_ = true;
    }
    Time t = 0;
    while (cursor_.row < row && nextTime(cursor_.pos, t)) {
        consume(cursor_.pos, t);
        ++cursor_.row;
    }
}

Time ListIndex::rowTime(std::size_t row) const {
    if (options_.trigger == ListTrigger::Strobe)
        return options_.strobeStart + static_cast<Time>(row) * options_.strobePeriod;
    if (row >= rows_)
        return lastTime_;
    if (row % kCheckpointEvery == 0)
        return checkpointTimes_[row / kCheckpointEvery];
    seek(row);
    Time t = 0;
    nextTime(cursor_.pos, t);
    return t;
}

std::size_t ListIndex::rowAtOrBefore(Time t) const {
    if (rows_ == 0)
        return kNoIndex;
    if (options_.trigger == ListTrigger::Strobe) {
        if (t < options_.strobeStart)
            return kNoIndex;
        return std::min(rows_ - 1,
                        static_cast<std::size_t>((t - options_.strobeStart) / options_.strobePeriod));
    }
    // The last checkpoint at or before t, then walk its block.
    const auto it = std::upper_bound(checkpointTimes_.begin(), checkpointTimes_.end(), t);
    if (it == checkpointTimes_.begin())
        return kNoIndex;
    const std::size_t cp = static_cast<std::size_t>(it - checkpointTimes_.begin()) - 1;
    std::size_t lo = cp * kCheckpointEvery;
    const std::size_t hi = std::min(rows_, lo + kCheckpointEvery);
    seek(lo);
    std::size_t best = lo;
    Time rt = 0;
    while (cursor_.row < hi && nextTime(cursor_.pos, rt) && rt <= t) {
        best = cursor_.row;
        consume(cursor_.pos, rt);
        ++cursor_.row;
    }
    return best;
}

} // namespace lvd::wave
