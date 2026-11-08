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

lvd::Result<BreakpointInfo> breakpointFrom(const QJsonObject& o) {
    const auto id = integer(o, "id");
    const auto time = integer(o, "time");
    if (!id || !time)
        return lvd::Result<BreakpointInfo>::failure("breakpoint without \"id\" or \"time\"");
    return BreakpointInfo{static_cast<int>(*id), *time};
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
        auto b = breakpointFrom(v.toObject());
        if (!b.ok())
            return R::failure(b.error());
        out.push_back(b.value());
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
