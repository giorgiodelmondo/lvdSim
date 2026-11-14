// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QStringList>

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;

namespace lvd::gui {

/// The "Compare Waveforms" dialog: a reference and a test waveform (the current one or a dataset), the scope
/// to compare (empty: everything) and a time tolerance ("2 ns"; differences that short are ignored). OK needs
/// two different waveforms and a valid tolerance.
///
/// Object names, for tests: refBox, testBox, scopeEdit, toleranceEdit, statusLabel.
class CompareDialog : public QDialog {
    Q_OBJECT
public:
    /// `datasets`: the dataset names; the current waveform is offered as the first entry of both lists.
    /// `timescaleExponent`: unit of bare tolerance numbers.
    CompareDialog(const QStringList& datasets, int timescaleExponent, QWidget* parent = nullptr);

    /// "" for the current waveform, else the dataset name.
    QString reference() const;
    QString test() const;
    QString scope() const; ///< dotted ("tb.dut"); "/tb/dut" is accepted and converted
    unsigned long long tolerance() const noexcept { return tolerance_; }
    bool isValid() const noexcept { return valid_; }

    QComboBox* refBox() const noexcept { return ref_; }
    QComboBox* testBox() const noexcept { return test_; }
    QLineEdit* scopeEdit() const noexcept { return scope_; }
    QLineEdit* toleranceEdit() const noexcept { return tol_; }
    QLabel* statusLabel() const noexcept { return status_; }

private:
    void revalidate();

    int exponent_;
    QComboBox* ref_ = nullptr;
    QComboBox* test_ = nullptr;
    QLineEdit* scope_ = nullptr;
    QLineEdit* tol_ = nullptr;
    QLabel* status_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
    unsigned long long tolerance_ = 0;
    bool valid_ = false;
};

} // namespace lvd::gui
