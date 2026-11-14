// SPDX-License-Identifier: GPL-3.0-or-later
#include <lvdwave/compare.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <set>
#include <utility>

namespace lvd::wave {

namespace {

Logic canonical(Logic v) noexcept {
    return v == Logic::L ? Logic::L0 : v == Logic::H ? Logic::L1 : v;
}

} // namespace

bool sameValue(const Signal& a, std::size_t ia, const Signal& b, std::size_t ib, double realTolerance) {
    if (ia == kNoIndex || ib == kNoIndex)
        return ia == ib;
    if (a.kind() != b.kind())
        return false;
    switch (a.kind()) {
    case SignalKind::Logic: {
        const std::uint32_t w = std::max(a.width(), b.width());
        for (std::uint32_t i = 0; i < w; ++i) {
            const Logic x = i < a.width() ? canonical(a.bit(ia, i)) : Logic::L0;
            const Logic y = i < b.width() ? canonical(b.bit(ib, i)) : Logic::L0;
            if (x != y)
                return false;
        }
        return true;
    }
    case SignalKind::Real: {
        const double x = a.real(ia), y = b.real(ib);
        if (std::isnan(x) || std::isnan(y))
            return std::isnan(x) && std::isnan(y);
        return std::fabs(x - y) <= realTolerance;
    }
    case SignalKind::String:
        return a.text(ia) == b.text(ib);
    }
    return false;
}

SignalCompare::SignalCompare(const Signal& ref, const Signal& test, CompareOptions options, std::string name)
    : ref_(ref)
    , test_(test)
    , options_(options)
    , out_(std::move(name), kRootScope, SignalKind::Logic, 1) {}

std::optional<Time> SignalCompare::openSince() const noexcept {
    if (differ_ && emitted_)
        return open_;
    return std::nullopt;
}

void SignalCompare::step(Time t) {
    const bool same = sameValue(ref_, curRef_, test_, curTest_, options_.realTolerance);
    const Time tol = options_.tolerance;
    if (!started_) {
        started_ = true;
        if (same || tol > 0)
            out_.appendLogic(t, "0");
    }
    const bool again = lastStep_ && *lastStep_ == t;
    lastStep_ = t;
    if (!again)
        lastClose_.reset();
    if (again && lastClose_ && !same && !differ_) {
        // more delta cycles of a time step already compared: the difference that seemed to end here goes on
        differ_ = true;
        open_ = lastClose_->open;
        if (lastClose_->recorded)
            intervals_.pop_back();
        emitted_ = lastClose_->level;
        if (emitted_)
            out_.appendLogic(t, "1");
        lastClose_.reset();
        return;
    }
    if (!same && !differ_) {
        differ_ = true;
        open_ = t;
        emitted_ = false;
        if (tol == 0) {
            out_.appendLogic(t, "1");
            emitted_ = true;
        }
    } else if (same && differ_) {
        differ_ = false;
        const Time len = t - open_;
        Close c;
        c.open = open_;
        c.level = emitted_;
        if (len == 0) {
            // later delta cycles of the same time step made the values equal again: no difference
            if (emitted_)
                out_.appendLogic(t, "0");
        } else if (tol == 0 || len > tol) {
            if (!emitted_)
                out_.appendLogic(open_, "1");
            out_.appendLogic(t, "0");
            intervals_.push_back({open_, t});
            c.level = true;
            c.recorded = true;
        }
        lastClose_ = c;
        emitted_ = false;
    }
}

std::size_t SignalCompare::update(Time until) {
    const std::uint64_t generation = std::uint64_t{ref_.generation()} + test_.generation();
    if (generation != generation_) {
        // A signal was cleared (a restarted live run): compare from the start again
        out_.clearChanges();
        intervals_.clear();
        nextRef_ = nextTest_ = 0;
        curRef_ = curTest_ = kNoIndex;
        started_ = differ_ = emitted_ = false;
        open_ = 0;
        lastClose_.reset();
        lastStep_.reset();
        generation_ = generation;
    }
    const std::size_t before = count();
    while (true) {
        bool any = false;
        Time t = 0;
        if (nextRef_ < ref_.changeCount() && ref_.time(nextRef_) <= until) {
            t = ref_.time(nextRef_);
            any = true;
        }
        if (nextTest_ < test_.changeCount() && test_.time(nextTest_) <= until) {
            const Time tt = test_.time(nextTest_);
            if (!any || tt < t)
                t = tt;
            any = true;
        }
        if (!any)
            break;
        bool moved = false;
        while (nextRef_ < ref_.changeCount() && ref_.time(nextRef_) == t) {
            ++nextRef_;
            moved = true;
        }
        if (moved)
            curRef_ = nextRef_ - 1;
        moved = false;
        while (nextTest_ < test_.changeCount() && test_.time(nextTest_) == t) {
            ++nextTest_;
            moved = true;
        }
        if (moved)
            curTest_ = nextTest_ - 1;
        step(t);
    }
    if (differ_ && !emitted_ && until >= open_ && until - open_ > options_.tolerance) {
        out_.appendLogic(open_, "1");
        emitted_ = true;
    }
    const std::size_t after = count();
    return after > before ? after - before : 0;
}

std::optional<Time> findDifference(const SignalCompare& cmp, std::optional<Time> from, SearchDirection dir) {
    return findEdge(SearchTarget{&cmp.differences(), -1}, from, dir, EdgeKind::Rising);
}

StoreDiff compareStores(const WaveStore& ref, const WaveStore& test, const CompareOptions& options,
                        std::string_view scope) {
    StoreDiff r;
    r.until = std::min(ref.endTime(), test.endTime());
    auto inScope = [&](const std::string& path) {
        if (scope.empty())
            return true;
        return path.size() >= scope.size() && path.compare(0, scope.size(), scope) == 0 &&
               (path.size() == scope.size() || path[scope.size()] == '.');
    };
    std::set<std::pair<const Signal*, const Signal*>> seen;
    for (SignalId id = 0; id < test.signalCount(); ++id) {
        const std::string path = test.pathOf(id);
        if (!inScope(path))
            continue;
        const SignalId rid = ref.findSignal(path);
        if (rid == kNoSignal) {
            r.onlyInTest.push_back(path);
            continue;
        }
        const Signal& a = ref.signal(rid);
        const Signal& b = test.signal(id);
        if (!seen.insert({&a, &b}).second)
            continue;
        if (a.kind() != b.kind()) {
            r.kindMismatch.push_back(path);
            continue;
        }
        ++r.compared;
        SignalCompare c(a, b, options);
        c.update(r.until);
        if (c.count() > 0) {
            SignalDiff d;
            d.path = path;
            d.count = c.count();
            d.first = !c.intervals().empty() ? c.intervals().front().start : *c.openSince();
            r.total += d.count;
            r.differing.push_back(std::move(d));
        }
    }
    for (SignalId id = 0; id < ref.signalCount(); ++id) {
        const std::string path = ref.pathOf(id);
        if (inScope(path) && test.findSignal(path) == kNoSignal)
            r.onlyInRef.push_back(path);
    }
    return r;
}

std::optional<Time> parseDuration(std::string_view text, int exponent) {
    std::string s(text);
    s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c) != 0; }),
            s.end());
    if (s.empty())
        return std::nullopt;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || !(v >= 0.0) || !std::isfinite(v))
        return std::nullopt;
    std::string unit(end);
    for (char& c : unit)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    int unitExp = exponent;
    if (unit == "fs")
        unitExp = -15;
    else if (unit == "ps")
        unitExp = -12;
    else if (unit == "ns")
        unitExp = -9;
    else if (unit == "us")
        unitExp = -6;
    else if (unit == "ms")
        unitExp = -3;
    else if (unit == "s" || unit == "sec")
        unitExp = 0;
    else if (!unit.empty())
        return std::nullopt;
    const double units = v * std::pow(10.0, unitExp - exponent);
    if (units > 9.0e18)
        return std::nullopt;
    return static_cast<Time>(std::llround(units));
}

} // namespace lvd::wave
