// SPDX-License-Identifier: GPL-3.0-or-later
#include "wave/compare_dialog.hpp"

#include <lvdwave/compare.hpp>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace lvd::gui {

CompareDialog::CompareDialog(const QStringList& datasets, int timescaleExponent, QWidget* parent)
    : QDialog(parent)
    , exponent_(timescaleExponent) {
    setWindowTitle(tr("Compare Waveforms"));
    setObjectName("compareDialog");
    ref_ = new QComboBox(this);
    ref_->setObjectName("refBox");
    test_ = new QComboBox(this);
    test_->setObjectName("testBox");
    for (QComboBox* box : {ref_, test_}) {
        box->addItem(tr("current simulation"), QString());
        for (const QString& d : datasets)
            box->addItem(d, d);
    }
    // the usual case: a dataset as the reference for the current run
    ref_->setCurrentIndex(datasets.isEmpty() ? 0 : 1);
    test_->setCurrentIndex(0);
    scope_ = new QLineEdit(this);
    scope_->setObjectName("scopeEdit");
    scope_->setPlaceholderText(tr("everything, or a scope such as /tb/dut"));
    tol_ = new QLineEdit(QStringLiteral("0"), this);
    tol_->setObjectName("toleranceEdit");
    tol_->setToolTip(tr("Differences that last this long or less are ignored, e.g. 2 ns"));
    status_ = new QLabel(this);
    status_->setObjectName("statusLabel");
    status_->setWordWrap(true);

    auto* form = new QFormLayout;
    form->addRow(tr("&Reference:"), ref_);
    form->addRow(tr("&Test:"), test_);
    form->addRow(tr("&Scope:"), scope_);
    form->addRow(tr("Time &tolerance:"), tol_);
    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons_->button(QDialogButtonBox::Ok)->setText(tr("Compare"));
    connect(buttons_, &QDialogButtonBox::accepted, this, [this] {
        if (valid_)
            accept();
    });
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(status_);
    lay->addWidget(buttons_);

    connect(ref_, &QComboBox::currentIndexChanged, this, [this] { revalidate(); });
    connect(test_, &QComboBox::currentIndexChanged, this, [this] { revalidate(); });
    connect(tol_, &QLineEdit::textChanged, this, [this] { revalidate(); });
    revalidate();
}

QString CompareDialog::reference() const {
    return ref_->currentData().toString();
}

QString CompareDialog::test() const {
    return test_->currentData().toString();
}

QString CompareDialog::scope() const {
    QString s = scope_->text().trimmed();
    while (s.startsWith(QLatin1Char('/')))
        s.remove(0, 1);
    while (s.endsWith(QLatin1Char('/')) || s.endsWith(QLatin1Char('.')))
        s.chop(1);
    s.replace(QLatin1Char('/'), QLatin1Char('.'));
    return s;
}

void CompareDialog::revalidate() {
    QString error;
    const auto tol = lvd::wave::parseDuration(tol_->text().toStdString(), exponent_);
    if (ref_->currentIndex() == test_->currentIndex())
        error = tr("Choose two different waveforms.");
    else if (!tol)
        error = tr("The tolerance is not a time (for example 2 ns).");
    valid_ = error.isEmpty();
    tolerance_ = tol.value_or(0);
    status_->setStyleSheet(valid_ ? QString() : QStringLiteral("color: #d04040;"));
    status_->setText(
        valid_ ? tr("Compares every signal of the test waveform with the signal of the same path in "
                    "the reference.")
               : error);
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(valid_);
}

} // namespace lvd::gui
