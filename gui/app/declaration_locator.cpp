// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/declaration_locator.hpp"

#include <QCoreApplication>
#include <QRegularExpression>

namespace lvd::gui {

namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("DeclarationLocator", text);
}

} // namespace

QString shellPathOfStoreSignal(const QString& dotted) {
    QStringList parts = dotted.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (parts.size() < 2)
        return {};
    parts.removeFirst();
    static const QRegularExpression range(QStringLiteral("(\\[[^\\]]*\\])+$"));
    parts.last().remove(range);
    return QLatin1Char('/') + parts.join(QLatin1Char('/'));
}

QString shellPathOfStoreScope(const QString& dotted) {
    QStringList parts = dotted.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return {};
    parts.removeFirst();
    if (parts.isEmpty())
        return QStringLiteral("/");
    return QLatin1Char('/') + parts.join(QLatin1Char('/')) + QLatin1Char('/');
}

QString describeLocationCommand(const QString& shellPath) {
    return QStringLiteral("describe -json -location {%1}").arg(shellPath);
}

void DeclarationLocator::reset() {
    ++generation_;
    cache_.clear();
    waiting_.clear();
    support_ = Support::Unknown;
    probing_ = false;
}

void DeclarationLocator::learnSupport(const DescribedObject& o) {
    support_ = o.hasLocation() ? Support::Yes : Support::No;
}

void DeclarationLocator::probe() {
    if (!request_ || probing_ || support_ != Support::Unknown)
        return;
    probing_ = true;
    const int gen = generation_;
    ++sent_;
    request_(
        describeLocationCommand(QStringLiteral("/")),
        [this, gen](const QString& json) {
            if (gen != generation_)
                return;
            probing_ = false;
            const auto r = parseDescribeReply(json);
            if (!r.ok() || r.value().isEmpty()) {
                support_ = Support::No;
                return;
            }
            learnSupport(r.value().first());
            cache_.insert(QStringLiteral("/"), r.value().first());
        },
        [this, gen](const QString&) {
            if (gen != generation_)
                return;
            probing_ = false;
            support_ = Support::No; // a kernel without the -location option refuses it
        });
}

QString DeclarationLocator::unavailableReason(bool simulationRunning) const {
    if (!simulationRunning)
        return tr("Needs a running simulation: the simulator knows where objects are declared");
    switch (support_) {
    case Support::Yes:
        return {};
    case Support::Unknown:
        return {}; // asked when used
    case Support::No:
        break;
    }
    return tr("This simulator does not report source locations (describe -location needs a newer nvc kernel "
              "of the lvdsim fork)");
}

void DeclarationLocator::describe(const QString& shellPath, Done done) {
    if (const auto it = cache_.constFind(shellPath); it != cache_.constEnd()) {
        done(lvd::Result<DescribedObject>(it.value()));
        return;
    }
    if (!request_) {
        done(lvd::Result<DescribedObject>::failure("no simulation"));
        return;
    }
    auto w = waiting_.find(shellPath);
    if (w != waiting_.end()) {
        w.value().push_back(std::move(done));
        return;
    }
    waiting_.insert(shellPath, {std::move(done)});
    const int gen = generation_;
    ++sent_;
    auto finish = [this, gen, shellPath](const lvd::Result<DescribedObject>& r) {
        if (gen != generation_)
            return;
        const QVector<Done> callbacks = waiting_.take(shellPath);
        for (const Done& d : callbacks)
            d(r);
    };
    request_(
        describeLocationCommand(shellPath),
        [this, finish, shellPath, gen](const QString& json) {
            if (gen != generation_)
                return; // an answer to a session that was reset since
            const auto r = parseDescribeReply(json);
            if (!r.ok()) {
                finish(lvd::Result<DescribedObject>::failure(r.error()));
                return;
            }
            if (r.value().isEmpty()) {
                finish(lvd::Result<DescribedObject>::failure("empty reply"));
                return;
            }
            const DescribedObject o = r.value().first();
            if (support_ == Support::Unknown || o.hasLocation())
                learnSupport(o);
            cache_.insert(shellPath, o);
            finish(lvd::Result<DescribedObject>(o));
        },
        [finish](const QString& error) {
            finish(lvd::Result<DescribedObject>::failure(
                (error.isEmpty() ? QStringLiteral("the simulator does not know this name") : error)
                    .toStdString()));
        });
}

} // namespace lvd::gui
