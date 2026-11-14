// SPDX-License-Identifier: GPL-3.0-or-later
#include "wave/memory_model.hpp"

#include "wave/view_state.hpp"

#include <QBrush>
#include <QColor>
#include <QFontDatabase>

#include <algorithm>
#include <cstdlib>

namespace lvd::gui {

using namespace lvd::wave;

MemoryModel::MemoryModel(QObject* parent)
    : QAbstractTableModel(parent) {}

void MemoryModel::setStore(std::shared_ptr<const WaveStore> store) {
    beginResetModel();
    image_.reset();
    store_ = std::move(store);
    highlightTime_.reset();
    highlighted_.clear();
    endResetModel();
}

void MemoryModel::setMemory(std::optional<MemoryInfo> info) {
    beginResetModel();
    image_.reset();
    if (info && store_)
        image_ = std::make_unique<MemoryImage>(*store_, std::move(*info));
    updateHighlight();
    endResetModel();
}

void MemoryModel::refresh() {
    if (!image_)
        return;
    image_->update();
    updateHighlight();
    allChanged();
}

void MemoryModel::setTime(Time t) {
    if (t == time_)
        return;
    time_ = t;
    updateHighlight();
    allChanged();
}

void MemoryModel::setWordsPerLine(int n) {
    n = std::clamp(n, 1, 256);
    if (n == wordsPerLine_)
        return;
    beginResetModel();
    wordsPerLine_ = n;
    endResetModel();
}

void MemoryModel::setAddressRadix(Radix r) {
    if (r == addressRadix_)
        return;
    addressRadix_ = r;
    emit headerDataChanged(Qt::Vertical, 0, std::max(0, rowCount() - 1));
    emit headerDataChanged(Qt::Horizontal, 0, std::max(0, columnCount() - 1));
}

void MemoryModel::setDataRadix(Radix r) {
    if (r == dataRadix_)
        return;
    dataRadix_ = r;
    allChanged();
}

void MemoryModel::updateHighlight() {
    highlighted_.clear();
    highlightTime_.reset();
    if (!image_)
        return;
    highlightTime_ = image_->lastWriteTime(time_);
    if (highlightTime_)
        highlighted_ = image_->wordsWrittenAt(*highlightTime_);
}

void MemoryModel::allChanged() {
    if (rowCount() > 0)
        emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
}

int MemoryModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid() || !image_)
        return 0;
    return static_cast<int>((image_->depth() + static_cast<std::size_t>(wordsPerLine_) - 1) /
                            static_cast<std::size_t>(wordsPerLine_));
}

int MemoryModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid() || !image_)
        return 0;
    return static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(wordsPerLine_), image_->depth()));
}

int MemoryModel::wordAt(const QModelIndex& index) const {
    if (!index.isValid() || !image_)
        return -1;
    const std::size_t w = static_cast<std::size_t>(index.row()) * static_cast<std::size_t>(wordsPerLine_) +
                          static_cast<std::size_t>(index.column());
    return w < image_->depth() ? static_cast<int>(w) : -1;
}

QModelIndex MemoryModel::indexOfWord(std::size_t word) const {
    if (!image_ || word >= image_->depth())
        return {};
    const auto per = static_cast<std::size_t>(wordsPerLine_);
    return index(static_cast<int>(word / per), static_cast<int>(word % per));
}

QString MemoryModel::addressText(std::size_t word) const {
    if (!image_)
        return {};
    const MemoryInfo& m = image_->info();
    const std::int64_t widest = std::max(std::abs(m.low), std::abs(m.high()));
    const int digits = static_cast<int>(formatAddress(widest, addressRadix_).size());
    return QString::fromStdString(
        formatAddress(m.low + static_cast<std::int64_t>(word), addressRadix_, digits));
}

QString MemoryModel::valueText(std::size_t word) const {
    if (!image_ || word >= image_->depth())
        return {};
    if (image_->wordSignal(word) == nullptr)
        return QString();
    return QString::fromStdString(image_->format(word, time_, dataRadix_));
}

QVariant MemoryModel::data(const QModelIndex& index, int role) const {
    const int w = wordAt(index);
    if (w < 0)
        return role == kWordRole ? QVariant(-1) : QVariant();
    const auto word = static_cast<std::size_t>(w);
    const bool written = std::binary_search(highlighted_.begin(), highlighted_.end(), word);
    switch (role) {
    case Qt::DisplayRole:
        return valueText(word);
    case Qt::TextAlignmentRole:
        return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
    case Qt::FontRole:
        return QFontDatabase::systemFont(QFontDatabase::FixedFont);
    case Qt::BackgroundRole:
        return written ? QVariant(QBrush(QColor(255, 214, 102))) : QVariant();
    case Qt::ForegroundRole:
        // written now: dark on the marker colour; never written up to time(): grey (still the initial value)
        if (written)
            return QBrush(QColor(20, 20, 20));
        return image_->writeCount(word, time_) == 0 ? QVariant(QBrush(QColor(128, 128, 128))) : QVariant();
    case kWordRole:
        return w;
    case kWrittenRole:
        return written;
    case Qt::ToolTipRole: {
        const MemoryInfo& m = image_->info();
        const int exp = store_ ? store_->timescaleExponent() : -15;
        QString tip = QStringLiteral("%1[%2] = %3")
                          .arg(QString::fromStdString(m.path))
                          .arg(m.low + static_cast<std::int64_t>(word))
                          .arg(valueText(word));
        const std::size_t n = image_->writeCount(word, time_);
        if (const auto lw = image_->lastWrite(word, time_))
            tip += tr("\nlast written at %1 (%2 write(s) up to %3)")
                       .arg(QString::fromStdString(formatTime(static_cast<double>(*lw), exp)))
                       .arg(n)
                       .arg(QString::fromStdString(formatTime(static_cast<double>(time_), exp)));
        else
            tip += tr("\nnot written up to %1 (initial value)")
                       .arg(QString::fromStdString(formatTime(static_cast<double>(time_), exp)));
        return tip;
    }
    default:
        return {};
    }
}

QVariant MemoryModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (!image_)
        return {};
    if (role == Qt::FontRole)
        return QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (role != Qt::DisplayRole)
        return QAbstractTableModel::headerData(section, orientation, role);
    if (orientation == Qt::Vertical)
        return addressText(static_cast<std::size_t>(section) * static_cast<std::size_t>(wordsPerLine_));
    const Radix r = addressRadix_ == Radix::Signed ? Radix::Unsigned : addressRadix_;
    return QStringLiteral("+") + QString::fromStdString(formatAddress(section, r));
}

} // namespace lvd::gui
