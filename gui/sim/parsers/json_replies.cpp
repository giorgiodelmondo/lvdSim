// SPDX-License-Identifier: GPL-3.0-or-later
#include "sim/parsers/json_replies.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

#include <cmath>

namespace lvd::gui {

namespace {

using std::string;

string text(const QString& s) {
    return s.toStdString();
}

// A JSON number that must be a whole number; times are integers in femtoseconds.
std::optional<qint64> integer(const QJsonObject& o, const char* key) {
    const QJsonValue v = o.value(QLatin1String(key));
    if (!v.isDouble())
        return std::nullopt;
    const double d = v.toDouble();
    if (!std::isfinite(d) || d != std::floor(d) || std::fabs(d) > 9.007199254740992e15)
        return std::nullopt;
    return static_cast<qint64>(d);
}

// A positive whole number given as a JSON number or as a string of digits (the kernel prints the values of
// `describe` as strings). 0 when missing or not a positive number.
int lineNumber(const QJsonObject& o, const char* key) {
    const QJsonValue v = o.value(QLatin1String(key));
    if (v.isString()) {
        bool ok = false;
        const int n = v.toString().toInt(&ok);
        return ok && n > 0 ? n : 0;
    }
    const auto n = integer(o, key);
    return n && *n > 0 && *n < (1 << 30) ? static_cast<int>(*n) : 0;
}

lvd::Result<BreakpointInfo> breakpointFrom(const QJsonObject& o) {
    const auto id = integer(o, "id");
    const auto time = integer(o, "time");
    if (!id || !time)
        return lvd::Result<BreakpointInfo>::failure("breakpoint without \"id\" or \"time\"");
    BreakpointInfo b;
    b.id = static_cast<int>(*id);
    b.timeFs = *time;
    return b;
}

// Source line breakpoints have "file" and "line" instead of "time".
bool isLineBreakpoint(const QJsonObject& o) {
    return !o.contains(QLatin1String("time")) && o.contains(QLatin1String("line"));
}

lvd::Result<LineBreakpointInfo> lineBreakpointFrom(const QJsonObject& o) {
    const auto id = integer(o, "id");
    const auto line = integer(o, "line");
    if (!id || !line || *line <= 0 || !o.value(QLatin1String("file")).isString())
        return lvd::Result<LineBreakpointInfo>::failure("breakpoint without \"id\", \"file\" or \"line\"");
    LineBreakpointInfo b;
    b.id = static_cast<int>(*id);
    b.file = o.value(QLatin1String("file")).toString();
    b.line = static_cast<int>(*line);
    b.hits = static_cast<int>(integer(o, "hits").value_or(0));
    return b;
}

lvd::Result<SourceFrame> frameFrom(const QJsonObject& o) {
    const auto level = integer(o, "level");
    if (!level || !o.value(QLatin1String("kind")).isString() || !o.value(QLatin1String("name")).isString())
        return lvd::Result<SourceFrame>::failure("frame without \"level\", \"kind\" or \"name\"");
    SourceFrame f;
    f.level = static_cast<int>(*level);
    f.kind = o.value(QLatin1String("kind")).toString();
    f.name = o.value(QLatin1String("name")).toString();
    f.symbol = o.value(QLatin1String("symbol")).toString();
    f.file = o.value(QLatin1String("file")).toString(); // null gives an empty string
    f.line = static_cast<int>(integer(o, "line").value_or(0));
    return f;
}

} // namespace

lvd::Result<QJsonObject> parseReply(const QString& line) {
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError)
        return lvd::Result<QJsonObject>::failure("not valid JSON: " + text(err.errorString()));
    if (!doc.isObject())
        return lvd::Result<QJsonObject>::failure("reply is not a JSON object");
    const QJsonObject o = doc.object();
    const QJsonValue v = o.value(QLatin1String("v"));
    if (!v.isDouble())
        return lvd::Result<QJsonObject>::failure("reply has no \"v\" member");
    if (v.toInt(-1) != kSchemaVersion)
        return lvd::Result<QJsonObject>::failure("unknown schema version " + std::to_string(v.toInt(-1)));
    return o;
}

lvd::Result<RunReply> parseRunReply(const QString& line) {
    auto r = parseReply(line);
    if (!r.ok())
        return lvd::Result<RunReply>::failure(r.error());
    const QJsonObject& o = r.value();
    RunReply out;
    const auto now = integer(o, "now");
    const auto delta = integer(o, "delta");
    if (!now || !delta)
        return lvd::Result<RunReply>::failure("run reply without \"now\" or \"delta\"");
    out.nowFs = *now;
    out.delta = *delta;
    const QString status = o.value(QLatin1String("status")).toString();
    if (status == QLatin1String("paused"))
        out.status = RunStatus::Paused;
    else if (status == QLatin1String("finished"))
        out.status = RunStatus::Finished;
    else if (status == QLatin1String("breakpoint"))
        out.status = RunStatus::Breakpoint;
    else if (status == QLatin1String("interrupted"))
        out.status = RunStatus::Interrupted;
    else
        return lvd::Result<RunReply>::failure("unknown run status \"" + text(status) + "\"");
    if (out.status == RunStatus::Breakpoint) {
        auto b = breakpointFrom(o.value(QLatin1String("breakpoint")).toObject());
        if (!b.ok())
            return lvd::Result<RunReply>::failure(b.error());
        out.breakpoint = b.value();
    }
    return out;
}

lvd::Result<QVector<ProcessInfo>> parseProcessesReply(const QString& line) {
    using R = lvd::Result<QVector<ProcessInfo>>;
    auto r = parseReply(line);
    if (!r.ok())
        return R::failure(r.error());
    const QJsonValue list = r.value().value(QLatin1String("processes"));
    if (!list.isArray())
        return R::failure("reply has no \"processes\" array");
    QVector<ProcessInfo> out;
    for (const QJsonValue& v : list.toArray()) {
        const QJsonObject o = v.toObject();
        if (!o.value(QLatin1String("name")).isString())
            return R::failure("process without \"name\"");
        ProcessInfo p;
        p.name = o.value(QLatin1String("name")).toString();
        p.path = o.value(QLatin1String("path")).toString();
        p.state = o.value(QLatin1String("state")).toString();
        p.file = o.value(QLatin1String("file")).toString(); // null gives an empty string
        p.line = o.value(QLatin1String("line")).toInt(0);
        out.push_back(p);
    }
    return out;
}

lvd::Result<QVector<BreakpointInfo>> parseBreakpointList(const QString& line) {
    using R = lvd::Result<QVector<BreakpointInfo>>;
    auto r = parseReply(line);
    if (!r.ok())
        return R::failure(r.error());
    const QJsonValue list = r.value().value(QLatin1String("breakpoints"));
    if (!list.isArray())
        return R::failure("reply has no \"breakpoints\" array");
    QVector<BreakpointInfo> out;
    for (const QJsonValue& v : list.toArray()) {
        if (isLineBreakpoint(v.toObject()))
            continue; // parseLineBreakpointList reads these
        auto b = breakpointFrom(v.toObject());
        if (!b.ok())
            return R::failure(b.error());
        out.push_back(b.value());
    }
    return out;
}

lvd::Result<QVector<LineBreakpointInfo>> parseLineBreakpointList(const QString& line) {
    using R = lvd::Result<QVector<LineBreakpointInfo>>;
    auto r = parseReply(line);
    if (!r.ok())
        return R::failure(r.error());
    const QJsonValue list = r.value().value(QLatin1String("breakpoints"));
    if (!list.isArray())
        return R::failure("reply has no \"breakpoints\" array");
    QVector<LineBreakpointInfo> out;
    for (const QJsonValue& v : list.toArray()) {
        if (!isLineBreakpoint(v.toObject()))
            continue; // parseBreakpointList reads these
        auto b = lineBreakpointFrom(v.toObject());
        if (!b.ok())
            return R::failure(b.error());
        out.push_back(b.value());
    }
    return out;
}

lvd::Result<LineBreakpointInfo> parseLineBreakpointAdded(const QString& line) {
    auto r = parseReply(line);
    if (!r.ok())
        return lvd::Result<LineBreakpointInfo>::failure(r.error());
    return lineBreakpointFrom(r.value());
}

PortMode portModeFromString(const QString& text) {
    const QString t = text.trimmed().toLower();
    if (t == QLatin1String("in"))
        return PortMode::In;
    if (t == QLatin1String("out"))
        return PortMode::Out;
    if (t == QLatin1String("inout"))
        return PortMode::InOut;
    if (t == QLatin1String("buffer"))
        return PortMode::Buffer;
    if (t == QLatin1String("internal"))
        return PortMode::Internal;
    return PortMode::Unknown;
}

QString portModeName(PortMode mode) {
    switch (mode) {
    case PortMode::In:
        return QStringLiteral("in");
    case PortMode::Out:
        return QStringLiteral("out");
    case PortMode::InOut:
        return QStringLiteral("inout");
    case PortMode::Buffer:
        return QStringLiteral("buffer");
    case PortMode::Internal:
        return QStringLiteral("internal");
    case PortMode::Unknown:
        break;
    }
    return {};
}

lvd::Result<QVector<DescribedObject>> parseDescribeReply(const QString& line) {
    using R = lvd::Result<QVector<DescribedObject>>;
    auto r = parseReply(line);
    if (!r.ok())
        return R::failure(r.error());
    const QJsonValue list = r.value().value(QLatin1String("objects"));
    if (!list.isArray())
        return R::failure("reply has no \"objects\" array");
    QVector<DescribedObject> out;
    for (const QJsonValue& v : list.toArray()) {
        const QJsonObject o = v.toObject();
        if (!o.value(QLatin1String("path")).isString())
            return R::failure("object without \"path\"");
        DescribedObject d;
        d.name = o.value(QLatin1String("name")).toString();
        d.path = o.value(QLatin1String("path")).toString();
        d.kind = o.value(QLatin1String("kind")).toString();
        d.type = o.value(QLatin1String("type")).toString();
        d.mode = portModeFromString(o.value(QLatin1String("mode")).toString());
        d.file = o.value(QLatin1String("file")).toString();
        d.line = lineNumber(o, "line");
        d.column = lineNumber(o, "column");
        d.unit = o.value(QLatin1String("unit")).toString();
        d.unitFile = o.value(QLatin1String("unit_file")).toString();
        d.unitLine = lineNumber(o, "unit_line");
        d.instFile = o.value(QLatin1String("inst_file")).toString();
        d.instLine = lineNumber(o, "inst_line");
        out.push_back(d);
    }
    return out;
}

lvd::Result<BreakpointInfo> parseBreakpointAdded(const QString& line) {
    auto r = parseReply(line);
    if (!r.ok())
        return lvd::Result<BreakpointInfo>::failure(r.error());
    return breakpointFrom(r.value());
}

lvd::Result<int> parseDeleted(const QString& line) {
    auto r = parseReply(line);
    if (!r.ok())
        return lvd::Result<int>::failure(r.error());
    const auto n = integer(r.value(), "deleted");
    if (!n)
        return lvd::Result<int>::failure("reply has no \"deleted\" member");
    return static_cast<int>(*n);
}

bool isStopReply(const QString& line) {
    auto r = parseReply(line);
    return r.ok() && r.value().value(QLatin1String("status")).toString() == QLatin1String("stopped");
}

lvd::Result<StopReply> parseStopReply(const QString& line) {
    using R = lvd::Result<StopReply>;
    auto r = parseReply(line);
    if (!r.ok())
        return R::failure(r.error());
    const QJsonObject& o = r.value();
    if (o.value(QLatin1String("status")).toString() != QLatin1String("stopped"))
        return R::failure("reply is not a stop (status is not \"stopped\")");
    const auto now = integer(o, "now");
    const auto delta = integer(o, "delta");
    if (!now || !delta)
        return R::failure("stop reply without \"now\" or \"delta\"");
    const QJsonObject stop = o.value(QLatin1String("stop")).toObject();
    const QString reason = stop.value(QLatin1String("reason")).toString();
    StopReply out;
    out.nowFs = *now;
    out.delta = *delta;
    if (reason == QLatin1String("breakpoint")) {
        const auto id = integer(stop, "id");
        if (!id)
            return R::failure("breakpoint stop without \"id\"");
        out.reason = StopReason::Breakpoint;
        out.atBreakpoint = true;
        out.breakpointId = static_cast<int>(*id);
    } else if (reason == QLatin1String("step")) {
        out.reason = StopReason::Step;
    } else if (reason == QLatin1String("stop")) {
        out.reason = StopReason::Stop;
    } else {
        return R::failure("unknown stop reason \"" + text(reason) + "\"");
    }
    out.process = stop.value(QLatin1String("process")).toString();
    const QJsonValue frame = stop.value(QLatin1String("frame"));
    if (out.reason == StopReason::Stop && (frame.isUndefined() || frame.isNull()))
        return out; // $stop: the kernel gives a frame, but the stop is meaningful without one
    auto f = frameFrom(frame.toObject());
    if (!f.ok())
        return R::failure(f.error());
    out.frame = f.value();
    return out;
}

lvd::Result<QVector<SourceFrame>> parseStackReply(const QString& line) {
    using R = lvd::Result<QVector<SourceFrame>>;
    auto r = parseReply(line);
    if (!r.ok())
        return R::failure(r.error());
    const QJsonValue list = r.value().value(QLatin1String("frames"));
    if (!list.isArray())
        return R::failure("reply has no \"frames\" array");
    QVector<SourceFrame> out;
    for (const QJsonValue& v : list.toArray()) {
        auto f = frameFrom(v.toObject());
        if (!f.ok())
            return R::failure(f.error());
        out.push_back(f.value());
    }
    return out;
}

lvd::Result<LocalsReply> parseLocalsReply(const QString& line) {
    using R = lvd::Result<LocalsReply>;
    auto r = parseReply(line);
    if (!r.ok())
        return R::failure(r.error());
    const auto frame = integer(r.value(), "frame");
    const QJsonValue list = r.value().value(QLatin1String("variables"));
    if (!frame || !list.isArray())
        return R::failure("reply has no \"frame\" or no \"variables\" array");
    LocalsReply out;
    out.frame = static_cast<int>(*frame);
    for (const QJsonValue& v : list.toArray()) {
        const QJsonObject o = v.toObject();
        if (!o.value(QLatin1String("name")).isString())
            return R::failure("variable without \"name\"");
        VariableInfo var;
        var.name = o.value(QLatin1String("name")).toString();
        var.kind = o.value(QLatin1String("kind")).toString();
        var.type = o.value(QLatin1String("type")).toString();
        const QJsonValue value = o.value(QLatin1String("value"));
        var.hasValue = value.isString();
        var.value = value.toString();
        out.variables.push_back(var);
    }
    return out;
}

bool isRestartReply(const QString& line) {
    auto r = parseReply(line);
    return r.ok() && r.value().value(QLatin1String("status")).toString() == QLatin1String("restarted");
}

lvd::Result<RestartReply> parseRestartReply(const QString& line) {
    using R = lvd::Result<RestartReply>;
    auto r = parseReply(line);
    if (!r.ok())
        return R::failure(r.error());
    const QJsonObject& o = r.value();
    if (o.value(QLatin1String("status")).toString() != QLatin1String("restarted"))
        return R::failure("not a restart reply");
    const auto now = integer(o, "now");
    const auto delta = integer(o, "delta");
    const auto breakpoints = integer(o, "breakpoints");
    const auto forces = integer(o, "forces");
    if (!now || !delta || !breakpoints || !forces)
        return R::failure("restart reply without \"now\", \"delta\", \"breakpoints\" or \"forces\"");
    RestartReply out;
    out.nowFs = *now;
    out.delta = *delta;
    out.breakpoints = static_cast<int>(*breakpoints);
    out.forces = static_cast<int>(*forces);
    return out;
}

std::optional<qint64> timeToFemtoseconds(const QString& number, const QString& unit) {
    int exponent = 0; // femtoseconds per unit is 10^exponent
    const QString u = unit.trimmed().toLower();
    if (u == QLatin1String("fs"))
        exponent = 0;
    else if (u == QLatin1String("ps"))
        exponent = 3;
    else if (u == QLatin1String("ns"))
        exponent = 6;
    else if (u == QLatin1String("us"))
        exponent = 9;
    else if (u == QLatin1String("ms"))
        exponent = 12;
    else
        return std::nullopt;

    const QString t = number.trimmed();
    const qsizetype dot = t.indexOf(QLatin1Char('.'));
    const QString whole = dot < 0 ? t : t.left(dot);
    QString frac = dot < 0 ? QString() : t.mid(dot + 1);
    if (whole.isEmpty() && frac.isEmpty())
        return std::nullopt;
    for (const QString& part : {whole, frac})
        for (QChar c : part)
            if (!c.isDigit() || c.unicode() > 0x7f)
                return std::nullopt;
    while (frac.endsWith(QLatin1Char('0')))
        frac.chop(1);
    if (frac.size() > exponent)
        return std::nullopt; // finer than a femtosecond
    // digits of the value scaled by 10^exponent: whole, then the fraction padded with zeros
    QString digits = whole + frac + QString(exponent - static_cast<int>(frac.size()), QLatin1Char('0'));
    while (digits.size() > 1 && digits.startsWith(QLatin1Char('0')))
        digits.remove(0, 1);
    if (digits.size() > 18)
        return std::nullopt; // would not fit
    return digits.toLongLong();
}

QString formatFemtoseconds(qint64 fs) {
    struct Unit {
        qint64 factor;
        const char* name;
    };
    static const Unit units[] = {
        {1'000'000'000'000, "ms"}, {1'000'000'000, "us"}, {1'000'000, "ns"}, {1'000, "ps"}, {1, "fs"}};
    if (fs == 0)
        return QStringLiteral("0 fs");
    for (const Unit& u : units)
        if (fs % u.factor == 0)
            return QStringLiteral("%1 %2").arg(fs / u.factor).arg(QLatin1String(u.name));
    return QStringLiteral("%1 fs").arg(fs);
}

} // namespace lvd::gui
