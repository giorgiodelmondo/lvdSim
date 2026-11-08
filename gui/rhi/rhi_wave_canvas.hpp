// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rhi/gpu_wave_renderer.hpp"

#include <QRhiWidget>
#include <QShowEvent>

#include <functional>

namespace lvd::gui {

/// The wave area drawn by the GPU: a QRhiWidget whose frames come from a provider (the wave widget, which
/// knows the rows, the view and the selection). Needs a window that renders through the RHI, which every
/// normal desktop platform does; headless platforms cannot, and failed() tells the owner to fall back to
/// the QPainter path.
class RhiWaveCanvas : public QRhiWidget {
    Q_OBJECT
public:
    using FrameProvider = std::function<GpuFrame(const QSize&)>;

    explicit RhiWaveCanvas(FrameProvider provider, QWidget* parent = nullptr);

    /// Drop all tiles on the GPU at the next frame (after the data changed).
    void requestCacheClear();
    QString backendName() const;
    const GpuWaveRenderer::Stats& lastStats() const noexcept { return renderer_.lastFrame(); }
    std::size_t cachedTiles() const noexcept { return renderer_.cachedTiles(); }
    bool isReady() const noexcept { return renderer_.isInitialized(); }
    /// Why failed() was emitted last (for the user: "no QRhi", "device creation failed", "shader load failed").
    QString failureReason() const { return failureReason_; }

signals:
    /// The GPU could not be used (no RHI support, device creation or shader loading failed).
    void failed();

protected:
    void showEvent(QShowEvent* e) override;
    void initialize(QRhiCommandBuffer* cb) override;
    void render(QRhiCommandBuffer* cb) override;
    void releaseResources() override;

private:
    FrameProvider provider_;
    GpuWaveRenderer renderer_;
    bool clearRequested_ = false;
    bool failedEmitted_ = false;
    QString failureReason_;
    void fail(const QString& reason);
};

}  // namespace lvd::gui
