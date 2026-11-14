// SPDX-License-Identifier: GPL-3.0-or-later
#include "wave/find_value_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace lvd::gui {

using lvd::wave::Radix;
using lvd::wave::SearchCondition;

namespace {

bool isCrossing(SearchCondition c) {
    return c == SearchCondition::CrossRising || c == SearchCondition::CrossFalling ||
           c == SearchCondition::CrossEither;
}

QString radixHint(Radix r) {
    switch (r) {
    case Radix::Binary:
        return QObject::tr("binary: 0 1 X Z U W L H, ? for any bit");
    case Radix::Octal:
        return QObject::tr("octal digits, X Z U, ? for any digit");
    case Radix::Hex:
    case Radix::Default:
        return QObject::tr("hex digits, X Z U, ? for any digit");
    case Radix::Unsigned:
        return QObject::tr("decimal number, or X Z U");
    case Radix::Signed:
        return QObject::tr("signed decimal number, or X Z U");
    case Radix::Ascii:
        return QObject::tr("text, ? for an unknown character");
    }
    return {};
}

} // namespace

FindValueDialog::FindValueDialog(WaveWidget* wave)
    : QDialog(wave, Qt::Tool)
    , wave_(wave) {
    setObjectName("findValueDialog");
    setWindowTitle(tr("Find Value"));
    setModal(false);

    signals_ = new QLabel(this);
    signals_->setObjectName("signalsLabel");
    signals_->setWordWrap(true);

    condition_ = new QComboBox(this);
    condition_->setObjectName("conditionBox");
    condition_->addItem(tr("equals"), static_cast<int>(SearchCondition::Equals));
    condition_->addItem(tr("does not equal"), static_cast<int>(SearchCondition::NotEquals));
    condition_->addItem(tr("crosses threshold rising"), static_cast<int>(SearchCondition::CrossRising));
    condition_->addItem(tr("crosses threshold falling"), static_cast<int>(SearchCondition::CrossFalling));
    condition_->addItem(tr("crosses threshold either way"), static_cast<int>(SearchCondition::CrossEither));

    value_ = new QLineEdit(this);
    value_->setObjectName("valueEdit");
    valueLabel_ = new QLabel(tr("&Value:"), this);
    valueLabel_->setObjectName("valueLabel");
    valueLabel_->setBuddy(value_);

    wrap_ = new QCheckBox(tr("&Wrap around"), this);
    wrap_->setObjectName("wrapBox");
    wrap_->setChecked(false);

    status_ = new QLabel(this);
    status_->setObjectName("statusLabel");
    status_->setWordWrap(true);
    status_->setMinimumHeight(fontMetrics().height() * 2);

    auto* form = new QFormLayout;
    form->addRow(tr("Signals:"), signals_);
    form->addRow(tr("&Condition:"), condition_);
    form->addRow(valueLabel_, value_);
    form->addRow(QString(), wrap_);

    auto* buttons = new QDialogButtonBox(this);
    prev_ = buttons->addButton(tr("Find &Previous"), QDialogButtonBox::ActionRole);
    prev_->setObjectName("findPrevious");
    next_ = buttons->addButton(tr("Find &Next"), QDialogButtonBox::ActionRole);
    next_->setObjectName("findNext");
    next_->setDefault(true);
    QPushButton* close = buttons->addButton(QDialogButtonBox::Close);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(next_, &QPushButton::clicked, this, [this] { find(+1); });
    connect(prev_, &QPushButton::clicked, this, [this] { find(-1); });
    connect(condition_, &QComboBox::currentIndexChanged, this, [this] { conditionChanged(); });
    connect(wave_, &WaveWidget::selectionChanged, this, [this] { refreshSignals(); });

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(status_);
    layout->addWidget(buttons);
    resize(440, sizeHint().height());
    refreshSignals();
}

void FindValueDialog::refreshSignals() {
    const std::vector<int> rows = wave_->searchRows();
    if (rows.empty()) {
        signals_->setText(tr("<i>none selected: select rows in the wave window</i>"));
    } else {
        QStringList names;
        for (std::size_t i = 0; i < rows.size() && i < 4; ++i)
            names << QString::fromStdString(wave_->rowName(rows[i])).toHtmlEscaped();
        QString text = names.join(QStringLiteral(", "));
        if (rows.size() > 4)
            text += tr(" and %1 more").arg(rows.size() - 4);
        signals_->setText(text);
    }
    conditionChanged();
}

void FindValueDialog::conditionChanged() {
    const auto c = static_cast<SearchCondition>(condition_->currentData().toInt());
    const std::vector<int> rows = wave_->searchRows();
    if (isCrossing(c)) {
        valueLabel_->setText(tr("&Threshold:"));
        value_->setPlaceholderText(tr("a number, e.g. 0.5 or 128"));
    } else {
        valueLabel_->setText(tr("&Value:"));
        value_->setPlaceholderText(rows.empty() ? tr("value in the row's radix")
                                                : radixHint(wave_->rowRadix(rows.front())));
    }
}

void FindValueDialog::focusValue() {
    value_->setFocus();
    value_->selectAll();
}

WaveWidget::FindSpec FindValueDialog::spec() const {
    WaveWidget::FindSpec s;
    s.condition = static_cast<SearchCondition>(condition_->currentData().toInt());
    s.value = value_->text();
    return s;
}

WaveWidget::FindResult FindValueDialog::find(int dir) {
    const WaveWidget::FindResult r = wave_->findValue(spec(), dir, wrap_->isChecked());
    status_->setText(r.message);
    return r;
}

} // namespace lvd::gui
