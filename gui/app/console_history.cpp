// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/console_history.hpp"

#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace lvd::gui {

ConsoleHistory::ConsoleHistory(int limit)
    : limit_(qMax(1, limit)) {}

bool ConsoleHistory::add(const QString& command) {
    if (command.trimmed().isEmpty())
        return false;
    entries_.removeAll(command);
    entries_ << command;
    trim();
    return true;
}

void ConsoleHistory::setEntries(const QStringList& entries) {
    entries_.clear();
    for (const QString& e : entries)
        add(e);
}

void ConsoleHistory::setLimit(int limit) {
    limit_ = qMax(1, limit);
    trim();
}

void ConsoleHistory::trim() {
    if (entries_.size() > limit_)
        entries_.erase(entries_.begin(), entries_.begin() + (entries_.size() - limit_));
}

qsizetype ConsoleHistory::searchBackward(const QString& text, qsizetype before) const {
    for (qsizetype i = qMin(before, entries_.size()) - 1; i >= 0; --i)
        if (entries_[i].contains(text, Qt::CaseInsensitive))
            return i;
    return -1;
}

// ---- dialog -----------------------------------------------------------------------------------------------

ConsoleHistoryDialog::ConsoleHistoryDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Command history"));
    setObjectName("consoleHistoryDialog");
    filter_ = new QLineEdit(this);
    filter_->setPlaceholderText(tr("Filter"));
    filter_->setClearButtonEnabled(true);
    list_ = new QListWidget(this);
    list_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    auto* buttons = new QDialogButtonBox(this);
    QPushButton* insert = buttons->addButton(tr("&Insert"), QDialogButtonBox::ActionRole);
    QPushButton* run = buttons->addButton(tr("&Run"), QDialogButtonBox::ActionRole);
    QPushButton* clear = buttons->addButton(tr("C&lear history"), QDialogButtonBox::ResetRole);
    buttons->addButton(QDialogButtonBox::Close);
    insert->setObjectName("insert");
    run->setObjectName("run");
    clear->setObjectName("clear");

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(filter_);
    layout->addWidget(list_, 1);
    layout->addWidget(buttons);
    resize(520, 420);

    connect(filter_, &QLineEdit::textChanged, this, [this] { refill(); });
    connect(list_, &QListWidget::itemActivated, this, [this] {
        if (const QString c = current(); !c.isEmpty())
            emit insertRequested(c);
    });
    connect(insert, &QPushButton::clicked, this, [this] {
        if (const QString c = current(); !c.isEmpty())
            emit insertRequested(c);
    });
    connect(run, &QPushButton::clicked, this, [this] {
        if (const QString c = current(); !c.isEmpty())
            emit runRequested(c);
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        emit clearRequested();
        setEntries({});
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void ConsoleHistoryDialog::setEntries(const QStringList& oldestFirst) {
    entries_ = oldestFirst;
    refill();
}

void ConsoleHistoryDialog::refill() {
    list_->clear();
    const QString f = filter_->text();
    for (qsizetype i = entries_.size() - 1; i >= 0; --i) {
        const QString& e = entries_[i];
        if (!f.isEmpty() && !e.contains(f, Qt::CaseInsensitive))
            continue;
        QString shownText = e;
        if (const qsizetype nl = e.indexOf(QLatin1Char('\n')); nl >= 0)
            shownText = e.left(nl) + QStringLiteral(" ... (%1 lines)").arg(e.count(QLatin1Char('\n')) + 1);
        auto* item = new QListWidgetItem(shownText, list_);
        item->setData(Qt::UserRole, e);
        item->setToolTip(e);
    }
    if (list_->count() > 0)
        list_->setCurrentRow(0);
}

QString ConsoleHistoryDialog::current() const {
    const QListWidgetItem* it = list_->currentItem();
    return it ? it->data(Qt::UserRole).toString() : QString();
}

QStringList ConsoleHistoryDialog::shown() const {
    QStringList l;
    for (int i = 0; i < list_->count(); ++i)
        l << list_->item(i)->data(Qt::UserRole).toString();
    return l;
}

} // namespace lvd::gui
