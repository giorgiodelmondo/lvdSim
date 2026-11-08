// SPDX-License-Identifier: GPL-3.0-or-later
#include "rhi/rhi_wave_canvas.hpp"

#include <QShowEvent>
#include <QTimer>

namespace lvd::gui {

RhiWaveCanvas::RhiWaveCanvas(FrameProvider provider, QWidget* parent)
    : QRhiWidget(parent)
    , provider_(std::move(provider)) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);
    setColorBufferFormat(QRhiWidget::TextureFormat::RGBA8);
    connect(this, &QRhiWidget::renderFailed, this, [this] {
        fail(QStringLiteral("QRhi device creation failed"));
    });
}

QString RhiWaveCanvas::backendName() const {
    QRhi* r = rhi();
    return r != nullptr ? QString::fromLatin1(r->backendName()) : QString();
}

void RhiWaveCanvas::requestCacheClear() {
    clearRequested_ = true;
    update();
}

void RhiWaveCanvas::showEvent(QShowEvent* e) {
    QRhiWidget::showEvent(e);
    failedEmitted_ = false;  // a new attempt may report again
    // QRhiWidget only warns when the platform has no RHI. If nothing was initialised a while after the
    // window appeared, report it so the owner can fall back.
    QTimer::singleShot(2000, this, [this] {
        if (isVisible() && !renderer_.isInitialized()) fail(QStringLiteral("no QRhi (no first frame after 2 s)"));
    });
}

void RhiWaveCanvas::initialize(QRhiCommandBuffer*) {
    if (!renderer_.initialize(rhi(), renderTarget()->renderPassDescriptor(), colorTexture()->format())) {
        fail(QStringLiteral("shader load failed"));
    }
}

void RhiWaveCanvas::fail(const QString& reason) {
    if (failedEmitted_) return;
    failedEmitted_ = true;
    failureReason_ = reason;
    emit failed();
}

void RhiWaveCanvas::render(QRhiCommandBuffer* cb) {
    if (!renderer_.isInitialized()) return;
    if (clearRequested_) {
        renderer_.clearCache();
        clearRequested_ = false;
    }
    renderer_.render(cb, renderTarget(), provider_(size()));
}

void RhiWaveCanvas::releaseResources() { renderer_.releaseResources(); }

}  // namespace lvd::gui
