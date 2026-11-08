// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QString>

#include <optional>

class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;

namespace lvd::gui {

/// What the analog settings dialog starts from.
struct AnalogSettingsInput {
    bool autoRange = true; ///< the row is in automatic mode
    double min = 0.0;      ///< the manual limits to offer (the row's, or the range it is drawn with now)
    double max = 1.0;
};

/// The outcome of the dialog.
struct AnalogSettingsChoice {
    bool autoRange = true;
    double min = 0.0; ///< meaningful when autoRange is false
    double max = 1.0;
};

/// Parses a range limit as typed: plain or scientific notation ("-1.5", "2e-3", "1E6"), surrounding spaces
/// ignored, always with a point as decimal separator (the file formats use the same). Empty for malformed
/// text, NaN, infinity.
std::optional<double> parseRangeLimit(const QString& text);

/// Modal dialog for the analog format of the selected analog rows: automatic range (from the data), or a
/// manual range with Min and Max. OK is disabled while the manual range is not valid (a number is missing or
/// Min is not less than Max) and a message says why.
///
/// Object names, for tests: autoRadio, manualRadio, minEdit, maxEdit, errorLabel, buttonBox (its OK button is
/// found with button(QDialogButtonBox::Ok)).
class AnalogSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit AnalogSettingsDialog(const AnalogSettingsInput& input, QWidget* parent = nullptr);

    bool isAuto() const;
    void setAuto(bool on);
    /// Text of the two fields (what the user typed).
    QString minText() const;
    QString maxText() const;
    void setMinText(const QString& text);
    void setMaxText(const QString& text);
    /// Empty when the choice is valid (always in auto mode), otherwise the message shown.
    QString error() const;
    bool isValid() const { return error().isEmpty(); }
    /// The choice; only meaningful when isValid().
    AnalogSettingsChoice choice() const;
    QPushButton* okButton() const;

private:
    void refresh();

    QRadioButton* auto_ = nullptr;
    QRadioButton* manual_ = nullptr;
    QLineEdit* min_ = nullptr;
    QLineEdit* max_ = nullptr;
    QLabel* error_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
};

} // namespace lvd::gui
