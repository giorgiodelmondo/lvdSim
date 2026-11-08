// SPDX-License-Identifier: GPL-3.0-or-later
#include "wave/analog_settings_dialog.hpp"

#include <lvdwave/display_model.hpp>

#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

#include <cmath>

namespace lvd::gui {

std::optional<double> parseRangeLimit(const QString& text) {
    const QString t = text.trimmed();
    if (t.isEmpty())
        return std::nullopt;
    bool ok = false;
    const double v = QLocale::c().toDouble(t, &ok);
    if (!ok || !std::isfinite(v))
        return std::nullopt;
    return v;
}

namespace {

QString limitText(double v) {
    return QString::number(v, 'g', 9);
}

} // namespace

AnalogSettingsDialog::AnalogSettingsDialog(const AnalogSettingsInput& input, QWidget* parent)
    : QDialog(parent) {
    setObjectName("analogSettingsDialog");
    setWindowTitle(tr("Analog Settings"));
    setModal(true);

    auto_ = new QRadioButton(tr("&Auto range"), this);
    auto_->setObjectName("autoRadio");
    auto_->setToolTip(tr("The range follows the data: the lowest and the highest value of the signal"));
    manual_ = new QRadioButton(tr("&Manual range"), this);
    manual_->setObjectName("manualRadio");
    manual_->setToolTip(
        tr("Fixed limits: Min is the bottom of the row, Max the top. Values outside are drawn at the edge"));

    auto* validator = new QDoubleValidator(this);
    validator->setNotation(QDoubleValidator::ScientificNotation);
    validator->setLocale(QLocale::c());
    min_ = new QLineEdit(limitText(input.min), this);
    min_->setObjectName("minEdit");
    max_ = new QLineEdit(limitText(input.max), this);
    max_->setObjectName("maxEdit");
    for (QLineEdit* e : {min_, max_}) {
        e->setValidator(validator);
        e->setMinimumWidth(160);
    }

    auto* form = new QFormLayout;
    form->setContentsMargins(24, 0, 0, 0);
    form->addRow(tr("&Min:"), min_);
    form->addRow(tr("Ma&x:"), max_);

    error_ = new QLabel(this);
    error_->setObjectName("errorLabel");
    error_->setWordWrap(true);
    error_->setStyleSheet("QLabel { color: #ff6060; }");
    error_->setMinimumHeight(error_->fontMetrics().height() * 2);
    error_->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons_->setObjectName("buttonBox");

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(auto_);
    layout->addWidget(manual_);
    layout->addLayout(form);
    layout->addWidget(error_);
    layout->addWidget(buttons_);

    connect(buttons_, &QDialogButtonBox::accepted, this, [this] {
        if (isValid())
            accept();
    });
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(auto_, &QRadioButton::toggled, this, [this] { refresh(); });
    connect(min_, &QLineEdit::textChanged, this, [this] { refresh(); });
    connect(max_, &QLineEdit::textChanged, this, [this] { refresh(); });

    (input.autoRange ? auto_ : manual_)->setChecked(true);
    refresh();
}

bool AnalogSettingsDialog::isAuto() const {
    return auto_->isChecked();
}
void AnalogSettingsDialog::setAuto(bool on) {
    (on ? auto_ : manual_)->setChecked(true);
}
QString AnalogSettingsDialog::minText() const {
    return min_->text();
}
QString AnalogSettingsDialog::maxText() const {
    return max_->text();
}
void AnalogSettingsDialog::setMinText(const QString& text) {
    min_->setText(text);
}
void AnalogSettingsDialog::setMaxText(const QString& text) {
    max_->setText(text);
}
QPushButton* AnalogSettingsDialog::okButton() const {
    return buttons_->button(QDialogButtonBox::Ok);
}

QString AnalogSettingsDialog::error() const {
    if (isAuto())
        return {};
    const auto lo = parseRangeLimit(min_->text());
    const auto hi = parseRangeLimit(max_->text());
    if (!lo)
        return tr("Min is not a number. Use for example -1.5 or 2e-3.");
    if (!hi)
        return tr("Max is not a number. Use for example 3.3 or 1e6.");
    const std::string why = lvd::wave::analogRangeError(*lo, *hi);
    if (why.empty())
        return {};
    return tr("Min must be less than Max.");
}

AnalogSettingsChoice AnalogSettingsDialog::choice() const {
    AnalogSettingsChoice c;
    c.autoRange = isAuto();
    if (!c.autoRange) {
        c.min = parseRangeLimit(min_->text()).value_or(0.0);
        c.max = parseRangeLimit(max_->text()).value_or(1.0);
    }
    return c;
}

void AnalogSettingsDialog::refresh() {
    const bool manual = !isAuto();
    min_->setEnabled(manual);
    max_->setEnabled(manual);
    const QString why = error();
    error_->setText(why);
    okButton()->setEnabled(why.isEmpty());
    const bool badMin = manual && !parseRangeLimit(min_->text());
    const bool badMax = manual && !parseRangeLimit(max_->text());
    const bool order = manual && !badMin && !badMax && !why.isEmpty();
    min_->setStyleSheet(badMin || order ? "QLineEdit { border: 1px solid #ff4040; }" : QString());
    max_->setStyleSheet(badMax || order ? "QLineEdit { border: 1px solid #ff4040; }" : QString());
}

} // namespace lvd::gui
