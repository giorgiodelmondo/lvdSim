// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "wave/wave_widget.hpp"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace lvd::gui {

/// The non-modal "Find Value" dialog of a wave window. It searches the rows selected in the wave window (the
/// label at the top names them and the radix values are read in) for a condition: equal or not equal to a
/// value, or a threshold crossing (rising, falling, either) for numeric rows. Find Next / Find Previous
/// search from the active cursor and move it to the hit; Wrap around (off by default) starts over from the
/// other end. The result is shown in the dialog. F3 / Shift+F3 in the wave window repeat the last search.
///
/// Object names, for tests: signalsLabel, conditionBox, valueEdit, valueLabel, wrapBox, findNext,
/// findPrevious, statusLabel.
class FindValueDialog : public QDialog {
    Q_OBJECT
public:
    explicit FindValueDialog(WaveWidget* wave);

    /// Reads the selection of the wave window again (names and radix hint).
    void refreshSignals();
    void focusValue();
    /// Runs the search (dir > 0: next, < 0: previous) with what the fields hold.
    WaveWidget::FindResult find(int dir);
    WaveWidget::FindSpec spec() const;

    QComboBox* conditionBox() const noexcept { return condition_; }
    QLineEdit* valueEdit() const noexcept { return value_; }
    QCheckBox* wrapBox() const noexcept { return wrap_; }
    QLabel* statusLabel() const noexcept { return status_; }
    QLabel* signalsLabel() const noexcept { return signals_; }

private:
    void conditionChanged();

    WaveWidget* wave_;
    QLabel* signals_ = nullptr;
    QComboBox* condition_ = nullptr;
    QLabel* valueLabel_ = nullptr;
    QLineEdit* value_ = nullptr;
    QCheckBox* wrap_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* next_ = nullptr;
    QPushButton* prev_ = nullptr;
};

} // namespace lvd::gui
