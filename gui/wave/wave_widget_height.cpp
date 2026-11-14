// SPDX-License-Identifier: GPL-3.0-or-later
// Row heights of a WaveWidget: the "Height..." dialog, resizing a row by its bottom border, Alt+Up /
// Alt+Down. The height lives in the display model (DisplayItem::heightPx), so undo, the layout file and the
// `add wave` script carry it; rebuildRows() turns it into the geometry of the rows.
#include "wave/row_height_dialog.hpp"
#include "wave/wave_widget.hpp"

#include <QSpinBox>

#include <algorithm>

namespace lvd::gui {

using namespace lvd::wave;

namespace {

/// Items that can have their own height: every row kind except group headers.
bool takesHeight(const DisplayItem& it) {
    return it.kind != ItemKind::Group;
}

} // namespace

int WaveWidget::rowHeightAt(int row) const {
    return rows_.at(static_cast<std::size_t>(row)).h;
}

bool WaveWidget::heightSelectionAvailable() const {
    for (const DisplayItem* it : selectedItems())
        if (takesHeight(*it))
            return true;
    return false;
}

bool WaveWidget::setRowHeightForSelection(int px) {
    lastEditError_.clear();
    const int target = clampRowHeight(px);
    std::vector<DisplayItem*> items;
    for (DisplayItem* it : selectedItems())
        if (takesHeight(*it) && it->heightPx != target)
            items.push_back(it);
    if (items.empty()) {
        lastEditError_ = heightSelectionAvailable() ? tr("The selected rows already have this height")
                                                    : tr("No row that can be resized is selected");
        emit editRejected(lastEditError_);
        return false;
    }
    Snapshot before{model_, selected_,
                    target == 0 ? tr("Default row height") : tr("Row height %1 px").arg(target)};
    for (DisplayItem* it : items)
        it->heightPx = target;
    pushUndo(std::move(before));
    rebuildRows();
    emit structureChanged();
    return true;
}

bool WaveWidget::adjustRowHeightForSelection(int delta) {
    lastEditError_.clear();
    std::vector<std::pair<DisplayItem*, int>> changes;
    for (const Row& r : rows_) {
        if (r.bit >= 0 || !takesHeight(*r.item) || !isSelected(r))
            continue;
        const int h = std::clamp(r.h + delta, kMinRowHeightPx, kMaxRowHeightPx);
        if (h != r.h)
            changes.emplace_back(r.item, h);
    }
    if (changes.empty())
        return false;
    Snapshot before{model_, selected_, delta > 0 ? tr("Taller rows") : tr("Lower rows")};
    for (auto& [it, h] : changes)
        it->heightPx = h;
    pushUndo(std::move(before));
    rebuildRows();
    emit structureChanged();
    return true;
}

bool WaveWidget::showRowHeightDialog(int row) {
    int primary = -1;
    if (row >= 0 && row < rowCount() && rows_[static_cast<std::size_t>(row)].bit < 0 &&
        isSelected(rows_[static_cast<std::size_t>(row)]) &&
        takesHeight(*rows_[static_cast<std::size_t>(row)].item))
        primary = row;
    for (int i = 0; primary < 0 && i < rowCount(); ++i) {
        const Row& r = rows_[static_cast<std::size_t>(i)];
        if (r.bit < 0 && isSelected(r) && takesHeight(*r.item))
            primary = i;
    }
    if (primary < 0)
        return false;
    RowHeightDialog dialog(rows_[static_cast<std::size_t>(primary)].h, this);
    const bool accepted = heightRunner_ ? heightRunner_(dialog) : dialog.exec() == QDialog::Accepted;
    if (!accepted)
        return false;
    return setRowHeightForSelection(dialog.resetToDefault() ? 0 : dialog.heightPx());
}

int WaveWidget::rowBorderAt(const QPoint& pos) const {
    if (pos.x() < 0 || pos.x() >= waveLeft() || pos.y() < kRowsTop || pos.y() >= rowsBottom())
        return -1;
    // The border is the last pixel line of a row: the grip is that line, the 3 lines above it and the first
    // line of the next row (a click further into the next row selects it, as before).
    for (int dy : {0, -1}) {
        const int row = rowAtY(pos.y() + dy);
        if (row < 0)
            continue;
        const Row& r = rows_[static_cast<std::size_t>(row)];
        const int bottom = rowTop(row) + r.h - 1;
        if (pos.y() >= bottom - 3 && pos.y() <= bottom + 1)
            return r.bit < 0 && takesHeight(*r.item) ? row : -1;
    }
    return -1;
}

bool WaveWidget::beginRowResize(int row, int y) {
    if (row < 0 || row >= rowCount())
        return false;
    const Row& r = rows_[static_cast<std::size_t>(row)];
    if (r.bit >= 0 || !takesHeight(*r.item))
        return false;
    resizeId_ = r.item->id;
    resizeStartY_ = y;
    resizeStartH_ = r.h;
    resizeBefore_ = Snapshot{model_, selected_, tr("Resize row")};
    drag_ = Drag::RowHeight;
    setCursor(Qt::SizeVerCursor);
    return true;
}

void WaveWidget::updateRowResize(int y) {
    if (drag_ != Drag::RowHeight)
        return;
    DisplayItem* it = findItem(model_, resizeId_);
    if (it == nullptr) {
        cancelRowResize();
        return;
    }
    const int h = std::clamp(resizeStartH_ + (y - resizeStartY_), kMinRowHeightPx, kMaxRowHeightPx);
    if (it->heightPx == h)
        return;
    it->heightPx = h;
    rebuildRows(); // the rows below move with the border while the mouse moves
}

bool WaveWidget::finishRowResize() {
    if (drag_ != Drag::RowHeight)
        return false;
    drag_ = Drag::None;
    setCursor(Qt::ArrowCursor);
    std::optional<Snapshot> before = std::move(resizeBefore_);
    resizeBefore_.reset();
    const DisplayItem* it = findItem(model_, resizeId_);
    const DisplayItem* was = before ? findItem(before->model, resizeId_) : nullptr;
    if (it == nullptr || was == nullptr || it->heightPx == was->heightPx)
        return false;
    pushUndo(std::move(*before));
    emit structureChanged();
    return true;
}

void WaveWidget::cancelRowResize() {
    if (drag_ != Drag::RowHeight)
        return;
    drag_ = Drag::None;
    setCursor(Qt::ArrowCursor);
    if (resizeBefore_) {
        DisplayItem* it = findItem(model_, resizeId_);
        const DisplayItem* was = findItem(resizeBefore_->model, resizeId_);
        if (it != nullptr && was != nullptr)
            it->heightPx = was->heightPx;
    }
    resizeBefore_.reset();
    rebuildRows();
}

int WaveWidget::labelBand(const Row& row) const {
    const bool analog = row.source && row.source->waveClass() == WaveClass::Analog;
    return analog ? std::min(row.h, kRowHeight) : row.h;
}

QFont WaveWidget::rowFont(int band) const {
    QFont f = font();
    const int fits = QFontMetrics(f).height() + 2;
    if (band >= fits)
        return f; // the normal font: taller rows centre it, they do not enlarge it
    // Lower rows: a smaller font that still fits, down to a legible minimum (the row clips below that).
    constexpr int kMinPixelSize = 8;
    f.setPixelSize(std::max(kMinPixelSize, band - 4));
    return f;
}

} // namespace lvd::gui
