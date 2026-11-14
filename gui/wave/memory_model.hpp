// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <lvdwave/memory.hpp>
#include <lvdwave/store.hpp>

#include <QAbstractTableModel>

#include <memory>
#include <optional>
#include <vector>

namespace lvd::gui {

/// One memory as an address/data grid: a line per wordsPerLine() words, the address of the line's first word
/// in the vertical header, the word offsets in the horizontal header. Values come from a
/// lvd::wave::MemoryImage at time(); only the cells Qt asks for are formatted (a screen is a few thousand
/// O(log n) lookups).
///
/// The words written at the latest write time at or before time() are marked (kWrittenRole, a background
/// colour): at a write time they are the words that changed there, between writes the last ones written.
class MemoryModel : public QAbstractTableModel {
    Q_OBJECT
public:
    static constexpr int kWordRole = Qt::UserRole;        ///< word number (int), -1 past the end
    static constexpr int kWrittenRole = Qt::UserRole + 1; ///< bool: written at highlightTime()

    explicit MemoryModel(QObject* parent = nullptr);

    /// The store the memories are read from; clears the memory shown.
    void setStore(std::shared_ptr<const lvd::wave::WaveStore> store);
    const lvd::wave::WaveStore* store() const noexcept { return store_.get(); }
    /// Shows a memory; nullopt shows none.
    void setMemory(std::optional<lvd::wave::MemoryInfo> info);
    const lvd::wave::MemoryImage* image() const noexcept { return image_.get(); }

    /// Reads what the store gained (live runs) and redraws.
    void refresh();

    void setTime(lvd::wave::Time t);
    lvd::wave::Time time() const noexcept { return time_; }
    void setWordsPerLine(int n);
    int wordsPerLine() const noexcept { return wordsPerLine_; }
    void setAddressRadix(lvd::wave::Radix r);
    lvd::wave::Radix addressRadix() const noexcept { return addressRadix_; }
    void setDataRadix(lvd::wave::Radix r);
    lvd::wave::Radix dataRadix() const noexcept { return dataRadix_; }

    /// The time whose writes are marked, and those words (ascending).
    std::optional<lvd::wave::Time> highlightTime() const noexcept { return highlightTime_; }
    const std::vector<std::size_t>& highlightedWords() const noexcept { return highlighted_; }

    int wordAt(const QModelIndex& index) const;
    QModelIndex indexOfWord(std::size_t word) const;
    /// Address of a word (low + word) as the headers write it.
    QString addressText(std::size_t word) const;
    /// Value text of a word at time(), "-" before its first value.
    QString valueText(std::size_t word) const;

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    void updateHighlight();
    void allChanged();

    std::shared_ptr<const lvd::wave::WaveStore> store_;
    std::unique_ptr<lvd::wave::MemoryImage> image_;
    lvd::wave::Time time_ = 0;
    int wordsPerLine_ = 8;
    lvd::wave::Radix addressRadix_ = lvd::wave::Radix::Hex;
    lvd::wave::Radix dataRadix_ = lvd::wave::Radix::Hex;
    std::optional<lvd::wave::Time> highlightTime_;
    std::vector<std::size_t> highlighted_;
};

} // namespace lvd::gui
