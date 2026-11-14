// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "wave/memory_model.hpp"

#include <lvdwave/memory.hpp>

#include <QWidget>

#include <functional>
#include <memory>
#include <optional>

class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTableView;
class QToolButton;
class QTreeWidget;

namespace lvd::gui {

/// The Memory window: the memories of the design (arrays of words, see lvd::wave::findMemories) in a list,
/// and the selected one as an address/data grid (MemoryModel), for a dock of the simulation window.
///
/// - The grid shows the memory at one time: the end of the recorded data ("Latest", updated while the
///   simulation runs, refresh()) or the active wave cursor (showCursorTime()), which makes it possible to go
///   back in time through everything recorded. Previous / next write move the wave cursor to the time of the
///   next write to this memory (timeSelected()) and switch to the cursor.
/// - The words written at the last write time at or before the shown time are marked.
/// - Words per line, the address radix and the data radix are set in the bar above the grid; `add mem`
/// options
///   set them too (applyRequest()).
/// - "Go to" selects the word at an address (typed in the address radix, or with a 0x, 16#..#, x"..", 0b
///   prefix); "Find" searches the words for a value in the data radix (the rules of the wave window's Find
///   Value), from the selected word, wrapping at the end.
/// - "Save..." writes the memory at the shown time as a $readmemh file ($readmemb when the name ends in .bin
/// or
///   .memb).
/// - The context menu of the grid adds the selected words to the wave window (addToWave()) or copies them.
///
/// Object names, for tests: memoryList, memoryTable, wordsBox, addressRadixBox, dataRadixBox, timeBox,
/// prevWrite, nextWrite, gotoEdit, findEdit, findPrev, findNext, saveMemory, memoryStatus, memoryHint.
class MemoryWindow : public QWidget {
    Q_OBJECT
public:
    enum class TimeSource { Latest, Cursor };

    explicit MemoryWindow(QWidget* parent = nullptr);

    MemoryModel* model() const noexcept { return model_; }
    QTableView* table() const noexcept { return table_; }
    QTreeWidget* memoryList() const noexcept { return list_; }

    /// Lists the memories of the store (nullptr: none).
    void setStore(std::shared_ptr<const lvd::wave::WaveStore> store);
    /// The store gained data (live run): the grid is updated.
    void refresh();
    /// Dotted paths of the memories listed.
    QStringList memoryPaths() const;
    /// Shows a memory by path (dotted, or `/top/mem` as `add mem` writes it). False if there is no such
    /// memory.
    bool showMemory(const QString& path);
    QString currentMemory() const;
    /// `add mem` with its options: every path is checked, the last one found is shown with the options
    /// applied. Returns false (and the message in *error) if a path is not a memory.
    bool applyRequest(const lvd::wave::AddMemRequest& request, QString* error = nullptr);

    void setTimeSource(TimeSource source);
    TimeSource timeSource() const noexcept { return source_; }
    /// The active wave cursor moved (timescale units); used when the time source is Cursor.
    void showCursorTime(std::optional<double> t);
    /// The time the grid shows.
    lvd::wave::Time shownTime() const;

    void setWordsPerLine(int n);
    void setAddressRadix(lvd::wave::Radix r);
    void setDataRadix(lvd::wave::Radix r);

    /// Selects and shows the word at an address. False (and a status message) if the text is no address of
    /// the memory.
    bool goToAddress(const QString& text);
    /// Finds the next (dir > 0) or previous word with this value, from the selected word, wrapping. False if
    /// there is none or the value is not valid (see statusText()).
    bool findValue(const QString& text, int dir);
    /// Moves to the next / previous write time of the memory: switches to the cursor and emits
    /// timeSelected().
    bool stepWrite(int dir);
    /// Words currently selected, ascending.
    std::vector<std::size_t> selectedWords() const;

    QString memoryFileText(lvd::wave::Radix radix) const;
    bool saveMemoryFile(const QString& path, QString* error = nullptr);
    /// Asks for a file name (replaceable for tests), then saveMemoryFile().
    bool saveDialog();
    using FilePrompt = std::function<QString()>;
    void setFilePrompt(FilePrompt prompt) { filePrompt_ = std::move(prompt); }

    /// The status line under the grid: memory, time, last write, or the result of the last go to / find.
    QString statusText() const;

signals:
    /// Move the wave cursor to this time (timescale units).
    void timeSelected(double time);
    /// `add wave` paths of words to add to the wave window ("/top/mem[3]").
    void addToWave(const QStringList& paths);

private:
    void selectMemoryRow(int row);
    void applyTime();
    void updateStatus(const QString& message = {});
    void showContextMenu(const QPoint& pos);

    std::shared_ptr<const lvd::wave::WaveStore> store_;
    std::vector<lvd::wave::MemoryInfo> memories_;
    MemoryModel* model_ = nullptr;
    QTreeWidget* list_ = nullptr;
    QTableView* table_ = nullptr;
    QSpinBox* words_ = nullptr;
    QComboBox* addressRadix_ = nullptr;
    QComboBox* dataRadix_ = nullptr;
    QComboBox* time_ = nullptr;
    QToolButton* prevWrite_ = nullptr;
    QToolButton* nextWrite_ = nullptr;
    QLineEdit* goto_ = nullptr;
    QLineEdit* find_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* hint_ = nullptr;
    TimeSource source_ = TimeSource::Latest;
    std::optional<double> cursor_;
    QString message_;
    FilePrompt filePrompt_;
};

} // namespace lvd::gui
