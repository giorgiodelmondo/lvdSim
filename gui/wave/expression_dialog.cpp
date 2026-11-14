// SPDX-License-Identifier: GPL-3.0-or-later
#include "wave/expression_dialog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace lvd::gui {

ExpressionDialog::ExpressionDialog(Checker checker, const QStringList& signalPaths, QWidget* parent)
    : QDialog(parent)
    , checker_(std::move(checker)) {
    setWindowTitle(tr("Expression Signal"));
    setObjectName("expressionDialog");

    name_ = new QLineEdit(this);
    name_->setObjectName("nameEdit");
    name_->setPlaceholderText(tr("row name"));
    expr_ = new QLineEdit(this);
    expr_->setObjectName("exprEdit");
    expr_->setPlaceholderText(tr("for example: valid and not busy"));
    expr_->setMinimumWidth(420);
    insert_ = new QComboBox(this);
    insert_->setObjectName("insertSignal");
    insert_->addItem(tr("Insert signal..."));
    insert_->addItems(signalPaths);
    insert_->setEnabled(!signalPaths.isEmpty());

    status_ = new QLabel(this);
    status_->setObjectName("statusLabel");
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* hint = new QLabel(
        tr("Names: /top/sig, top.sig, sig, ref:/top/sig (dataset). Values: 100, 16#3F#, x\"3F\", \"0101\", "
           "'1', 1.5.\n"
           "Operators: not and or xor nand nor xnor (also ~ ! && || | ^), = /= < <= > >= (also == !=),\n"
           "=== !== (exact, X equals X), + -, & or {a, b} (concatenation), a(7 downto 0), a[3:0], "
           "signed(a)."),
        this);
    hint->setObjectName("syntaxHint");
    QFont small = hint->font();
    small.setPointSizeF(small.pointSizeF() * 0.9);
    hint->setFont(small);

    auto* form = new QFormLayout;
    form->addRow(tr("&Name:"), name_);
    form->addRow(tr("&Expression:"), expr_);
    form->addRow(QString(), insert_);

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons_, &QDialogButtonBox::accepted, this, [this] {
        if (valid_)
            accept();
    });
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* lay = new QVBoxLayout(this);
    lay->addLayout(form);
    lay->addWidget(status_);
    lay->addWidget(hint);
    lay->addWidget(buttons_);

    connect(expr_, &QLineEdit::textChanged, this, [this] {
        if (!nameEdited_)
            name_->setText(expr_->text().trimmed().left(40));
        revalidate();
    });
    connect(name_, &QLineEdit::textEdited, this, [this] { nameEdited_ = true; });
    connect(name_, &QLineEdit::textChanged, this, [this] { revalidate(); });
    connect(insert_, &QComboBox::activated, this, [this](int index) {
        if (index <= 0)
            return;
        expr_->insert(insert_->itemText(index));
        insert_->setCurrentIndex(0);
        expr_->setFocus();
    });
    revalidate();
}

void ExpressionDialog::setValues(const QString& name, const QString& expression) {
    nameEdited_ = true; // an existing name is kept while the expression is edited
    expr_->setText(expression);
    name_->setText(name);
    revalidate();
}

QString ExpressionDialog::name() const {
    return name_->text().trimmed();
}

QString ExpressionDialog::expression() const {
    return expr_->text().trimmed();
}

void ExpressionDialog::revalidate() {
    QString description;
    QString error;
    if (expression().isEmpty())
        error = tr("Enter an expression.");
    else
        error = checker_ ? checker_(expression(), &description) : QString();
    if (error.isEmpty() && name().isEmpty())
        error = tr("Enter a name for the row.");
    valid_ = error.isEmpty();
    if (valid_) {
        status_->setStyleSheet(QStringLiteral("color: #2e9e44;"));
        status_->setText(
            tr("OK: the result is a %1.").arg(description.isEmpty() ? tr("value") : description));
    } else {
        status_->setStyleSheet(QStringLiteral("color: #d04040;"));
        status_->setText(error);
    }
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(valid_);
}

} // namespace lvd::gui
