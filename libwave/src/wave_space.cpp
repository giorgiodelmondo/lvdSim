// SPDX-License-Identifier: GPL-3.0-or-later
#include <lvdwave/wave_commands.hpp>
#include <lvdwave/wave_space.hpp>

#include <cctype>
#include <limits>

namespace lvd::wave {

bool WaveSpace::validDatasetName(std::string_view name) noexcept {
    if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name[0]))))
        return false;
    for (char c : name)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
            return false;
    return true;
}

WaveSpace::Split WaveSpace::split(std::string_view prefixed) noexcept {
    const std::size_t colon = prefixed.find(':');
    if (colon == std::string_view::npos || !validDatasetName(prefixed.substr(0, colon)))
        return {{}, prefixed};
    return {prefixed.substr(0, colon), prefixed.substr(colon + 1)};
}

std::string WaveSpace::join(std::string_view dataset, std::string_view path) {
    if (dataset.empty())
        return std::string(path);
    std::string out(dataset);
    out += ':';
    out += path;
    return out;
}

std::string WaveSpace::addDataset(const std::string& name, std::shared_ptr<const WaveStore> store) {
    if (!validDatasetName(name))
        return "a dataset name starts with a letter and has only letters, digits and '_'";
    if (!store)
        return "no waveform";
    if (current_ && current_->timescaleExponent() != store->timescaleExponent())
        return "the time unit differs from the current waveform's";
    datasets_[name] = std::move(store);
    return {};
}

bool WaveSpace::removeDataset(const std::string& name) {
    return datasets_.erase(name) != 0;
}

std::vector<std::string> WaveSpace::datasetNames() const {
    std::vector<std::string> out;
    for (const auto& [name, store] : datasets_)
        out.push_back(name);
    return out;
}

std::shared_ptr<const WaveStore> WaveSpace::datasetShared(const std::string& name) const {
    const auto it = datasets_.find(name);
    return it == datasets_.end() ? nullptr : it->second;
}

const WaveStore* WaveSpace::store(std::string_view dataset) const {
    if (dataset.empty())
        return current_.get();
    const auto it = datasets_.find(dataset);
    return it == datasets_.end() ? nullptr : it->second.get();
}

const Signal* WaveSpace::findPath(std::string_view prefixedPath) const {
    const Split s = split(prefixedPath);
    const WaveStore* st = store(s.dataset);
    if (st == nullptr)
        return nullptr;
    const SignalId id = st->findSignal(s.path);
    return id == kNoSignal ? nullptr : &st->signal(id);
}

const Signal* WaveSpace::findName(std::string_view name, std::string& error) const {
    const Split s = split(name);
    const WaveStore* st = store(s.dataset);
    if (st == nullptr) {
        error =
            s.dataset.empty() ? "no waveform is loaded" : "no dataset named '" + std::string(s.dataset) + "'";
        return nullptr;
    }
    const std::string path(s.path);
    if (path.empty()) {
        error = "empty signal name";
        return nullptr;
    }
    if (path.find_first_of("*?") != std::string::npos) {
        error = "wildcards are not allowed in '" + std::string(name) + "'";
        return nullptr;
    }
    std::string pattern;
    if (path.find('/') != std::string::npos) {
        pattern = path;
    } else {
        if (path.find('.') != std::string::npos) {
            const SignalId id = st->findSignal(path);
            if (id != kNoSignal)
                return &st->signal(id);
        }
        pattern = "/";
        for (char c : path)
            pattern += c == '.' ? '/' : c;
    }
    const std::vector<std::string> found = expandWavePattern(*st, pattern, false);
    if (found.empty()) {
        error = "unknown signal '" + std::string(name) + "'";
        return nullptr;
    }
    // Several entries may name one signal (a port and the signal it is connected to are aliases).
    const Signal* first = &st->signal(st->findSignal(found.front()));
    for (const std::string& p : found)
        if (&st->signal(st->findSignal(p)) != first) {
            error = "'" + std::string(name) + "' is ambiguous (" + found.front() + ", " + p + ")";
            return nullptr;
        }
    return first;
}

SignalResolver WaveSpace::resolver() const {
    return [this](std::string_view name, std::string& error) { return findName(name, error); };
}

std::vector<std::string> WaveSpace::expandPattern(std::string_view prefixedPattern, bool recursive) const {
    const Split s = split(prefixedPattern);
    const WaveStore* st = store(s.dataset);
    if (st == nullptr)
        return {};
    std::vector<std::string> out = expandWavePattern(*st, std::string(s.path), recursive);
    if (!s.dataset.empty())
        for (std::string& p : out)
            p = join(s.dataset, p);
    return out;
}

Time WaveSpace::endTime() const noexcept {
    Time t = current_ ? current_->endTime() : 0;
    for (const auto& [name, st] : datasets_)
        t = std::max(t, st->endTime());
    return t;
}

Time WaveSpace::endTimeOf(std::string_view prefixedPath) const {
    const WaveStore* st = storeOf(prefixedPath);
    return st == nullptr ? 0 : st->endTime();
}

Result<WaveStore> retimeStore(const WaveStore& src, int exponent) {
    using R = Result<WaveStore>;
    const int diff = src.timescaleExponent() - exponent;
    if (diff < -18 || diff > 18)
        return R::failure("time units too far apart");
    std::uint64_t factor = 1;
    for (int i = 0; i < (diff < 0 ? -diff : diff); ++i)
        factor *= 10;
    const bool finer = diff > 0;
    const Time limit = finer ? std::numeric_limits<Time>::max() / factor : 0;
    bool overflow = false;
    auto conv = [&](Time t) -> Time {
        if (!finer)
            return t / factor;
        if (t > limit) {
            overflow = true;
            return 0;
        }
        return t * factor;
    };

    WaveStore out;
    out.setTimescaleExponent(exponent);
    for (ScopeId id = 1; id < src.scopeCount(); ++id) {
        const Scope& sc = src.scope(id);
        out.addScope(sc.parent, sc.name, sc.type);
    }
    for (SignalId id = 0; id < src.signalCount(); ++id) {
        const Signal& e = src.entry(id);
        if (e.aliasOf() != kNoSignal) {
            if (e.aliasOf() >= id)
                return R::failure("alias defined before its signal");
            out.addAlias(e.scope(), e.name(), e.aliasOf());
            out.setDirection(id, e.direction());
            continue;
        }
        out.addSignal(e.scope(), e.name(), e.kind(), e.width());
        out.setDirection(id, e.direction());
        Signal& d = out.mutableSignal(id);
        for (std::size_t i = 0; i < e.changeCount(); ++i) {
            const Time t = conv(e.time(i));
            switch (e.kind()) {
            case SignalKind::Logic:
                d.appendLogic(t, e.bits(i));
                break;
            case SignalKind::Real:
                d.appendReal(t, e.real(i));
                break;
            case SignalKind::String:
                d.appendText(t, e.text(i));
                break;
            }
        }
    }
    if (src.signalCount() > 0 || src.endTime() > 0) {
        out.extendTimeRange(conv(src.startTime()));
        out.extendTimeRange(conv(src.endTime()));
    }
    if (overflow)
        return R::failure("times do not fit the finer unit");
    return out;
}

} // namespace lvd::wave
