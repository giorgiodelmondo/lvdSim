// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace lvd::gui {

/// What the user wants for the waveform renderer. Auto asks for the GPU and falls back to QPainter when
/// the GPU cannot be used; Gpu asks for the GPU too but the fallback is always reported; Cpu never uses
/// the GPU. Every real application starts from resolveRenderPolicy(); a bare WaveWidget stays on QPainter.
enum class RenderPolicy { Auto, Cpu, Gpu };

struct RenderDecision {
    RenderPolicy policy = RenderPolicy::Auto;
    bool requestGpu = false;  ///< ask the wave widget for the GPU backend
    QString cpuReason;        ///< why the GPU is not requested although the policy wants it (empty if none)
};

/// "auto", "cpu"/"qpainter", "gpu" (case insensitive, trimmed). Anything else is not a policy.
bool parseRenderPolicy(const QString& text, RenderPolicy* out);
/// The text stored in the settings file.
QString renderPolicyName(RenderPolicy p);

/// True on platforms without a GPU rendering path (offscreen, minimal, vnc).
bool platformHasNoGpu(const QString& platformName);

/// Pure policy: environment variable LVDSIM_RENDERER (cpu, qpainter, gpu, auto; unset or unknown = no
/// override) beats the stored setting (render/backend; unset or unknown = Auto). A policy that wants the
/// GPU does not request it on a platform without one, and says why.
RenderDecision resolveRenderPolicy(const QString& env, const QString& setting, const QString& platformName);

/// resolveRenderPolicy() with the real environment, the given stored setting and the running platform.
RenderDecision currentRenderDecision(const QString& setting);

/// Settings key of the stored choice.
inline constexpr const char* kRenderSettingKey = "render/backend";

}  // namespace lvd::gui
