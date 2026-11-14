// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

class QDialogButtonBox;
class QPushButton;
class QSpinBox;

namespace lvd::gui {

/// Modal dialog for the height of the selected wave rows in pixels. The spin box is limited to
/// [lvd::wave::kMinRowHeightPx, lvd::wave::kMaxRowHeightPx]. "Default" gives every selected row its default
/// height again (digital rows one line, analog traces their number of lines) and closes the dialog.
///
/// Object names, for tests: heightSpin, defaultButton, buttonBox.
class RowHeightDialog : public QDialog {
    Q_OBJECT
public:
    /// `current`: the height to start from (the clicked row's).
    explicit RowHeightDialog(int current, QWidget* parent = nullptr);

    int heightPx() const;
    void setHeightPx(int px);
    /// True if the dialog was closed with "Default".
    bool resetToDefault() const noexcept { return reset_; }
    QSpinBox* spinBox() const noexcept { return spin_; }
    QPushButton* okButton() const;
    QPushButton* defaultButton() const noexcept { return default_; }

private:
    QSpinBox* spin_ = nullptr;
    QPushButton* default_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
    bool reset_ = false;
};

} // namespace lvd::gui
