// SPDX-License-Identifier: GPL-3.0-or-later
#include "wave/wave_widget.hpp"

#include "rhi/rhi_wave_canvas.hpp"
#include "wave/analog_settings_dialog.hpp"
#include "wave/painter_renderer.hpp"
#include "wave/render_policy.hpp"

#include <QApplication>
#include <QColorDialog>
#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHelpEvent>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPolygon>
#include <QScrollBar>
#include <QTimer>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>

static void lvdInitWaveResources() { Q_INIT_RESOURCE(wave_bars); }  // global scope: the macro declares a function

namespace lvd::gui {

using namespace lvd::wave;

namespace {

Time toTime(double t) {
    return t <= 0.0 ? Time{0} : static_cast<Time>(t);
}

std::uint64_t fnv(std::uint64_t h, std::uint64_t v) {
    h ^= v;
    return h * 1099511628211ull;
}

std::uint64_t hashString(std::uint64_t h, const std::string& s) {
    for (unsigned char c : s)
        h = fnv(h, c);
    return fnv(h, 0xff);
}

std::uint64_t hashDouble(std::uint64_t h, double d) {
    return fnv(h, scaleBitsOf(d));
}

// Identity of a row for the tile cache: the item id (and the bit of a per-bit row), never an address or a
// position, so moving a row, grouping it or undoing a move keeps its cached tiles.
const void* identityKey(ItemId id, int bit) {
    std::uint64_t k = id;
    if (bit >= 0)
        k = fnv(fnv(k, 0xB17B17ull), static_cast<std::uint64_t>(bit) + 1);
    return reinterpret_cast<const void*>(static_cast<std::uintptr_t>(k));
}

QColor toQColor(std::uint32_t rgb) {
    return QColor((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

// Nearest transition of a row strictly before (dir < 0) or after (dir > 0) time t.
std::optional<double> edgeFrom(const RowSource& src, Time t, int dir) {
    const std::size_t n = src.count();
    const std::size_t hold = src.holdIndex(t);
    if (dir > 0) {
        std::size_t j = hold == kNoIndex ? 0 : hold + 1;
        while (j < n && src.time(j) <= t)
            ++j;
        if (j < n)
            return static_cast<double>(src.time(j));
    } else if (hold != kNoIndex) {
        std::size_t j = hold + 1;
        while (j > 0 && src.time(j - 1) >= t)
            --j;
        if (j > 0)
            return static_cast<double>(src.time(j - 1));
    }
    return std::nullopt;
}

const QColor kCursorColors[] = {QColor(0xff, 0xe0, 0x40), QColor(0xff, 0x80, 0xff), QColor(0x40, 0xe0, 0xff),
                                QColor(0xff, 0xa0, 0x40), QColor(0xa0, 0xff, 0x60), QColor(0xff, 0x70, 0x70),
                                QColor(0xa0, 0xa0, 0xff), QColor(0xe0, 0xe0, 0xe0)};

QColor eventColor(EventSeverity s) {
    switch (s) {
    case EventSeverity::Note:
        return QColor(0x40, 0xc4, 0xe0); // teal blue
    case EventSeverity::Warning:
        return QColor(0xff, 0xb4, 0x20); // amber
    case EventSeverity::Error:
        return QColor(0xff, 0x55, 0x55); // red
    case EventSeverity::Failure:
        return QColor(0xff, 0x40, 0xd8); // magenta
    }
    return QColor(0xe0, 0xe0, 0xe0);
}

const char* eventSeverityName(EventSeverity s) {
    switch (s) {
    case EventSeverity::Note:
        return "Note";
    case EventSeverity::Warning:
        return "Warning";
    case EventSeverity::Error:
        return "Error";
    case EventSeverity::Failure:
        return "Failure";
    }
    return "";
}

QString scrollBarStyleSheet() {
    const QString common = QStringLiteral(
        "QScrollBar { background: #1c1c1c; border: 0; }"
        "QScrollBar::handle { background: #5a5a5a; border-radius: 3px; }"
        "QScrollBar::handle:hover, QScrollBar::handle:pressed { background: #7a7a7a; }"
        "QScrollBar::add-page, QScrollBar::sub-page { background: #1c1c1c; }"
        "QScrollBar::add-line, QScrollBar::sub-line { background: #262626; border: 0; subcontrol-origin: margin; }");
    const QString vertical = QStringLiteral(
        "QScrollBar:vertical { width: 15px; margin: 15px 0 15px 0; }"
        "QScrollBar::handle:vertical { min-height: 24px; margin: 0 2px; }"
        "QScrollBar::add-line:vertical { height: 15px; subcontrol-position: bottom; }"
        "QScrollBar::sub-line:vertical { height: 15px; subcontrol-position: top; }"
        "QScrollBar::up-arrow:vertical { image: url(:/lvdsim/wave/arrow_up.png); }"
        "QScrollBar::down-arrow:vertical { image: url(:/lvdsim/wave/arrow_down.png); }");
    const QString horizontal = QStringLiteral(
        "QScrollBar:horizontal { height: 15px; margin: 0 15px 0 15px; }"
        "QScrollBar::handle:horizontal { min-width: 24px; margin: 2px 0; }"
        "QScrollBar::add-line:horizontal { width: 15px; subcontrol-position: right; }"
        "QScrollBar::sub-line:horizontal { width: 15px; subcontrol-position: left; }"
        "QScrollBar::left-arrow:horizontal { image: url(:/lvdsim/wave/arrow_left.png); }"
        "QScrollBar::right-arrow:horizontal { image: url(:/lvdsim/wave/arrow_right.png); }");
    return common + vertical + horizontal;
}

} // namespace

/// A transparent widget laid over the whole wave widget. In GPU mode the canvas covers the wave area and
/// cannot be painted on with QPainter, so bus value labels and cursors are painted here, above it.
class WaveOverlay : public QWidget {
public:
    explicit WaveOverlay(WaveWidget* owner)
        : QWidget(owner)
        , owner_(owner) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        owner_->paintOverlay(p);
    }

private:
    WaveWidget* owner_;
};

WaveWidget::WaveWidget(QWidget* parent)
    : QWidget(parent)
    , renderer_(std::make_unique<PainterRenderer>(palette_)) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(320, 120);

    vbar_ = new QScrollBar(Qt::Vertical, this);
    connect(vbar_, &QScrollBar::valueChanged, this, [this](int v) {
        scrollY_ = v;
        if (editor_ != nullptr)
            positionEditor();
        update();
    });
    lvdInitWaveResources();
    hbar_ = new QScrollBar(Qt::Horizontal, this);
    hbar_->setObjectName("waveHorizontalBar");
    vbar_->setObjectName("waveVerticalBar");
    hbar_->setRange(0, 0);
    hbar_->setPageStep(kHBarSteps);
    // The bars sit on the dark wave area. A style sheet gives every platform style the same dark track, grey
    // handle and arrow buttons (native styles would draw hairline or invisible bars on a dark background).
    for (QScrollBar* bar : {vbar_, hbar_}) bar->setStyleSheet(scrollBarStyleSheet());
    connect(hbar_, &QScrollBar::valueChanged, this, [this](int v) { hbarMoved(v); });
    autoScroll_ = new QTimer(this);
    autoScroll_->setInterval(40);
    connect(autoScroll_, &QTimer::timeout, this, [this] {
        if (autoScrollStep() != 0)
            updateRowDrag(lastMouse_);
    });
}

WaveWidget::~WaveWidget() = default;

WaveWidget::Backend WaveWidget::backendFromEnvironment() {
    return currentRenderDecision({}).requestGpu ? Backend::Gpu : Backend::Cpu;
}

void WaveWidget::setRenderBackend(Backend backend) {
    requested_ = backend;
    fallbackReason_.clear();
    if (backend == Backend::Gpu) {
        // Headless platforms have no RHI: do not even create the canvas, and say so.
        const QString platform = QGuiApplication::platformName();
        if (platformHasNoGpu(platform)) {
            const bool was = gpuActive_;
            gpuActive_ = false;
            fallbackReason_ = tr("platform %1").arg(platform);
            if (was)
                emit renderBackendChanged();
            return;
        }
        if (canvas_ == nullptr) {
            canvas_ = new RhiWaveCanvas([this](const QSize& size) { return buildGpuFrame(size); }, this);
            overlay_ = new WaveOverlay(this);
            connect(canvas_, &RhiWaveCanvas::failed, this, [this] { fallBackToCpu(); });
        }
        gpuActive_ = true;
        canvas_->show();
        overlay_->show();
        updateGpuGeometry();
    } else {
        gpuActive_ = false;
        if (canvas_ != nullptr) {
            canvas_->hide();
            overlay_->hide();
        }
    }
    update();
    emit renderBackendChanged();
}

void WaveWidget::fallBackToCpu() {
    if (!gpuActive_)
        return;
    gpuActive_ = false;
    fallbackReason_ = canvas_->failureReason();
    canvas_->hide();
    overlay_->hide();
    update();
    emit renderBackendChanged();
}

void WaveWidget::updateGpuGeometry() {
    if (canvas_ == nullptr)
        return;
    canvas_->setGeometry(waveLeft(), kRowsTop, waveWidth(), std::max(1, rowsBottom() - kRowsTop));
    overlay_->setGeometry(rect());
    overlay_->raise();
    vbar_->raise();
    hbar_->raise();
}

int WaveWidget::effectiveWaveRight() const {
    return width() - vbar_->sizeHint().width();
}
int WaveWidget::rowsBottom() const {
    return std::max(kRowsTop, height() - hbar_->sizeHint().height());
}

void WaveWidget::layoutBars() {
    const int sbw = vbar_->sizeHint().width();
    const int bottom = rowsBottom();
    vbar_->setGeometry(width() - sbw, kRowsTop, sbw, std::max(0, bottom - kRowsTop));
    const int left = std::min(waveLeft(), std::max(0, width() - sbw));
    hbar_->setGeometry(left, bottom, std::max(0, width() - sbw - left), std::max(0, height() - bottom));
}
int WaveWidget::waveWidth() const {
    return std::max(1, effectiveWaveRight() - waveLeft());
}

void WaveWidget::setStore(std::shared_ptr<const WaveStore> store) {
    autoFit_ = true;
    store_ = std::move(store);
    lastEnd_ = store_ ? static_cast<double>(store_->endTime()) : 0.0;
    indexes_.clear();
    busCache_.clear();
    bitCache_.clear();
    ++storeRevision_;
    cache_.clear();
    selected_.clear();
    cancelRename();
    cursors_.clear();
    activeCursor_ = 0;
    rebuildRows();
    fitAll();
    if (follow_)
        pinToEnd();
    emit cursorsChanged();
    emit selectionChanged();
}

void WaveWidget::setDisplayModel(DisplayModel model) {
    cancelRowDrag();
    cancelRename();
    model_ = std::move(model);
    clearUndoHistory();
    selected_.clear();
    rebuildRows();
    emit selectionChanged();
}

void WaveWidget::setRenderer(std::unique_ptr<IRenderer> renderer) {
    if (!renderer)
        return;
    renderer_ = std::move(renderer);
    cache_.clear();
    update();
}

void WaveWidget::setView(double startTime, double timePerPixel) {
    viewPannedByUser();
    view_.widthPx = waveWidth();
    view_.startTime = startTime;
    view_.timePerPixel = timePerPixel;
    clampView();
    emitViewChanged();
}

void WaveWidget::fitAll() {
    autoFit_ = true;
    view_.widthPx = waveWidth();
    if (store_)
        view_.fit(static_cast<double>(store_->startTime()), static_cast<double>(store_->endTime()));
    else
        view_.fit(0.0, 1.0);
    emitViewChanged();
}

void WaveWidget::zoomFull() {
    if (follow_)
        setFollow(false);
    fitAll();
}

void WaveWidget::zoomBy(double factor) {
    autoFit_ = false;
    view_.widthPx = waveWidth();
    view_.zoomAt(view_.widthPx / 2.0, factor);
    clampView();
    if (follow_)
        pinToEnd();
    emitViewChanged();
}

void WaveWidget::setFollow(bool on) {
    if (follow_ == on)
        return;
    follow_ = on;
    if (on) {
        if (store_) autoFit_ = false;  // the current width (the whole run, if nothing was zoomed) becomes the follow width
        pinToEnd();
    }
    emit followChanged(on);
}

void WaveWidget::pinToEnd() {
    view_.widthPx = waveWidth();
    if (autoFit_) {  // Follow was set before there was data to take a width from: the view still fits the run
        if (store_) view_.fit(static_cast<double>(store_->startTime()), static_cast<double>(store_->endTime()));
        emitViewChanged();
        return;
    }
    const double end = store_ ? static_cast<double>(store_->endTime()) : 0.0;
    view_.startTime = std::max(0.0, end - view_.widthPx * view_.timePerPixel);
    clampView();
    emitViewChanged();
}

void WaveWidget::viewPannedByUser() {
    autoFit_ = false;
    if (follow_)
        setFollow(false);
}

void WaveWidget::clampView() {
    view_.clamp(store_ ? static_cast<double>(store_->endTime()) : 0.0);
}

void WaveWidget::emitViewChanged() {
    updateHBar();
    update();
    emit viewChanged();
}

// ---------------------------------------------------------------------------------------------
// Cursors

std::optional<double> WaveWidget::cursorTime(int which) const noexcept {
    if (which < 0 || which >= cursorCount())
        return std::nullopt;
    return cursors_[static_cast<std::size_t>(which)];
}

std::optional<double> WaveWidget::activeCursorTime() const {
    return cursorTime(activeCursor_);
}

void WaveWidget::setCursorTime(int which, std::optional<double> t) {
    if (which < 0 || which > cursorCount())
        return;
    if (!t) {
        if (which < cursorCount())
            removeCursor(which);
        return;
    }
    const double v = std::max(0.0, *t);
    if (which == cursorCount()) {
        if (cursorCount() >= kMaxCursors)
            return;
        cursors_.push_back(v);
        activeCursor_ = which;
    } else {
        cursors_[static_cast<std::size_t>(which)] = v;
    }
    update();
    emit cursorsChanged();
}

void WaveWidget::setActiveCursor(int which) {
    if (which < 0 || which >= cursorCount())
        return;
    activeCursor_ = which;
    update();
    emit cursorsChanged();
}

int WaveWidget::addCursor(std::optional<double> t) {
    if (cursorCount() >= kMaxCursors)
        return -1;
    view_.widthPx = waveWidth();
    const double at = t ? *t : view_.timeAt(view_.widthPx / 2.0);
    setCursorTime(cursorCount(), at);
    return activeCursor_;
}

void WaveWidget::removeCursor(int which) {
    if (which < 0 || which >= cursorCount())
        return;
    cursors_.erase(cursors_.begin() + which);
    activeCursor_ = std::clamp(activeCursor_ > which ? activeCursor_ - 1 : activeCursor_, 0,
                               std::max(0, cursorCount() - 1));
    update();
    emit cursorsChanged();
}

int WaveWidget::cursorNear(int x) const {
    int best = -1;
    double bestDist = 5.0;
    for (int i = 0; i < cursorCount(); ++i) {
        const double px = waveLeft() + view_.xAt(cursors_[static_cast<std::size_t>(i)]);
        const double d = std::fabs(px - x);
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

void WaveWidget::ensureVisible(double t) {
    autoFit_ = false;
    view_.widthPx = waveWidth();
    if (t >= view_.startTime && t <= view_.endTime())
        return;
    if (follow_)
        setFollow(false);
    view_.startTime = std::max(0.0, t - view_.widthPx * 0.2 * view_.timePerPixel);
    clampView();
    emitViewChanged();
}

// ---------------------------------------------------------------------------------------------
// Rows

const NumericIndex* WaveWidget::numericIndexFor(SignalId id, bool isSigned) {
    const SignalId real = store_->resolve(id);
    auto key = std::make_pair(real, isSigned);
    auto it = indexes_.find(key);
    if (it == indexes_.end()) {
        auto idx = std::make_unique<NumericIndex>(NumericIndex::build(store_->signal(real), isSigned));
        it = indexes_.emplace(key, std::move(idx)).first;
    }
    return it->second.get();
}

void WaveWidget::rebuildRows() {
    rows_.clear();
    int y = 0;
    auto push = [&](Row r) {
        const bool analog = r.source && r.source->waveClass() == WaveClass::Analog;
        r.h = analog ? std::max(1, r.item->analog.heightRows) * kRowHeight : kRowHeight;
        r.y = y;
        y += r.h;
        rows_.push_back(std::move(r));
    };

    for (const DisplayRow& dr : model_.visibleRows()) {
        Row r;
        r.item = const_cast<DisplayItem*>(dr.item); // the model is owned and mutable here
        r.depth = dr.depth;
        const DisplayItem& it = *r.item;

        std::uint64_t rev = fnv(14695981039346656037ull, storeRevision_);
        rev = hashString(rev, it.path);
        for (const BusMember& m : it.members) {
            rev = hashString(rev, m.path);
            rev = fnv(rev, static_cast<std::uint64_t>(m.bit + 1));
        }
        rev = fnv(rev, it.analog.enabled);
        rev = fnv(rev, it.analog.autoRange);
        rev = hashDouble(rev, it.analog.min);
        rev = hashDouble(rev, it.analog.max);
        rev = fnv(rev, static_cast<std::uint64_t>(it.analog.interpolation));
        rev = fnv(rev, it.analog.isSigned);
        rev = fnv(rev, it.color ? (*it.color | 0x1000000u) : 0u);
        r.revision = rev;
        r.cacheKey = identityKey(it.id, -1);

        SignalId signalId = kNoSignal;
        if (store_ && it.kind == ItemKind::Signal) {
            signalId = store_->findSignal(it.path);
            if (signalId != kNoSignal) {
                const Signal& sig = store_->signal(signalId);
                const NumericIndex* idx =
                    it.analog.enabled ? numericIndexFor(signalId, it.analog.isSigned) : nullptr;
                r.source = std::make_shared<SignalSource>(sig, idx, it.analog.enabled);
                r.expandable = sig.kind() == SignalKind::Logic && sig.width() > 1;
            }
        } else if (store_ && it.kind == ItemKind::VirtualBus) {
            std::string key = std::to_string(storeRevision_);
            for (const BusMember& m : it.members)
                key += '|' + m.path + '#' + std::to_string(m.bit);
            auto found = busCache_.find(key);
            if (found == busCache_.end())
                found = busCache_.emplace(key, std::make_shared<BusSource>(*store_, it.members)).first;
            r.source = found->second;
        }

        r.style.autoRange = it.analog.autoRange;
        r.style.min = it.analog.min;
        r.style.max = it.analog.max;
        r.style.linear = it.analog.interpolation == Interpolation::Linear;
        if (it.color) {
            r.style.hasColor = true;
            r.style.color = toQColor(*it.color);
        }

        const Row parent = r; // copy for the per-bit rows
        push(std::move(r));

        if (it.showBits && signalId != kNoSignal && parent.expandable) {
            const SignalId real = store_->resolve(signalId);
            const std::uint32_t width = store_->signal(real).width();
            for (std::uint32_t b = width; b-- > 0;) {
                Row br = parent;
                br.bit = static_cast<int>(b);
                br.depth = parent.depth + 1;
                br.expandable = false;
                br.revision = fnv(parent.revision, b + 1);
                auto key = std::make_pair(real, static_cast<int>(b));
                auto found = bitCache_.find(key);
                if (found == bitCache_.end())
                    found =
                        bitCache_.emplace(key, std::make_shared<BitSource>(store_->signal(real), b)).first;
                br.source = found->second;
                br.cacheKey = identityKey(it.id, br.bit);
                push(std::move(br));
            }
        }
    }
    updateScrollBar();
    if (editor_ != nullptr)
        positionEditor();
    update();
}

void WaveWidget::updateScrollBar() {
    const int content = contentHeight();
    const int page = std::max(1, rowsBottom() - kRowsTop);
    vbar_->setRange(0, std::max(0, content - page));
    vbar_->setPageStep(page);
    vbar_->setSingleStep(kRowHeight);
    scrollY_ = std::clamp(scrollY_, 0, vbar_->maximum());
    vbar_->setValue(scrollY_);
}

int WaveWidget::contentHeight() const {
    return rows_.empty() ? 0 : rows_.back().y + rows_.back().h;
}

void WaveWidget::updateHBar() {
    if (hbar_ == nullptr)
        return;
    const double t0 = store_ ? static_cast<double>(store_->startTime()) : 0.0;
    const double end = store_ ? static_cast<double>(store_->endTime()) : 0.0;
    const double span = std::max(1e-9, view_.widthPx * view_.timePerPixel);
    const double total = std::max({end - t0, span, 1.0});
    const double ratio = std::min(1.0, span / total);
    const int page = std::clamp(static_cast<int>(std::lround(ratio * kHBarSteps)), 1, kHBarSteps);
    const int maxValue = kHBarSteps - page;
    int value = 0;
    if (maxValue > 0) {
        const double f = (view_.startTime - t0) / total;
        value = std::clamp(static_cast<int>(std::lround(f * kHBarSteps)), 0, maxValue);
    }
    hbarSync_ = true;
    hbar_->setRange(0, maxValue);
    hbar_->setPageStep(page);
    hbar_->setSingleStep(std::max(1, page / 10));
    if (!hbar_->isSliderDown())
        hbar_->setValue(value); // while the user drags, the thumb is where the mouse put it
    hbarSync_ = false;
}

void WaveWidget::hbarMoved(int value) {
    if (hbarSync_ || !store_)
        return;
    const double t0 = static_cast<double>(store_->startTime());
    const double end = static_cast<double>(store_->endTime());
    view_.widthPx = waveWidth();
    const double span = view_.widthPx * view_.timePerPixel;
    const double total = std::max({end - t0, span, 1.0});
    if (value >= hbar_->maximum() && hbar_->maximum() > 0)
        view_.startTime = std::max(t0, t0 + total - span);
    else
        view_.startTime = t0 + total * static_cast<double>(value) / kHBarSteps;
    viewPannedByUser();
    clampView();
    emitViewChanged();
}

int WaveWidget::rowTop(int row) const {
    return kRowsTop + rows_[static_cast<std::size_t>(row)].y - scrollY_;
}

int WaveWidget::rowAtY(int y) const {
    const int content = y - kRowsTop + scrollY_;
    if (content < 0 || rows_.empty())
        return -1;
    // Rows are stacked in order: binary search for the last row that starts at or above `content`.
    const auto it =
        std::upper_bound(rows_.begin(), rows_.end(), content, [](int c, const Row& r) { return c < r.y; });
    if (it == rows_.begin())
        return -1;
    const Row& r = *(it - 1);
    return content < r.y + r.h ? static_cast<int>(it - rows_.begin()) - 1 : -1;
}

std::string WaveWidget::nameOf(const Row& row) const {
    if (row.item->kind == ItemKind::Divider)
        return row.item->label;
    std::string name = row.item->displayName();
    if (row.bit >= 0)
        name += "[" + std::to_string(row.bit) + "]";
    return name;
}

std::string WaveWidget::rowName(int row) const {
    return nameOf(rows_.at(static_cast<std::size_t>(row)));
}
bool WaveWidget::rowHasSource(int row) const {
    return rows_.at(static_cast<std::size_t>(row)).source != nullptr;
}
int WaveWidget::rowBit(int row) const {
    return rows_.at(static_cast<std::size_t>(row)).bit;
}
bool WaveWidget::rowExpandable(int row) const {
    return rows_.at(static_cast<std::size_t>(row)).expandable;
}

std::optional<QColor> WaveWidget::rowColor(int row) const {
    const Row& r = rows_.at(static_cast<std::size_t>(row));
    if (!r.item->color)
        return std::nullopt;
    return toQColor(*r.item->color);
}

WaveClass WaveWidget::rowClass(int row) const {
    const Row& r = rows_.at(static_cast<std::size_t>(row));
    return r.source ? r.source->waveClass() : WaveClass::Bus;
}

Time WaveWidget::dataEnd() const {
    return store_ ? store_->endTime() : std::numeric_limits<Time>::max();
}

int WaveWidget::noDataLeft() const {
    if (!store_)
        return waveLeft() + view_.widthPx;
    const double x = std::floor(view_.xAt(static_cast<double>(store_->endTime()))) +
                     1.0; // the column holding the end is drawn
    return waveLeft() + static_cast<int>(std::clamp(x, -1.0, static_cast<double>(view_.widthPx) + 1.0));
}

QColor WaveWidget::noDataTint() const {
    return QColor(255, 255, 255, 10);
}

std::string WaveWidget::rowValueText(int row) const {
    const Row& r = rows_.at(static_cast<std::size_t>(row));
    const auto cursor = activeCursorTime();
    if (!r.source || !cursor)
        return {};
    if (store_ && *cursor > static_cast<double>(store_->endTime()))
        return "-"; // no data after the end of the run
    const std::size_t idx = r.source->holdIndex(toTime(*cursor));
    if (idx == kNoIndex)
        return "-";
    return r.source->text(idx, r.item->radix);
}

// ---------------------------------------------------------------------------------------------
// Selection and editing

bool WaveWidget::isSelected(const Row& row) const {
    return std::find(selected_.begin(), selected_.end(), RowKey{row.item->id, row.bit}) != selected_.end();
}

std::vector<DisplayItem*> WaveWidget::selectedItems() const {
    std::vector<DisplayItem*> items;
    for (const RowKey& k : selected_) {
        if (k.bit >= 0)
            continue;
        DisplayItem* it = lvd::wave::findItem(const_cast<DisplayModel&>(model_), k.id);
        if (it != nullptr && std::find(items.begin(), items.end(), it) == items.end())
            items.push_back(it);
    }
    return items;
}

void WaveWidget::selectRow(int row, bool additive) {
    if (row < 0 || row >= rowCount())
        return;
    const Row& r = rows_[static_cast<std::size_t>(row)];
    const RowKey key{r.item->id, r.bit};
    if (!additive) {
        selected_.assign(1, key);
    } else {
        auto f = std::find(selected_.begin(), selected_.end(), key);
        if (f == selected_.end())
            selected_.push_back(key);
        else
            selected_.erase(f);
    }
    anchorRow_ = row;
    update();
    emit selectionChanged();
}

void WaveWidget::selectRange(int fromRow, int toRow) {
    if (rowCount() == 0)
        return;
    int a = std::clamp(std::min(fromRow, toRow), 0, rowCount() - 1);
    int b = std::clamp(std::max(fromRow, toRow), 0, rowCount() - 1);
    selected_.clear();
    for (int i = a; i <= b; ++i) {
        const Row& r = rows_[static_cast<std::size_t>(i)];
        selected_.push_back({r.item->id, r.bit});
    }
    update();
    emit selectionChanged();
}

void WaveWidget::clearSelection() {
    selected_.clear();
    update();
    emit selectionChanged();
}

void WaveWidget::setRadixForSelection(Radix radix) {
    const std::vector<DisplayItem*> items = selectedItems();
    if (items.empty())
        return;
    Snapshot before{model_, selected_, tr("Change radix")};
    for (DisplayItem* it : items)
        if (it->kind == ItemKind::Signal || it->kind == ItemKind::VirtualBus)
            it->radix = radix;
    pushUndo(std::move(before));
    rebuildRows();
    emit structureChanged();
}

void WaveWidget::toggleAnalogForSelection() {
    const std::vector<DisplayItem*> items = selectedItems();
    if (items.empty())
        return;
    Snapshot before{model_, selected_, tr("Toggle analog trace")};
    for (DisplayItem* it : items)
        if (it->kind == ItemKind::Signal)
            it->analog.enabled = !it->analog.enabled;
    pushUndo(std::move(before));
    rebuildRows();
    emit structureChanged();
}

bool WaveWidget::analogSelectionAvailable() const {
    for (const DisplayItem* it : selectedItems())
        if (it->kind == ItemKind::Signal && it->analog.enabled) return true;
    return false;
}

std::optional<AnalogRange> WaveWidget::effectiveAnalogRange(int row) const {
    if (row < 0 || row >= rowCount()) return std::nullopt;
    const Row& r = rows_[static_cast<std::size_t>(row)];
    if (!r.source || r.source->waveClass() != WaveClass::Analog || r.bit >= 0) return std::nullopt;
    std::optional<AnalogRange> data;
    if (r.item->analog.autoRange) {
        const NumericIndex* idx = r.source->numeric();
        if (idx != nullptr && idx->valid() && idx->size() != 0) {
            const MinMax all = idx->range(0, idx->size());
            if (!all.empty()) data = AnalogRange{all.min, all.max};
        }
    }
    return resolveAnalogRange(r.item->analog, data);
}

bool WaveWidget::setAnalogRangeForSelection(bool autoRange, double min, double max) {
    lastEditError_.clear();
    std::vector<DisplayItem*> targets;
    for (DisplayItem* it : selectedItems())
        if (it->kind == ItemKind::Signal && it->analog.enabled) targets.push_back(it);
    auto refuse = [&](const QString& why) {
        lastEditError_ = why;
        emit editRejected(why);
        return false;
    };
    if (targets.empty()) return refuse(tr("No analog row is selected"));
    if (!autoRange) {
        const std::string why = analogRangeError(min, max);
        if (!why.empty()) return refuse(tr("Invalid analog range: %1").arg(QString::fromStdString(why)));
    }
    bool changes = false;
    for (const DisplayItem* it : targets) {
        const AnalogSettings& a = it->analog;
        if (a.autoRange != autoRange || (!autoRange && (a.min != min || a.max != max))) changes = true;
    }
    if (!changes) return false;
    Snapshot before{model_, selected_, autoRange ? tr("Analog range: auto") : tr("Analog range: manual")};
    for (DisplayItem* it : targets) {
        it->analog.autoRange = autoRange;
        if (!autoRange) {
            it->analog.min = min;
            it->analog.max = max;
        }
    }
    pushUndo(std::move(before));
    rebuildRows();
    emit structureChanged();
    return true;
}

bool WaveWidget::showAnalogSettings(int row) {
    // The row the dialog starts from: the clicked row if it is an analog trace, else the first selected one.
    int primary = -1;
    if (row >= 0 && effectiveAnalogRange(row) && isSelected(rows_[static_cast<std::size_t>(row)])) primary = row;
    for (int i = 0; primary < 0 && i < rowCount(); ++i)
        if (rows_[static_cast<std::size_t>(i)].bit < 0 && isSelected(rows_[static_cast<std::size_t>(i)]) &&
            rows_[static_cast<std::size_t>(i)].item->analog.enabled && rows_[static_cast<std::size_t>(i)].item->kind == ItemKind::Signal)
            primary = i;
    if (primary < 0) return false;

    const AnalogSettings& a = rows_[static_cast<std::size_t>(primary)].item->analog;
    AnalogSettingsInput in;
    in.autoRange = a.autoRange;
    in.min = a.min;
    in.max = a.max;
    if (const auto eff = effectiveAnalogRange(primary)) {
        in.min = eff->min;
        in.max = eff->max;
    }
    AnalogSettingsDialog dialog(in, this);
    const bool accepted = analogRunner_ ? analogRunner_(dialog) : dialog.exec() == QDialog::Accepted;
    if (!accepted || !dialog.isValid()) return false;
    const AnalogSettingsChoice c = dialog.choice();
    return setAnalogRangeForSelection(c.autoRange, c.min, c.max);
}

void WaveWidget::setColorForSelection(std::optional<QColor> color) {
    if (selected_.empty())
        return;
    Snapshot before{model_, selected_, tr("Change color")};
    for (const RowKey& k : selected_) {
        DisplayItem* it = lvd::wave::findItem(model_, k.id);
        if (it == nullptr || it->kind == ItemKind::Divider || it->kind == ItemKind::Group)
            continue;
        if (color)
            it->color = static_cast<std::uint32_t>(color->rgb() & 0xFFFFFFu);
        else
            it->color.reset();
    }
    pushUndo(std::move(before));
    rebuildRows();
    emit structureChanged();
}

bool WaveWidget::groupSelection(const std::string& label) {
    return groupSelected(label);
}

bool WaveWidget::busFromSelection(const std::string& label) {
    std::vector<BusMember> members;
    const DisplayItem* last = nullptr;
    for (const Row& r : rows_) {
        if (!isSelected(r))
            continue;
        if (r.bit >= 0) {
            members.push_back(BusMember{r.item->path, r.bit});
        } else if (r.item->kind == ItemKind::Signal) {
            members.push_back(BusMember{r.item->path, -1});
        } else if (r.item->kind == ItemKind::VirtualBus) {
            for (const BusMember& m : r.item->members)
                members.push_back(m);
        } else {
            continue;
        }
        last = r.item;
    }

    // A whole signal counts as several bits, so one wide signal alone is not a bus.
    std::size_t bits = 0;
    for (const BusMember& m : members) {
        if (m.bit >= 0 || !store_) {
            ++bits;
            continue;
        }
        const SignalId id = store_->findSignal(m.path);
        bits += id == kNoSignal ? 1 : store_->signal(id).width();
    }
    if (members.size() < 2 && bits < 2)
        return false;
    if (members.empty())
        return false;

    Snapshot before{model_, selected_, tr("Make virtual bus")};
    insertAfter(model_, last,
                DisplayModel::makeBus(label.empty() ? "bus" : label, std::move(members), Radix::Hex));
    pushUndo(std::move(before));
    selected_.clear();
    rebuildRows();
    emit selectionChanged();
    emit structureChanged();
    return true;
}

void WaveWidget::removeSelection() {
    const std::vector<ItemId> ids = selectedItemIds();
    if (ids.empty())
        return;
    if (runEdit(tr("Delete"), [&](DisplayModel& m) { return lvd::wave::removeItemsById(m, ids); })) {
        selected_.clear();
        emit selectionChanged();
    }
}

void WaveWidget::toggleGroup(int row) {
    if (row < 0 || row >= rowCount())
        return;
    DisplayItem* it = rows_[static_cast<std::size_t>(row)].item;
    if (it->kind != ItemKind::Group)
        return;
    it->expanded = !it->expanded;
    rebuildRows();
}

void WaveWidget::toggleBits(int row) {
    if (row < 0 || row >= rowCount())
        return;
    const Row& r = rows_[static_cast<std::size_t>(row)];
    if (!r.expandable)
        return;
    r.item->showBits = !r.item->showBits;
    rebuildRows();
}

void WaveWidget::insertDivider(const std::string& label) {
    insertDividerBelow(label);
}

void WaveWidget::promptInsertDivider() {
    bool ok = false;
    const QString text = QInputDialog::getText(this, tr("Add divider"), tr("Label (may be empty):"),
                                               QLineEdit::Normal, {}, &ok);
    if (ok)
        insertDivider(text.toStdString());
}

// ---------------------------------------------------------------------------------------------
// Event markers

double WaveWidget::fsPerUnit() const {
    if (!store_)
        return 1.0;
    return std::pow(10.0, 15 + store_->timescaleExponent());
}

void WaveWidget::setEventMarkers(std::vector<EventMarker> markers) {
    std::stable_sort(markers.begin(), markers.end(),
                     [](const EventMarker& a, const EventMarker& b) { return a.timeFs < b.timeFs; });
    events_ = std::move(markers);
    for (auto& v : eventPrefix_)
        v.assign(events_.size() + 1, 0);
    for (std::size_t i = 0; i < events_.size(); ++i)
        for (int k = 0; k < 4; ++k)
            eventPrefix_[k][i + 1] =
                eventPrefix_[k][i] + (static_cast<int>(events_[i].severity) == k ? 1u : 0u);
    update();
}

void WaveWidget::addEventMarker(EventMarker marker) {
    if (eventPrefix_[0].empty())
        for (auto& v : eventPrefix_)
            v.assign(1, 0);
    if (events_.empty() || marker.timeFs >= events_.back().timeFs) {
        const int sev = static_cast<int>(marker.severity);
        events_.push_back(std::move(marker));
        for (int k = 0; k < 4; ++k)
            eventPrefix_[k].push_back(eventPrefix_[k].back() + (sev == k ? 1u : 0u));
        update();
        return;
    }
    std::vector<EventMarker> all = std::move(events_);
    const auto at = std::upper_bound(all.begin(), all.end(), marker.timeFs,
                                     [](long long t, const EventMarker& m) { return t < m.timeFs; });
    all.insert(at, std::move(marker));
    setEventMarkers(std::move(all));
}

void WaveWidget::clearEventMarkers() {
    events_.clear();
    for (auto& v : eventPrefix_)
        v.assign(1, 0);
    update();
}

std::vector<EventGroup> WaveWidget::eventGroupsBetween(double xFrom, double xTo) const {
    std::vector<EventGroup> out;
    if (!store_ || events_.empty() || eventPrefix_[0].size() != events_.size() + 1)
        return out;
    const double f = fsPerUnit();
    auto fsAtX = [&](double x) { return view_.timeAt(x) * f; };
    auto before = [](const EventMarker& m, double fs) { return static_cast<double>(m.timeFs) < fs; };
    const auto b = events_.begin();
    std::size_t i =
        static_cast<std::size_t>(std::lower_bound(b, events_.end(), fsAtX(std::floor(xFrom)), before) - b);
    const std::size_t end =
        static_cast<std::size_t>(std::lower_bound(b + static_cast<std::ptrdiff_t>(i), events_.end(),
                                                  fsAtX(std::floor(xTo) + 1.0), before) -
                                 b);
    while (i < end) {
        const double xi = view_.xAt(static_cast<double>(events_[i].timeFs) / f);
        // All events up to the start of the next pixel column share this arrow; found by binary search so
        // that a dense burst costs O(log n), not O(events).
        const double nextFs = fsAtX(std::floor(xi) + 1.0);
        std::size_t j =
            static_cast<std::size_t>(std::lower_bound(b + static_cast<std::ptrdiff_t>(i) + 1,
                                                      b + static_cast<std::ptrdiff_t>(end), nextFs, before) -
                                     b);
        j = std::max(j, i + 1);
        EventGroup g;
        g.x = waveLeft() + static_cast<int>(std::lround(xi));
        g.count = static_cast<int>(j - i);
        g.first = i;
        for (int k = 3; k >= 0; --k) {
            if (eventPrefix_[k][j] - eventPrefix_[k][i] > 0) {
                g.severity = static_cast<EventSeverity>(k);
                break;
            }
        }
        out.push_back(g);
        i = j;
    }
    return out;
}

std::vector<EventGroup> WaveWidget::visibleEventGroups() const {
    return eventGroupsBetween(-6.0, static_cast<double>(waveWidth()) + 6.0);
}

std::optional<EventGroup> WaveWidget::eventGroupAt(const QPoint& pos) const {
    if (pos.y() < kRulerHeight || pos.y() >= kRowsTop || pos.x() < waveLeft() ||
        pos.x() >= effectiveWaveRight())
        return std::nullopt;
    const auto groups = eventGroupsBetween(pos.x() - waveLeft() - 6.0, pos.x() - waveLeft() + 6.0);
    std::optional<EventGroup> best;
    int bestDist = 6;
    for (const EventGroup& g : groups) {
        const int d = std::abs(g.x - pos.x());
        if (d < bestDist) {
            bestDist = d;
            best = g;
        }
    }
    return best;
}

QString WaveWidget::eventGroupText(const EventGroup& g) const {
    if (g.first >= events_.size())
        return {};
    const int exp = store_ ? store_->timescaleExponent() : -15;
    const double f = fsPerUnit();
    auto stamp = [&](const EventMarker& m) {
        return QString::fromStdString(formatTime(static_cast<double>(m.timeFs) / f, exp));
    };
    const EventMarker& m0 = events_[g.first];
    if (g.count <= 1)
        return QStringLiteral("%1 at %2: %3").arg(eventSeverityName(m0.severity), stamp(m0), m0.text);
    QString text = QStringLiteral("%1 events from %2 (highest: %3)")
                       .arg(g.count)
                       .arg(stamp(m0), eventSeverityName(g.severity));
    const int shown = std::min(g.count, 6);
    for (int k = 0; k < shown; ++k) {
        const EventMarker& m = events_[g.first + static_cast<std::size_t>(k)];
        text += QStringLiteral("\n%1 %2: %3").arg(eventSeverityName(m.severity), stamp(m), m.text);
    }
    if (g.count > shown)
        text += QStringLiteral("\n... and %1 more").arg(g.count - shown);
    return text;
}

void WaveWidget::showTimeFs(long long fs) {
    const double t = static_cast<double>(fs) / fsPerUnit();
    setCursorTime(0, t);
    setActiveCursor(0);
    ensureVisible(t);
}

bool WaveWidget::event(QEvent* e) {
    if (e->type() == QEvent::ToolTip) {
        const auto* he = static_cast<QHelpEvent*>(e);
        if (const auto g = eventGroupAt(he->pos())) {
            const QString html = QStringLiteral("<p style=\"white-space:pre\">") +
                                 eventGroupText(*g).toHtmlEscaped() + QStringLiteral("</p>");
            QToolTip::showText(he->globalPos(), html, this);
        } else {
            QToolTip::hideText();
            e->ignore();
        }
        return true;
    }
    return QWidget::event(e);
}

// ---------------------------------------------------------------------------------------------
// Navigation

bool WaveWidget::jumpToEdge(int dir) {
    if (dir == 0)
        return false;
    std::vector<const RowSource*> sources;
    for (const Row& r : rows_)
        if (r.source && isSelected(r))
            sources.push_back(r.source.get());
    if (sources.empty())
        for (const Row& r : rows_)
            if (r.source && r.bit < 0)
                sources.push_back(r.source.get());

    const auto cursor = activeCursorTime();
    const double ref = cursor ? *cursor : view_.startTime;
    const Time t = toTime(ref);

    std::optional<double> target;
    for (const RowSource* s : sources) {
        const auto e = edgeFrom(*s, t, dir);
        if (!e)
            continue;
        if (!target || (dir > 0 ? *e < *target : *e > *target))
            target = e;
    }
    if (!target)
        return false;

    if (cursorCount() == 0)
        setCursorTime(0, target);
    else
        setCursorTime(activeCursor_, target);
    ensureVisible(*target);
    return true;
}

void WaveWidget::goToStart() {
    viewPannedByUser();
    const double t = store_ ? static_cast<double>(store_->startTime()) : 0.0;
    setCursorTime(cursorCount() == 0 ? 0 : activeCursor_, t);
    view_.startTime = t;
    clampView();
    emitViewChanged();
}

void WaveWidget::goToEnd() {
    viewPannedByUser();
    const double t = store_ ? static_cast<double>(store_->endTime()) : 0.0;
    setCursorTime(cursorCount() == 0 ? 0 : activeCursor_, t);
    view_.widthPx = waveWidth();
    view_.startTime = std::max(0.0, t - view_.widthPx * 0.9 * view_.timePerPixel);
    clampView();
    emitViewChanged();
}

void WaveWidget::goToTime(double time) {
    viewPannedByUser();
    setCursorTime(cursorCount() == 0 ? 0 : activeCursor_, time);
    view_.widthPx = waveWidth();
    view_.startTime = std::max(0.0, time - view_.widthPx * 0.5 * view_.timePerPixel);
    clampView();
    emitViewChanged();
}

// ---------------------------------------------------------------------------------------------
// Painting

void WaveWidget::resizeEvent(QResizeEvent*) {
    layoutBars();
    updateGpuGeometry();
    view_.widthPx = waveWidth();
    updateScrollBar();
    if (follow_)
        pinToEnd();
    else
        updateHBar();
    update();
}

void WaveWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette_.background);

    if (!store_) {
        p.setPen(palette_.text);
        p.drawText(rect(), Qt::AlignCenter, tr("No waveform loaded"));
        return;
    }

    view_.widthPx = waveWidth();
    const int right = effectiveWaveRight();

    p.fillRect(QRect(0, 0, waveLeft(), height()), QColor(0x1a, 0x1a, 0x1a));

    if (!gpuActive_) {
        p.setPen(palette_.grid);
        for (double t : rulerTicks(view_, 80.0)) {
            const int x = waveLeft() + static_cast<int>(std::lround(view_.xAt(t)));
            if (x >= waveLeft() && x < right)
                p.drawLine(x, kRowsTop, x, rowsBottom());
        }
    }

    if (!gpuActive_) { // no data after the end of the run: a faint tint, the rows draw nothing there
        const int x = noDataLeft();
        if (x < right)
            p.fillRect(QRect(std::max(x, waveLeft() + 1), kRowsTop, right - std::max(x, waveLeft() + 1),
                             rowsBottom() - kRowsTop),
                       noDataTint());
    }

    p.setClipRect(QRect(0, kRowsTop, right, rowsBottom() - kRowsTop));
    for (int i = 0; i < rowCount(); ++i) {
        const int top = rowTop(i);
        const int h = rows_[static_cast<std::size_t>(i)].h;
        if (top + h < kRowsTop || top > rowsBottom())
            continue;
        drawRow(p, i, top);
    }
    p.setClipping(false);

    drawRuler(p);
    if (gpuActive_) {
        canvas_->update();
        overlay_->update();
    } else {
        drawEventMarkers(p);
        drawCursors(p);
    }

    p.setPen(palette_.grid);
    p.drawLine(nameWidth_, 0, nameWidth_, height());
    p.drawLine(waveLeft(), 0, waveLeft(), height());
    if (drag_ == Drag::Rows)
        drawDropIndicator(p);
}

void WaveWidget::drawRuler(QPainter& p) const {
    p.fillRect(QRect(0, 0, width(), kRulerHeight), QColor(0x20, 0x20, 0x20));
    p.setPen(palette_.text);
    p.drawText(QRect(6, 0, nameWidth_ - 8, kRulerHeight), Qt::AlignVCenter | Qt::AlignLeft, tr("Signal"));
    p.drawText(QRect(nameWidth_ + 6, 0, valueWidth_ - 8, kRulerHeight), Qt::AlignVCenter | Qt::AlignLeft,
               tr("Value"));

    const int right = effectiveWaveRight();
    p.setClipRect(QRect(waveLeft(), 0, right - waveLeft(), kRulerHeight));
    const int exp = store_->timescaleExponent();
    const QFontMetrics fm(font());
    for (double t : rulerTicks(view_, 80.0)) {
        const int x = waveLeft() + static_cast<int>(std::lround(view_.xAt(t)));
        p.setPen(palette_.text);
        p.drawLine(x, 2, x, kRulerHeight - fm.height() - 4);
        p.drawText(x + 3, fm.ascent() + 3, QString::fromStdString(formatTime(t, exp)));
    }
    p.setClipping(false);
    p.setPen(palette_.grid);
    p.drawLine(0, kRulerHeight - 1, width(), kRulerHeight - 1);

    // The band below the ruler: event arrows and cursor delta labels, never rows.
    p.fillRect(QRect(0, kRulerHeight, width(), kBandHeight), QColor(0x17, 0x17, 0x1b));
    p.setPen(palette_.text.darker(150));
    p.drawText(QRect(6, kRulerHeight, nameWidth_ - 8, kBandHeight), Qt::AlignVCenter | Qt::AlignLeft,
               tr("Reports and cursor deltas"));
    p.setPen(palette_.grid);
    p.drawLine(0, kRowsTop - 1, width(), kRowsTop - 1);
}

void WaveWidget::drawEventMarkers(QPainter& p) const {
    if (!store_ || events_.empty())
        return;
    const int right = effectiveWaveRight();
    p.save();
    p.setClipRect(QRect(waveLeft() + 1, kRulerHeight, std::max(0, right - waveLeft() - 1), kBandHeight));
    p.setRenderHint(QPainter::Antialiasing, true);
    const int base = kRulerHeight + 4;
    const int tip = kRowsTop - 3;
    for (const EventGroup& g : visibleEventGroups()) {
        const QColor c = eventColor(g.severity);
        QPolygon tri;
        tri << QPoint(g.x - 4, base) << QPoint(g.x + 4, base) << QPoint(g.x, tip);
        p.setBrush(c);
        p.setPen(g.count > 1 ? QColor(0xf0, 0xf0, 0xf0)
                             : c.darker(150)); // a merged arrow has a light outline
        p.drawPolygon(tri);
    }
    p.restore();
}

void WaveWidget::drawCursors(QPainter& p) const {
    if (cursors_.empty())
        return;
    const int right = effectiveWaveRight();
    const int exp = store_->timescaleExponent();
    const QFontMetrics fm(font());
    p.setClipRect(QRect(waveLeft(), 0, right - waveLeft(), rowsBottom()));

    std::vector<int> xs(cursors_.size());
    for (int c = 0; c < cursorCount(); ++c) {
        const int x =
            waveLeft() + static_cast<int>(std::lround(view_.xAt(cursors_[static_cast<std::size_t>(c)])));
        xs[static_cast<std::size_t>(c)] = x;
        const QColor col = kCursorColors[c % 8];
        p.setPen(col);
        p.drawLine(x, kRulerHeight, x, height()); // through the event band and the rows

        QString label = QString::fromStdString(formatTime(cursors_[static_cast<std::size_t>(c)], exp));
        if (cursorCount() > 1)
            label = QStringLiteral("C%1  ").arg(c + 1) + label;
        const QRect tag(x + 2, kRulerHeight - fm.height() - 2, fm.horizontalAdvance(label) + 6, fm.height());
        p.fillRect(tag, c == activeCursor_ ? QColor(0x50, 0x50, 0x50) : QColor(0x20, 0x20, 0x20));
        p.setPen(col);
        p.drawText(tag, Qt::AlignCenter, label);
        if (c == activeCursor_) { // marker on the ruler edge
            QPolygon tri;
            tri << QPoint(x - 4, kRulerHeight - 1) << QPoint(x + 4, kRulerHeight - 1)
                << QPoint(x, kRulerHeight - 6);
            p.setBrush(col);
            p.drawPolygon(tri);
            p.setBrush(Qt::NoBrush);
        }
    }

    // Time differences between neighbouring cursors.
    std::vector<int> order(cursors_.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        order[i] = static_cast<int>(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return cursors_[static_cast<std::size_t>(a)] < cursors_[static_cast<std::size_t>(b)];
    });
    for (std::size_t k = 0; k + 1 < order.size(); ++k) {
        const int a = order[k], b = order[k + 1];
        const double delta = cursors_[static_cast<std::size_t>(b)] - cursors_[static_cast<std::size_t>(a)];
        const int x0 = xs[static_cast<std::size_t>(a)], x1 = xs[static_cast<std::size_t>(b)];
        const int y =
            kRulerHeight + kBandHeight / 2; // the delta labels live in the event band, above the rows
        p.setPen(palette_.text);
        p.drawLine(x0, y, x1, y);
        p.drawLine(x0, y, x0 + 5, y - 3);
        p.drawLine(x0, y, x0 + 5, y + 3);
        p.drawLine(x1, y, x1 - 5, y - 3);
        p.drawLine(x1, y, x1 - 5, y + 3);

        const QString label = QStringLiteral("Δ ") + QString::fromStdString(formatTime(delta, exp));
        const int lw = fm.horizontalAdvance(label) + 10;
        int cx = (x0 + x1) / 2 - lw / 2;
        cx = std::clamp(cx, waveLeft() + 2,
                        std::max(waveLeft() + 2, right - lw - 2)); // keep readable when zoomed in
        const QRect box(cx, kRulerHeight + 1, lw, kBandHeight - 2);
        p.fillRect(box, QColor(0x20, 0x20, 0x20, 230));
        p.drawRect(box);
        p.drawText(box, Qt::AlignCenter, label);
    }
    p.setClipping(false);
}

void WaveWidget::drawRow(QPainter& p, int rowIndex, int top) {
    Row& row = rows_[static_cast<std::size_t>(rowIndex)];
    const DisplayItem& it = *row.item;
    const int right = effectiveWaveRight();
    const QFontMetrics fm(font());

    if (isSelected(row))
        p.fillRect(QRect(0, top, right, row.h), palette_.selection);
    p.setPen(QColor(0x28, 0x28, 0x28));
    p.drawLine(0, top + row.h - 1, right, top + row.h - 1);

    // Name column.
    p.save();
    p.setClipRect(QRect(0, top, nameWidth_ - 1, row.h));
    int x = 6 + row.depth * 14;
    const bool hasTriangle = it.kind == ItemKind::Group || row.expandable;
    if (hasTriangle) {
        const bool open = it.kind == ItemKind::Group ? it.expanded : it.showBits;
        p.setPen(palette_.text);
        p.setBrush(palette_.text);
        const int cy = top + kRowHeight / 2;
        QPolygon tri;
        if (open)
            tri << QPoint(x, cy - 3) << QPoint(x + 8, cy - 3) << QPoint(x + 4, cy + 3);
        else
            tri << QPoint(x + 1, cy - 4) << QPoint(x + 1, cy + 4) << QPoint(x + 6, cy);
        p.drawPolygon(tri);
        p.setBrush(Qt::NoBrush);
    }
    if (it.kind != ItemKind::Divider)
        x += 14;

    const bool missing = !row.source && (it.kind == ItemKind::Signal || it.kind == ItemKind::VirtualBus);
    QColor nameColor = palette_.text;
    if (it.kind == ItemKind::Divider)
        nameColor = palette_.dense;
    else if (it.color)
        nameColor = toQColor(*it.color);
    if (missing)
        nameColor = palette_.unknown;
    p.setPen(nameColor);
    const QString name =
        fm.elidedText(QString::fromStdString(nameOf(row)), Qt::ElideLeft, nameWidth_ - x - 6);
    p.drawText(QRect(x, top, nameWidth_ - x - 4, kRowHeight), Qt::AlignVCenter | Qt::AlignLeft, name);
    p.restore();

    // Value column.
    if (row.source) {
        p.save();
        p.setClipRect(QRect(nameWidth_ + 1, top, valueWidth_ - 2, row.h));
        p.setPen(palette_.bus);
        const QString v =
            fm.elidedText(QString::fromStdString(rowValueText(rowIndex)), Qt::ElideRight, valueWidth_ - 10);
        p.drawText(QRect(nameWidth_ + 6, top, valueWidth_ - 8, kRowHeight), Qt::AlignVCenter | Qt::AlignLeft,
                   v);
        p.restore();
    }

    // Wave area.
    if (gpuActive_)
        return; // drawn by the GPU canvas, labels by the overlay
    if (it.kind == ItemKind::Divider) {
        p.setPen(palette_.dense);
        p.drawLine(waveLeft(), top + kRowHeight / 2, right, top + kRowHeight / 2);
        return;
    }
    if (!row.source)
        return;

    p.save();
    p.setClipRect(QRect(waveLeft() + 1, top, right - waveLeft() - 1, row.h));
    const double tpp = view_.timePerPixel;
    const double tileTime = IRenderer::kTileWidth * tpp;
    const long long k0 = static_cast<long long>(std::floor(view_.startTime / tileTime));
    const long long k1 = static_cast<long long>(std::floor(view_.endTime() / tileTime));
    for (long long k = k0; k <= k1; ++k) {
        TileKey key;
        key.row = row.cacheKey;
        key.revision = row.revision;
        key.scaleBits = scaleBitsOf(tpp);
        key.tile = k;
        key.heightPx = row.h;

        const QImage* img = cache_.find(key);
        if (img == nullptr) {
            TileRequest req;
            req.source = row.source.get();
            req.style = row.style;
            req.timePerPixel = tpp;
            req.tileIndex = k;
            req.widthPx = IRenderer::kTileWidth;
            req.heightPx = row.h;
            req.validEnd = dataEnd();
            cache_.put(key, renderer_->renderTile(req));
            img = cache_.find(key);
        }
        const int tx =
            waveLeft() +
            static_cast<int>(std::lround((static_cast<double>(k) * tileTime - view_.startTime) / tpp));
        p.drawImage(tx, top, *img);
    }
    drawBusLabels(p, row, top);
    p.restore();
}

void WaveWidget::drawBusLabels(QPainter& p, const Row& row, int top) const {
    const RowSource& src = *row.source;
    if (src.waveClass() != WaveClass::Bus)
        return;

    const double t0 = view_.startTime, t1 = view_.endTime();
    const auto [lo, hi] = src.changesIn(toTime(t0), toTime(t1) + 1);
    if (hi - lo > 800)
        return; // far too dense for any segment to hold text

    struct Seg {
        double a, b;
        std::size_t change;
    };
    std::vector<Seg> segs;
    const double tail =
        static_cast<double>(dataEnd()); // the last value is held up to the end of the data only
    const std::size_t n = src.count();
    if (lo > 0)
        segs.push_back({t0, lo < n ? static_cast<double>(src.time(lo)) : tail, lo - 1});
    for (std::size_t i = lo; i < hi; ++i)
        segs.push_back(
            {static_cast<double>(src.time(i)), i + 1 < n ? static_cast<double>(src.time(i + 1)) : tail, i});

    const QFontMetrics fm(font());
    p.setPen(row.style.hasColor ? row.style.color : palette_.text);
    for (const Seg& s : segs) {
        const double xa = std::max(s.a, t0), xb = std::min(s.b, t1);
        const double x0 = waveLeft() + view_.xAt(xa), x1 = waveLeft() + view_.xAt(xb);
        const double w = x1 - x0;
        if (w < 22.0)
            continue;
        const QString text = fm.elidedText(QString::fromStdString(src.text(s.change, row.item->radix)),
                                           Qt::ElideRight, static_cast<int>(w) - 10);
        p.drawText(QRectF(x0, top, w, kRowHeight), Qt::AlignCenter, text);
    }
}

// ---------------------------------------------------------------------------------------------
// Input

void WaveWidget::mousePressEvent(QMouseEvent* e) {
    setFocus();
    const QPoint pos = e->position().toPoint();
    lastMouse_ = pos;

    if (e->button() == Qt::MiddleButton) {
        drag_ = Drag::Pan;
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;

    if (std::abs(pos.x() - nameWidth_) <= 3) {
        drag_ = Drag::NameSplitter;
        return;
    }
    if (std::abs(pos.x() - waveLeft()) <= 3) {
        drag_ = Drag::ValueSplitter;
        return;
    }

    const bool shift = e->modifiers() & Qt::ShiftModifier;
    const bool ctrl = e->modifiers() & Qt::ControlModifier;

    if (pos.x() >= waveLeft()) {
        if (const auto g = eventGroupAt(pos)) { // an event arrow: cursor 1 goes there, no drag
            showTimeFs(events_[g->first].timeFs);
            return;
        }
        const double t = view_.timeAt(pos.x() - waveLeft());
        const int near = cursorNear(pos.x());
        if (near >= 0 && !shift) {
            setActiveCursor(near);
            dragCursor_ = near;
        } else if (shift) {
            int idx = addCursor(t);
            dragCursor_ = idx >= 0 ? idx : activeCursor_;
        } else {
            if (cursorCount() == 0)
                addCursor(t);
            else
                setCursorTime(activeCursor_, t);
            dragCursor_ = activeCursor_;
        }
        drag_ = Drag::Cursor;
        const int row = rowAtY(pos.y());
        if (row >= 0 && pos.y() >= kRowsTop && !shift)
            selectRow(row, ctrl);
        return;
    }

    const int row = rowAtY(pos.y());
    if (row < 0)
        return;
    const Row& r = rows_[static_cast<std::size_t>(row)];
    const int triX = 6 + r.depth * 14;
    if ((r.item->kind == ItemKind::Group || r.expandable) && pos.x() >= triX && pos.x() <= triX + 12) {
        if (r.item->kind == ItemKind::Group)
            toggleGroup(row);
        else
            toggleBits(row);
        return;
    }
    rowPressPending_ = false;
    deferSelect_ = false;
    if (shift && anchorRow_ >= 0) {
        selectRange(anchorRow_, row);
    } else if (ctrl) {
        selectRow(row, true);
    } else {
        // A plain press on a selected row keeps the selection: it may be the start of dragging all of it.
        deferSelect_ = isSelected(r);
        if (!deferSelect_)
            selectRow(row);
        pressRow_ = row;
        pressPos_ = pos;
        rowPressPending_ = true;
    }
}

void WaveWidget::mouseMoveEvent(QMouseEvent* e) {
    const QPoint pos = e->position().toPoint();
    switch (drag_) {
    case Drag::Cursor:
        setCursorTime(dragCursor_, view_.timeAt(pos.x() - waveLeft()));
        break;
    case Drag::Pan:
        viewPannedByUser();
        view_.panPixels(-(pos.x() - lastMouse_.x()));
        clampView();
        emitViewChanged();
        break;
    case Drag::NameSplitter:
        nameWidth_ = std::clamp(pos.x(), 80, std::max(80, width() / 2));
        if (editor_ != nullptr)
            positionEditor();
        layoutBars();
        updateGpuGeometry();
        view_.widthPx = waveWidth();
        if (follow_)
            pinToEnd();
        else
            updateHBar();
        update();
        break;
    case Drag::ValueSplitter:
        valueWidth_ = std::clamp(pos.x() - nameWidth_, 40, std::max(40, width() / 3));
        layoutBars();
        updateGpuGeometry();
        view_.widthPx = waveWidth();
        if (follow_)
            pinToEnd();
        else
            updateHBar();
        update();
        break;
    case Drag::Rows:
        updateRowDrag(pos);
        break;
    case Drag::None:
        if (rowPressPending_ && (pos - pressPos_).manhattanLength() >= QApplication::startDragDistance()) {
            if (startRowDrag()) {
                updateRowDrag(pos);
                break;
            }
            rowPressPending_ = false;
        }
        if (std::abs(pos.x() - nameWidth_) <= 3 || std::abs(pos.x() - waveLeft()) <= 3)
            setCursor(Qt::SplitHCursor);
        else if (eventGroupAt(pos))
            setCursor(Qt::PointingHandCursor);
        else if (pos.x() > waveLeft() && cursorNear(pos.x()) >= 0)
            setCursor(Qt::SizeHorCursor);
        else
            setCursor(Qt::ArrowCursor);
        break;
    }
    lastMouse_ = pos;
}

void WaveWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (drag_ == Drag::Rows) {
        finishRowDrag(e->position().toPoint());
    } else if (rowPressPending_ && deferSelect_ && pressRow_ >= 0 && pressRow_ < rowCount()) {
        selectRow(pressRow_); // a click on one of several selected rows selects just that row
    }
    rowPressPending_ = false;
    deferSelect_ = false;
    if (drag_ != Drag::Rows)
        drag_ = Drag::None;
}

void WaveWidget::mouseDoubleClickEvent(QMouseEvent* e) {
    const QPoint pos = e->position().toPoint();
    const int row = rowAtY(pos.y());
    if (row < 0 || pos.x() >= waveLeft())
        return;
    const ItemKind kind = rows_[static_cast<std::size_t>(row)].item->kind;
    if (kind == ItemKind::Group)
        toggleGroup(row);
    else if (kind == ItemKind::Divider)
        beginRename(row);
    else
        toggleBits(row);
}

void WaveWidget::wheelEvent(QWheelEvent* e) {
    const int dy = e->angleDelta().y();
    const int dx = e->angleDelta().x();
    if (e->modifiers() & Qt::ControlModifier) {
        autoFit_ = false;
        const double factor = std::pow(1.25, dy / 120.0);
        view_.widthPx = waveWidth();
        view_.zoomAt(std::clamp(e->position().x() - waveLeft(), 0.0, static_cast<double>(view_.widthPx)),
                     factor);
        clampView();
        if (follow_)
            pinToEnd(); // following: the end stays at the right edge, not the point under the pointer
        emitViewChanged();
    } else if ((e->modifiers() & Qt::ShiftModifier) || dx != 0) {
        viewPannedByUser();
        view_.panPixels(-((dx != 0 ? dx : dy) / 120.0) * 60.0);
        clampView();
        emitViewChanged();
    } else {
        vbar_->setValue(vbar_->value() - dy / 120 * 3 * kRowHeight);
    }
    e->accept();
}

void WaveWidget::keyPressEvent(QKeyEvent* e) {
    const bool ctrl = e->modifiers() & Qt::ControlModifier;
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    switch (e->key()) {
    case Qt::Key_Escape:
        if (drag_ == Drag::Rows) {
            cancelRowDrag();
            return;
        }
        break;
    case Qt::Key_F2:
        if (!ctrl && !shift)
            beginRename();
        return;
    case Qt::Key_Up:
    case Qt::Key_Down: {
        const int dir = e->key() == Qt::Key_Down ? 1 : -1;
        if (ctrl && shift)
            moveSelectedRowsToEdge(dir < 0);
        else if (ctrl)
            moveSelectedRows(dir);
        else if (rowCount() > 0) { // plain arrows walk the selection through the rows
            const int from = anchorRow_ >= 0 ? anchorRow_ : (dir > 0 ? -1 : rowCount());
            const int to = std::clamp(from + dir, 0, rowCount() - 1);
            if (shift && anchorRow_ >= 0) {
                selectRange(anchorRow_, to);
                ensureRowVisible(to);
            } else {
                selectRow(to);
                ensureRowVisible(to);
            }
        }
        return;
    }
    case Qt::Key_Z:
        if (ctrl) {
            if (shift)
                redo();
            else
                undo();
            return;
        }
        break;
    case Qt::Key_Y:
        if (ctrl) {
            redo();
            return;
        }
        break;
    case Qt::Key_Left:
    case Qt::Key_Right: {
        const int dir = e->key() == Qt::Key_Right ? 1 : -1;
        if (ctrl) {
            viewPannedByUser();
            view_.panPixels(dir * view_.widthPx / 4.0);
            clampView();
            emitViewChanged();
        } else {
            jumpToEdge(dir);
        }
        return;
    }
    case Qt::Key_F:
        zoomFull();
        return;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        zoomBy(1.5);
        return;
    case Qt::Key_Minus:
        zoomBy(1.0 / 1.5);
        return;
    case Qt::Key_Delete:
        removeSelection();
        return;
    case Qt::Key_G:
        if (ctrl && shift)
            ungroupSelected();
        else if (ctrl)
            groupSelected();
        return;
    case Qt::Key_B:
        if (ctrl)
            busFromSelection();
        return;
    case Qt::Key_Home:
        goToStart();
        return;
    case Qt::Key_End:
        goToEnd();
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(e);
}

void WaveWidget::contextMenuEvent(QContextMenuEvent* e) {
    const int row = rowAtY(e->pos().y());
    QMenu* menu = createContextMenu(row);
    menu->exec(e->globalPos());
    delete menu;
}

QMenu* WaveWidget::createContextMenu(int row) {
    if (row >= 0 && row < rowCount() && !isSelected(rows_[static_cast<std::size_t>(row)]))
        selectRow(row);
    if (row < 0 || row >= rowCount())
        row = -1;

    QMenu* menu = new QMenu(this);
    menu->setObjectName("waveContextMenu");
    QMenu* radix = menu->addMenu(tr("Radix"));
    for (Radix r : {Radix::Binary, Radix::Octal, Radix::Hex, Radix::Unsigned, Radix::Signed, Radix::Ascii,
                    Radix::Default}) {
        QAction* a = radix->addAction(QString::fromLatin1(radixName(r)));
        connect(a, &QAction::triggered, this, [this, r] { setRadixForSelection(r); });
    }

    QMenu* color = menu->addMenu(tr("Color"));
    struct Swatch {
        const char* name;
        QColor c;
    };
    const Swatch swatches[] = {{"Green", QColor(0x40, 0xe0, 0x40)},  {"Cyan", QColor(0x40, 0xd0, 0xe0)},
                               {"Yellow", QColor(0xff, 0xe0, 0x40)}, {"Orange", QColor(0xff, 0xa0, 0x30)},
                               {"Red", QColor(0xff, 0x60, 0x60)},    {"Magenta", QColor(0xff, 0x70, 0xff)},
                               {"Blue", QColor(0x70, 0x90, 0xff)},   {"White", QColor(0xf0, 0xf0, 0xf0)},
                               {"Gray", QColor(0x90, 0x90, 0x90)}};
    for (const Swatch& s : swatches) {
        QPixmap pix(14, 14);
        pix.fill(s.c);
        QAction* a = color->addAction(QIcon(pix), tr(s.name));
        const QColor c = s.c;
        connect(a, &QAction::triggered, this, [this, c] { setColorForSelection(c); });
    }
    color->addSeparator();
    connect(color->addAction(tr("Custom...")), &QAction::triggered, this, [this, row] {
        const QColor initial = (row >= 0 && rowColor(row)) ? *rowColor(row) : palette_.bus;
        const QColor c = QColorDialog::getColor(initial, this, tr("Signal color"));
        if (c.isValid())
            setColorForSelection(c);
    });
    connect(color->addAction(tr("Default")), &QAction::triggered, this,
            [this] { setColorForSelection(std::nullopt); });

    QAction* analog = menu->addAction(tr("Analog trace"));
    connect(analog, &QAction::triggered, this, [this] { toggleAnalogForSelection(); });
    QAction* analogSettings = menu->addAction(tr("Analog Settings..."));
    analogSettings->setObjectName("analogSettings");
    analogSettings->setEnabled(analogSelectionAvailable());
    analogSettings->setToolTip(tr("Auto or manual (Min, Max) range of the selected analog traces"));
    connect(analogSettings, &QAction::triggered, this, [this, row] { showAnalogSettings(row); });
    if (row >= 0 && rows_[static_cast<std::size_t>(row)].expandable) {
        QAction* bits = menu->addAction(
            rows_[static_cast<std::size_t>(row)].item->showBits ? tr("Hide bits") : tr("Show bits"));
        connect(bits, &QAction::triggered, this, [this, row] { toggleBits(row); });
    }
    menu->addSeparator();
    populateArrangeMenu(menu, false);
    menu->addSeparator();
    connect(menu->addAction(tr("Make virtual bus from selection\tCtrl+B")), &QAction::triggered, this,
            [this] { busFromSelection(); });
    connect(menu->addAction(tr("Add divider...")), &QAction::triggered, this,
            [this] { promptInsertDivider(); });
    if (row >= 0 && rows_[static_cast<std::size_t>(row)].item->kind == ItemKind::Group) {
        menu->addSeparator();
        connect(menu->addAction(tr("Expand / collapse")), &QAction::triggered, this,
                [this, row] { toggleGroup(row); });
    }
    return menu;
}

// ---------------------------------------------------------------------------------------------
// Live data

void WaveWidget::dataChanged() {
    if (!store_)
        return;
    if (canvas_ != nullptr)
        canvas_->requestCacheClear();
    ++storeRevision_; // makes every cached tile unreachable
    cache_.clear();
    busCache_.clear();
    bitCache_.clear();
    for (auto& [key, idx] : indexes_)
        idx->update(store_->signal(key.first));

    view_.widthPx = waveWidth();
    const double end = static_cast<double>(store_->endTime());
    const bool wasAtEnd = view_.endTime() >= lastEnd_ - view_.timePerPixel * 2.0;
    rebuildRows();

    if (follow_ && !autoFit_) {
        view_.startTime = std::max(0.0, end - view_.widthPx * view_.timePerPixel);
    } else if (autoFit_) {
        view_.fit(static_cast<double>(store_->startTime()), end);
    } else if (wasAtEnd && end > view_.endTime()) {
        view_.startTime = std::max(0.0, end - view_.widthPx * view_.timePerPixel);
    }
    lastEnd_ = end;
    clampView();
    emitViewChanged();
}

std::size_t WaveWidget::addWaveSignals(const AddWaveRequest& request) {
    if (!store_)
        return 0;
    const std::size_t n = applyAddWave(model_, *store_, request);
    clearUndoHistory();
    selected_.clear();
    rebuildRows();
    emit selectionChanged();
    if (n != 0)
        emit structureChanged();
    return n;
}

std::string WaveWidget::doScript() const {
    return exportDoScript(model_);
}

ImportResult WaveWidget::loadDoScript(const std::string& script) {
    ImportResult none;
    if (!store_)
        return none;
    const ImportResult r = importDoScript(script, model_, *store_);
    clearUndoHistory();
    selected_.clear();
    rebuildRows();
    emit selectionChanged();
    if (r.rowsAdded != 0)
        emit structureChanged();
    return r;
}

void WaveWidget::clearWave() {
    cancelRename();
    model_.root().children.clear();
    clearUndoHistory();
    selected_.clear();
    rebuildRows();
    emit selectionChanged();
    emit structureChanged();
}

// ---------------------------------------------------------------------------------------------
// GPU frame

void WaveWidget::paintOverlay(QPainter& p) const {
    if (!store_)
        return;
    const int right = effectiveWaveRight();
    p.save();
    p.setClipRect(
        QRect(waveLeft() + 1, kRowsTop, std::max(0, right - waveLeft() - 1), rowsBottom() - kRowsTop));
    for (int i = 0; i < rowCount(); ++i) {
        const Row& row = rows_[static_cast<std::size_t>(i)];
        const int top = rowTop(i);
        if (!row.source || top + row.h < kRowsTop || top > rowsBottom())
            continue;
        drawBusLabels(p, row, top);
    }
    p.restore();
    drawEventMarkers(p);
    drawCursors(p);
}

GpuFrame WaveWidget::buildGpuFrame(const QSize& size) {
    GpuFrame f;
    f.size = size;
    f.background = palette_.background;
    f.tileClip = QRect(QPoint(0, 0), size);
    if (!store_)
        return f;

    const int W = size.width(), H = size.height();
    ViewState view = view_;
    view.widthPx = W;

    // No data after the end of the run: a faint tint behind everything else.
    {
        const int x = std::max(0, noDataLeft() - waveLeft());
        if (x < W)
            f.rects.push_back({static_cast<float>(x), 0, static_cast<float>(W - x), static_cast<float>(H),
                               Rgba::from(noDataTint())});
    }

    // The grid first, as in the QPainter path, and the border of the wave area.
    for (double t : rulerTicks(view, 80.0)) {
        const int x = static_cast<int>(std::lround(view.xAt(t)));
        if (x >= 0 && x < W)
            f.rects.push_back(
                {static_cast<float>(x), 0, 1, static_cast<float>(H), Rgba::from(palette_.grid)});
    }
    f.rects.push_back({0, 0, 1, static_cast<float>(H), Rgba::from(palette_.grid)});

    auto requests = std::make_shared<std::unordered_map<TileKey, TileRequest, TileKeyHash>>();
    const double tpp = view_.timePerPixel;
    const double tileTime = IRenderer::kTileWidth * tpp;
    const long long k0 = static_cast<long long>(std::floor(view_.startTime / tileTime));
    const long long k1 = static_cast<long long>(std::floor(view_.endTime() / tileTime));

    for (const Row& row : rows_) {
        const int top = row.y - scrollY_; // relative to the canvas, which starts at kRowsTop
        if (top + row.h < 0 || top > H)
            continue;

        if (isSelected(row))
            f.rects.push_back({0, static_cast<float>(top), static_cast<float>(W), static_cast<float>(row.h),
                               Rgba::from(palette_.selection)});
        f.rects.push_back(
            {0, static_cast<float>(top + row.h - 1), static_cast<float>(W), 1, Rgba{0x28, 0x28, 0x28, 255}});
        if (row.item->kind == ItemKind::Divider) {
            f.rects.push_back({0, static_cast<float>(top + kRowHeight / 2), static_cast<float>(W), 1,
                               Rgba::from(palette_.dense)});
            continue;
        }
        if (!row.source)
            continue;

        for (long long k = k0; k <= k1; ++k) {
            TileKey key;
            key.row = row.cacheKey;
            key.revision = row.revision;
            key.scaleBits = scaleBitsOf(tpp);
            key.tile = k;
            key.heightPx = row.h;

            GpuTileDraw d;
            d.key = key;
            d.heightPx = row.h;
            d.x =
                static_cast<float>(std::lround((static_cast<double>(k) * tileTime - view_.startTime) / tpp));
            d.y = static_cast<float>(top);
            f.tiles.push_back(d);

            TileRequest req;
            req.source = row.source.get();
            req.style = row.style;
            req.timePerPixel = tpp;
            req.tileIndex = k;
            req.widthPx = IRenderer::kTileWidth;
            req.heightPx = row.h;
            req.validEnd = dataEnd();
            requests->emplace(key, req);
        }
    }

    const Palette pal = palette_;
    f.geometryFor = [requests, pal](const GpuTileDraw& d) {
        const auto it = requests->find(d.key);
        return it == requests->end() ? TileGeometry{} : buildTileGeometry(it->second, pal);
    };
    return f;
}

QImage WaveWidget::snapshot() {
    // The overlay is painted separately below, above the GPU image, so keep it out of the plain grab.
    const bool overlayShown = overlay_ != nullptr && overlay_->isVisible();
    if (overlayShown)
        overlay_->hide();
    QImage img = grab().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (gpuActive_ && canvas_ != nullptr && canvas_->isReady()) {
        const QImage gpu = canvas_->grabFramebuffer();
        QPainter p(&img);
        p.drawImage(canvas_->pos(), gpu);
        QPixmap overlay(size());
        overlay.fill(Qt::transparent);
        overlay_->render(&overlay);
        p.drawPixmap(0, 0, overlay);
    }
    if (overlayShown)
        overlay_->show();
    return img;
}

} // namespace lvd::gui
