// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QStringList>

#include <functional>

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;

namespace lvd::gui {

/// The "Expression Signal" dialog: a name and an expression (lvd::wave::Expression syntax). The expression is
/// checked while it is typed; the status line shows the error with its column, or the type of the result. OK
/// is enabled only for a name and an expression that compiles. "Insert signal" puts the path of a wave row at
/// the text cursor.
///
/// Object names, for tests: nameEdit, exprEdit, insertSignal, statusLabel.
class ExpressionDialog : public QDialog {
    Q_OBJECT
public:
    /// Returns an error message, or empty and fills the description of the result.
    using Checker = std::function<QString(const QString& text, QString* description)>;

    ExpressionDialog(Checker checker, const QStringList& signalPaths, QWidget* parent = nullptr);

    void setValues(const QString& name, const QString& expression);
    QString name() const;
    QString expression() const;
    bool isValid() const noexcept { return valid_; }

    QLineEdit* nameEdit() const noexcept { return name_; }
    QLineEdit* exprEdit() const noexcept { return expr_; }
    QComboBox* insertBox() const noexcept { return insert_; }
    QLabel* statusLabel() const noexcept { return status_; }

private:
    void revalidate();

    Checker checker_;
    QLineEdit* name_ = nullptr;
    QLineEdit* expr_ = nullptr;
    QComboBox* insert_ = nullptr;
    QLabel* status_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
    bool valid_ = false;
    bool nameEdited_ = false;
};

} // namespace lvd::gui
