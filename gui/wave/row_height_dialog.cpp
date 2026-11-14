// SPDX-License-Identifier: GPL-3.0-or-later
#include "wave/row_height_dialog.hpp"

#include <lvdwave/display_model.hpp>

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace lvd::gui {

RowHeightDialog::RowHeightDialog(int current, QWidget* parent)
    : QDialog(parent) {
    setObjectName("rowHeightDialog");
    setWindowTitle(tr("Row Height"));
    setModal(true);

    spin_ = new QSpinBox(this);
    spin_->setObjectName("heightSpin");
    spin_->setRange(lvd::wave::kMinRowHeightPx, lvd::wave::kMaxRowHeightPx);
    spin_->setSuffix(tr(" px"));
    spin_->setValue(current);
    spin_->selectAll();

    auto* form = new QFormLayout;
    form->addRow(tr("&Height:"), spin_);

    auto* hint =
        new QLabel(tr("Applies to every selected row. Default: %1 px, analog traces their number of lines.")
                       .arg(lvd::wave::kDefaultRowHeightPx),
                   this);
    hint->setWordWrap(true);

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons_->setObjectName("buttonBox");
    default_ = buttons_->addButton(tr("&Default"), QDialogButtonBox::ResetRole);
    default_->setObjectName("defaultButton");
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(default_, &QPushButton::clicked, this, [this] {
        reset_ = true;
        accept();
    });

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(hint);
    layout->addWidget(buttons_);
}

int RowHeightDialog::heightPx() const {
    return spin_->value();
}

void RowHeightDialog::setHeightPx(int px) {
    spin_->setValue(px);
}

QPushButton* RowHeightDialog::okButton() const {
    return buttons_->button(QDialogButtonBox::Ok);
}

} // namespace lvd::gui
