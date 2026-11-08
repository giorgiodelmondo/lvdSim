// SPDX-License-Identifier: GPL-3.0-or-later
#include "wave/render_policy.hpp"

#include <QGuiApplication>

namespace lvd::gui {

bool parseRenderPolicy(const QString& text, RenderPolicy* out) {
    const QString t = text.trimmed().toLower();
    RenderPolicy p;
    if (t == "auto")
        p = RenderPolicy::Auto;
    else if (t == "cpu" || t == "qpainter")
        p = RenderPolicy::Cpu;
    else if (t == "gpu")
        p = RenderPolicy::Gpu;
    else
        return false;
    if (out != nullptr) *out = p;
    return true;
}

QString renderPolicyName(RenderPolicy p) {
    switch (p) {
    case RenderPolicy::Cpu: return QStringLiteral("cpu");
    case RenderPolicy::Gpu: return QStringLiteral("gpu");
    case RenderPolicy::Auto: break;
    }
    return QStringLiteral("auto");
}

bool platformHasNoGpu(const QString& platformName) {
    return platformName == "offscreen" || platformName == "minimal" || platformName == "vnc";
}

RenderDecision resolveRenderPolicy(const QString& env, const QString& setting, const QString& platformName) {
    RenderDecision d;
    if (!parseRenderPolicy(env, &d.policy) && !parseRenderPolicy(setting, &d.policy)) d.policy = RenderPolicy::Auto;
    if (d.policy == RenderPolicy::Cpu) return d;
    if (platformHasNoGpu(platformName))
        d.cpuReason = QStringLiteral("platform %1").arg(platformName);
    else
        d.requestGpu = true;
    return d;
}

RenderDecision currentRenderDecision(const QString& setting) {
    return resolveRenderPolicy(qEnvironmentVariable("LVDSIM_RENDERER"), setting, QGuiApplication::platformName());
}

}  // namespace lvd::gui
