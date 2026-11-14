// SPDX-License-Identifier: GPL-3.0-or-later
#include "regression.hpp"

#include "coverage.hpp"
#include "environment.hpp"
#include "text_util.hpp"
#include "translate.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <regex>
#include <thread>

namespace fs = std::filesystem;

namespace lvd::tools::regress {

using text::lower;

namespace {

std::string absolute(const std::string& base, const std::string& path) {
    const fs::path p(path);
    if (p.is_absolute() || base.empty())
        return p.lexically_normal().generic_string();
    return (fs::path(base) / p).lexically_normal().generic_string();
}

std::string safeName(std::string s) {
    for (char& c : s)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-' && c != '.')
            c = '_';
    return s;
}

std::optional<std::string> libraryDir(const PlanOptions& o, const std::string& name) {
    for (const auto& [n, d] : o.libraries)
        if (lower(n) == lower(name))
            return d;
    return std::nullopt;
}

// NAME=VALUE as nvc's -g wants it: a path /top/inst/NAME (the form vsim accepts) becomes inst.NAME.
std::string nvcGeneric(const std::string& g) {
    const std::size_t eq = g.find('=');
    std::string name = g.substr(0, eq);
    const std::string value = eq == std::string::npos ? std::string() : g.substr(eq);
    if (name.find('/') != std::string::npos) {
        if (name.front() == '/') {
            const std::size_t second = name.find('/', 1);
            name = second == std::string::npos ? name.substr(1) : name.substr(second + 1);
        }
        std::replace(name.begin(), name.end(), '/', '.');
    }
    return name + value;
}

bool selected(const PlanOptions& o, const std::string& test, const std::string& id) {
    if (o.only.empty())
        return true;
    return std::find(o.only.begin(), o.only.end(), test) != o.only.end() ||
           std::find(o.only.begin(), o.only.end(), id) != o.only.end();
}

} // namespace

PlanResult planJobs(const proj::Project& p, const PlanOptions& o) {
    PlanResult r;
    const std::string outDir = !o.outDir.empty() ? o.outDir
                                                 : absolute(o.projectDir, p.settings.regressionDir.empty()
                                                                              ? "lvdsim_regression"
                                                                              : p.settings.regressionDir);
    for (const std::string& name : o.only) {
        bool known = false;
        for (const proj::Test& t : p.tests) {
            known = known || t.name == name;
            for (const std::uint64_t s : t.seeds)
                known = known || name == t.name + "@" + std::to_string(s);
        }
        if (!known)
            r.errors.push_back("no test '" + name + "' in the project");
    }

    for (std::size_t ti = 0; ti < p.tests.size(); ++ti) {
        const proj::Test& t = p.tests[ti];
        // Top levels: all from one library (nvc elaborates them from the work library).
        std::vector<DesignUnitName> tops;
        for (const std::string& w : text::splitWords(t.top))
            tops.push_back(parseDesignUnit(w));
        if (tops.empty()) {
            r.errors.push_back("test '" + t.name + "' has no top level");
            continue;
        }
        // Without a library: work if the project has it, else the last library (usually the test bench's).
        std::string lib = tops.front().library;
        if (lib.empty())
            lib = (p.findLibrary("work") != nullptr || p.libraries.empty()) ? std::string("work")
                                                                            : p.libraries.back().name;
        bool sameLib = true;
        for (const DesignUnitName& u : tops)
            sameLib = sameLib && (u.library.empty() || lower(u.library) == lower(lib));
        if (!sameLib) {
            r.errors.push_back("test '" + t.name + "': all top levels must be in one library");
            continue;
        }
        const auto workDir = libraryDir(o, lib);
        if (!workDir) {
            r.errors.push_back("test '" + t.name + "': library '" + lib + "' is not known");
            continue;
        }

        std::vector<std::optional<std::uint64_t>> seeds;
        if (t.seeds.empty())
            seeds.emplace_back(std::nullopt);
        for (const std::uint64_t s : t.seeds)
            seeds.emplace_back(s);

        for (const auto& seed : seeds) {
            Job j;
            j.test = t.name;
            j.testIndex = static_cast<int>(ti);
            j.seed = seed;
            j.id = seed ? t.name + "@" + std::to_string(*seed) : t.name;
            if (!selected(o, t.name, j.id))
                continue;
            j.top = t.top;
            j.dir = outDir + "/" + safeName(seed ? t.name + "_s" + std::to_string(*seed) : t.name);
            j.logFile = j.dir + "/run.log";
            j.timeoutSec = o.timeoutSec > 0   ? o.timeoutSec
                           : t.timeoutSec > 0 ? t.timeoutSec
                                              : (p.settings.timeoutSec > 0 ? p.settings.timeoutSec : 600);
            j.expect = t.expect;
            const bool wave = o.wave ? *o.wave : (t.wave ? *t.wave : p.settings.wave);
            const bool cover = o.coverage ? *o.coverage : (t.coverage ? *t.coverage : p.settings.coverage);
            if (wave)
                j.waveFile = j.dir + "/wave.fst";
            if (cover)
                j.coverFile = j.dir + "/coverage.ncdb";

            std::vector<std::string>& a = j.nvcArgs;
            a = o.nvcGlobalArgs;
            a.push_back("--std=" + (p.settings.vhdl.empty() ? std::string("08") : p.settings.vhdl));
            a.push_back("--work=" + lib + ":" + *workDir);
            for (const auto& [n, d] : o.libraries)
                if (lower(n) != lower(lib) && lower(n) != "work") // work always names the top's library
                    a.push_back("--map=" + n + ":" + d);
            if (seed)
                a.push_back("--seed=" + std::to_string(*seed));
            const bool script = !t.doScript.empty() || !t.commands.empty();
            // nvc hands plus arguments to VHPI and VPI plug-ins: after -r for a plain run, among the global
            // options for a scripted one (--do would read them as a design unit).
            if (script)
                a.insert(a.end(), t.plusargs.begin(), t.plusargs.end());
            a.push_back("-e");
            a.push_back("--jit");
            a.push_back("--no-save");
            if (cover) {
                const std::vector<std::string> c =
                    cov::elaborationOptions(cov::parseKinds(p.settings.coverageKinds), j.coverFile);
                a.insert(a.end(), c.begin(), c.end());
            }
            for (const std::string& g : t.generics)
                a.push_back("-g" + nvcGeneric(g));
            if (seed && !t.seedGeneric.empty())
                a.push_back("-g" + t.seedGeneric + "=" + std::to_string(*seed));
            for (const DesignUnitName& u : tops)
                a.push_back(u.unit);

            std::vector<std::string> waveArgs;
            if (wave) {
                waveArgs.push_back("--format=fst");
                waveArgs.push_back("--wave=" + j.waveFile);
            }
            if (script) {
                a.push_back("--do");
                a.insert(a.end(), waveArgs.begin(), waveArgs.end());
                if (!o.compatTcl.empty())
                    a.push_back(o.compatTcl);
                if (!t.doScript.empty()) {
                    a.push_back(absolute(o.projectDir, t.doScript));
                } else {
                    j.scriptFile = j.dir + "/commands.do";
                    j.scriptContents = t.commands + "\n";
                    a.push_back(j.scriptFile);
                }
            } else {
                a.push_back("-r");
                if (!t.stopTime.empty()) {
                    std::string st = t.stopTime;
                    st.erase(std::remove(st.begin(), st.end(), ' '), st.end());
                    a.push_back("--stop-time=" + st);
                }
                a.insert(a.end(), waveArgs.begin(), waveArgs.end());
                a.insert(a.end(), t.plusargs.begin(), t.plusargs.end());
            }
            r.jobs.push_back(std::move(j));
        }
    }
    return r;
}

const char* statusName(Status s) {
    switch (s) {
    case Status::Pending:
        return "pending";
    case Status::Running:
        return "running";
    case Status::Passed:
        return "passed";
    case Status::Failed:
        return "failed";
    case Status::TimedOut:
        return "timeout";
    case Status::NotStarted:
        return "error";
    case Status::Cancelled:
        return "cancelled";
    }
    return "";
}

bool isDone(Status s) {
    return s != Status::Pending && s != Status::Running;
}

Outcome evaluate(const Job& job, bool started, bool timedOut, int exitCode, std::string_view log,
                 double seconds, const std::string& startError) {
    Outcome o;
    o.exitCode = exitCode;
    o.seconds = seconds;
    o.counts = diag::count(log);
    for (const diag::Diagnostic& d : diag::parse(log))
        if (d.severity >= diag::Severity::Error) {
            o.firstProblem = d.message;
            break;
        }
    if (!started) {
        o.status = Status::NotStarted;
        o.message = startError.empty() ? std::string("could not start") : startError;
        return o;
    }
    if (timedOut) {
        o.status = Status::TimedOut;
        o.message = "timed out after " + std::to_string(job.timeoutSec) + " s";
        return o;
    }
    std::vector<std::string> why;
    if (exitCode != 0)
        why.push_back("exit code " + std::to_string(exitCode));
    auto plural = [](int n, const char* word) {
        return std::to_string(n) + " " + word + (n == 1 ? "" : "s");
    };
    if (o.counts.errors > 0)
        why.push_back(plural(o.counts.errors, "error"));
    if (o.counts.failures > 0)
        why.push_back(plural(o.counts.failures, "failure"));
    if (o.counts.fatals > 0)
        why.push_back(plural(o.counts.fatals, "fatal"));
    if (!job.expect.empty()) {
        bool invalid = false;
        try {
            const std::regex re(job.expect, std::regex::ECMAScript);
            o.expectFound = std::regex_search(log.begin(), log.end(), re);
        } catch (const std::regex_error&) {
            o.expectFound = false;
            invalid = true;
        }
        if (!o.expectFound)
            why.push_back(invalid ? "invalid expect pattern" : "expected output not found");
    }
    if (why.empty()) {
        o.status = Status::Passed;
        o.message = "passed";
    } else {
        o.status = Status::Failed;
        for (const std::string& w : why)
            o.message += (o.message.empty() ? "" : ", ") + w;
    }
    return o;
}

namespace {

std::string xmlEscape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        switch (ch) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '\'':
            out += "&apos;";
            break;
        default:
            if (c < 0x20 && ch != '\n' && ch != '\t' && ch != '\r')
                out += ' '; // not allowed in XML 1.0
            else
                out += ch;
        }
    }
    return out;
}

std::string seconds3(double s) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3f", s);
    return buf;
}

std::string tail(const std::string& text, std::size_t maxLines) {
    std::size_t pos = text.size();
    std::size_t lines = 0;
    while (pos > 0 && lines <= maxLines) {
        pos = text.rfind('\n', pos - 1);
        if (pos == std::string::npos) {
            pos = 0;
            break;
        }
        ++lines;
    }
    return pos == 0 ? text : text.substr(pos + 1);
}

} // namespace

std::string junitXml(const std::string& suiteName, const std::vector<Job>& jobs,
                     const std::vector<Outcome>& outcomes, const std::string& timestamp) {
    int failures = 0, errors = 0, skipped = 0;
    double total = 0;
    for (std::size_t i = 0; i < jobs.size() && i < outcomes.size(); ++i) {
        const Status s = outcomes[i].status;
        failures += s == Status::Failed ? 1 : 0;
        errors += (s == Status::TimedOut || s == Status::NotStarted) ? 1 : 0;
        skipped += (s == Status::Cancelled || s == Status::Pending || s == Status::Running) ? 1 : 0;
        total += outcomes[i].seconds;
    }
    const std::string counts = " tests=\"" + std::to_string(jobs.size()) + "\" failures=\"" +
                               std::to_string(failures) + "\" errors=\"" + std::to_string(errors) +
                               "\" skipped=\"" + std::to_string(skipped) + "\" time=\"" + seconds3(total) +
                               "\"";
    const std::string suite = xmlEscape(suiteName.empty() ? std::string("lvdsim") : suiteName);
    std::string x = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    x += "<testsuites name=\"" + suite + "\"" + counts + ">\n";
    x += "  <testsuite name=\"" + suite + "\"" + counts;
    if (!timestamp.empty())
        x += " timestamp=\"" + xmlEscape(timestamp) + "\"";
    x += ">\n";
    for (std::size_t i = 0; i < jobs.size() && i < outcomes.size(); ++i) {
        const Job& j = jobs[i];
        const Outcome& o = outcomes[i];
        x += "    <testcase name=\"" + xmlEscape(j.id) + "\" classname=\"" + suite + "." + xmlEscape(j.test) +
             "\" time=\"" + seconds3(o.seconds) + "\"";
        if (o.status == Status::Passed) {
            x += "/>\n";
            continue;
        }
        x += ">\n";
        std::string msg = o.message;
        if (!o.firstProblem.empty())
            msg += ": " + o.firstProblem;
        switch (o.status) {
        case Status::Failed:
            x += "      <failure type=\"failure\" message=\"" + xmlEscape(msg) + "\"/>\n";
            break;
        case Status::TimedOut:
            x += "      <error type=\"timeout\" message=\"" + xmlEscape(msg) + "\"/>\n";
            break;
        case Status::NotStarted:
            x += "      <error type=\"not-started\" message=\"" + xmlEscape(msg) + "\"/>\n";
            break;
        default:
            x += "      <skipped message=\"" + xmlEscape(std::string(statusName(o.status))) + "\"/>\n";
            break;
        }
        if (o.status == Status::Failed || o.status == Status::TimedOut) {
            const std::string log = readLog(j.logFile);
            if (!log.empty())
                x += "      <system-out>" + xmlEscape(tail(log, 200)) + "</system-out>\n";
        }
        x += "    </testcase>\n";
    }
    x += "  </testsuite>\n</testsuites>\n";
    return x;
}

std::string writeResults(const std::vector<Job>& jobs, const std::vector<Outcome>& outcomes) {
    std::string out;
    for (std::size_t i = 0; i < jobs.size() && i < outcomes.size(); ++i) {
        std::string msg = outcomes[i].message;
        std::replace(msg.begin(), msg.end(), '\t', ' ');
        std::replace(msg.begin(), msg.end(), '\n', ' ');
        out += jobs[i].id + '\t' + statusName(outcomes[i].status) + '\t' + seconds3(outcomes[i].seconds) +
               '\t' + msg + '\n';
    }
    return out;
}

std::vector<std::string> failedIds(std::string_view text) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t nl = text.find('\n', pos);
        const std::string_view line =
            text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = nl == std::string_view::npos ? text.size() : nl + 1;
        const std::size_t t1 = line.find('\t');
        if (t1 == std::string_view::npos)
            continue;
        const std::size_t t2 = line.find('\t', t1 + 1);
        const std::string_view status =
            line.substr(t1 + 1, t2 == std::string_view::npos ? std::string_view::npos : t2 - t1 - 1);
        if (status != "passed")
            out.emplace_back(line.substr(0, t1));
    }
    return out;
}

std::vector<std::string> coverageDatabases(const std::vector<Job>& jobs) {
    std::vector<std::string> out;
    for (const Job& j : jobs) {
        std::error_code ec;
        if (!j.coverFile.empty() && fs::is_regular_file(j.coverFile, ec))
            out.push_back(j.coverFile);
    }
    return out;
}

std::string readLog(const std::string& path) {
    return readTextFile(path).value_or(std::string());
}

// ---------------------------------------------------------------------------------------------
// Runner

struct Runner::Active {
    std::size_t index = 0;
    ChildProcess child;
    std::chrono::steady_clock::time_point start;
};

Runner::Runner(std::vector<Job> jobs, RunnerConfig config)
    : jobs_(std::move(jobs))
    , config_(std::move(config)) {
    outcomes_.resize(jobs_.size());
    if (config_.jobs < 1)
        config_.jobs = 1;
    bool haveThreads = false;
    for (const auto& [k, v] : config_.process.env)
        haveThreads = haveThreads || k == "NVC_MAX_THREADS";
    if (!haveThreads)
        config_.process.env.emplace_back("NVC_MAX_THREADS", "1");
}

Runner::~Runner() {
    onStarted = nullptr;
    onFinished = nullptr;
    cancel();
}

int Runner::running() const {
    return static_cast<int>(active_.size());
}

bool Runner::finished() const {
    return active_.empty() && next_ >= jobs_.size();
}

void Runner::startJob(std::size_t i) {
    const Job& j = jobs_[i];
    std::error_code ec;
    fs::create_directories(j.dir, ec);
    for (const std::string& f : {j.waveFile, j.coverFile})
        if (!f.empty())
            fs::remove(f, ec);
    if (!j.scriptFile.empty()) {
        if (FILE* f = std::fopen(j.scriptFile.c_str(), "wb")) {
            std::fwrite(j.scriptContents.data(), 1, j.scriptContents.size(), f);
            std::fclose(f);
        }
    }
    auto a = std::make_unique<Active>();
    a->index = i;
    a->start = std::chrono::steady_clock::now();
    std::vector<std::string> argv{config_.nvc};
    argv.insert(argv.end(), j.nvcArgs.begin(), j.nvcArgs.end());
    std::string error;
    outcomes_[i].status = Status::Running;
    // Temporary files (the FST writer makes several) go to the run's own directory: parallel runs do not
    // compete for names in the system temporary directory, and what a killed run leaves stays with it.
    ProcessOptions po = config_.process;
    for (const char* var : {"TMP", "TEMP", "TMPDIR"})
        po.env.emplace_back(var, j.dir);
    if (!a->child.start(argv, po, j.logFile, &error)) {
        outcomes_[i] = evaluate(j, false, false, -1, {}, 0, error);
        if (onFinished)
            onFinished(static_cast<int>(i));
        return;
    }
    active_.push_back(std::move(a));
    if (onStarted)
        onStarted(static_cast<int>(i));
}

void Runner::finishJob(std::size_t k, bool timedOut) {
    Active& a = *active_[k];
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - a.start).count();
    const std::size_t i = a.index;
    const int code = a.child.exitCode();
    active_.erase(active_.begin() + static_cast<std::ptrdiff_t>(k));
    outcomes_[i] = evaluate(jobs_[i], true, timedOut, code, readLog(jobs_[i].logFile), secs);
    if (onFinished)
        onFinished(static_cast<int>(i));
}

void Runner::step() {
    for (std::size_t k = 0; k < active_.size();) {
        Active& a = *active_[k];
        if (a.child.poll()) {
            finishJob(k, false);
            continue;
        }
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - a.start).count();
        if (secs > jobs_[a.index].timeoutSec) {
            a.child.kill();
            finishJob(k, true);
            continue;
        }
        ++k;
    }
    while (static_cast<int>(active_.size()) < config_.jobs && next_ < jobs_.size())
        startJob(next_++);
}

void Runner::cancel() {
    for (auto& a : active_) {
        a->child.kill();
        outcomes_[a->index].status = Status::Cancelled;
        outcomes_[a->index].message = "cancelled";
        if (onFinished)
            onFinished(static_cast<int>(a->index));
    }
    active_.clear();
    for (; next_ < jobs_.size(); ++next_) {
        outcomes_[next_].status = Status::Cancelled;
        outcomes_[next_].message = "cancelled";
    }
}

void Runner::runToEnd(int pollMs) {
    step();
    while (!finished()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(pollMs));
        step();
    }
}

} // namespace lvd::tools::regress
