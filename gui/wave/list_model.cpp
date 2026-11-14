// SPDX-License-Identifier: GPL-3.0-or-later
#include "wave/list_model.hpp"

#include <lvdwave/wave_commands.hpp>

#include <QFont>
#include <QIODevice>

#include <algorithm>
#include <climits>

namespace lvd::gui {

using namespace lvd::wave;

namespace {

const char* unitName(int exp) {
    switch (exp) {
    case -15:
        return "fs";
    case -12:
        return "ps";
    case -9:
        return "ns";
    case -6:
        return "us";
    case -3:
        return "ms";
    case 0:
        return "s";
    default:
        return "?";
    }
}

std::uint64_t pow10u(int n) {
    std::uint64_t v = 1;
    for (int i = 0; i < n; ++i)
        v *= 10;
    return v;
}

QString csvField(const QString& s) {
    if (!s.contains(QLatin1Char(',')) && !s.contains(QLatin1Char('"')) && !s.contains(QLatin1Char('\n')))
        return s;
    QString q = s;
    q.replace(QLatin1String("\""), QLatin1String("\"\""));
    return QLatin1Char('"') + q + QLatin1Char('"');
}

QString slashPath(const std::string& dotted) {
    if (!dotted.empty() && dotted[0] == '=')
        return QString::fromStdString(dotted.substr(1));
    return QString::fromStdString(commandPath(dotted));
}

} // namespace

ListModel::ListModel(QObject* parent)
    : QAbstractTableModel(parent) {}

void ListModel::setStore(std::shared_ptr<const WaveStore> store) {
    store_ = std::move(store);
    if (store_ && !unitChosen_)
        unitExp_ = std::max(-9, std::min(0, store_->timescaleExponent() - (store_->timescaleExponent() % 3)));
    rebuildIndex();
}

void ListModel::setSpace(const WaveSpace* space) {
    space_ = space;
    rebuildIndex();
}

const Signal* ListModel::lookup(const std::string& path) {
    if (!path.empty() && path[0] == '=') {
        auto it = exprs_.find(path);
        if (it == exprs_.end()) {
            WaveSpace own; // without the wave window's space: the store alone
            own.setCurrent(store_);
            const WaveSpace& sp = space_ != nullptr ? *space_ : own;
            auto r =
                ExpressionSignal::create(std::string_view(path).substr(1), sp.resolver(), path.substr(1));
            if (!r.ok())
                return nullptr;
            it = exprs_.emplace(path, std::move(r).value()).first;
        }
        return &it->second->signal();
    }
    if (store_) {
        const SignalId id = store_->findSignal(path);
        if (id != kNoSignal)
            return &store_->signal(id);
    }
    return space_ != nullptr ? space_->findPath(path) : nullptr;
}

void ListModel::resolveIds() {
    ids_.clear();
    exprs_.clear(); // inputs may have gone (a new store): build again
    for (const Column& c : columns_)
        ids_.push_back(lookup(c.path));
}

void ListModel::rebuildIndex() {
    beginResetModel();
    resolveIds();
    if (store_) {
        std::vector<const Signal*> found;
        for (const Signal* s : ids_)
            if (s != nullptr)
                found.push_back(s);
        ListOptions o;
        o.trigger = mode_.trigger;
        const Signal* trigger = mode_.trigger == ListTrigger::Signal ? lookup(mode_.triggerPath) : nullptr;
        o.strobeStart = mode_.strobeStart;
        o.strobePeriod = mode_.strobePeriod;
        index_ = ListIndex(*store_, std::move(found), trigger, o);
        rows_ = static_cast<int>(std::min<std::size_t>(index_.rowCount(), INT_MAX));
    } else {
        index_ = ListIndex();
        rows_ = 0;
    }
    endResetModel();
}

void ListModel::refresh() {
    if (!store_)
        return;
    // Signals named before the design defined them may exist now.
    bool missing = false;
    for (std::size_t i = 0; i < columns_.size(); ++i)
        if (ids_[i] == nullptr && columns_[i].path.empty() == false && columns_[i].path[0] != '=' &&
            lookup(columns_[i].path) != nullptr)
            missing = true;
    if (missing) {
        rebuildIndex();
        return;
    }
    for (auto& [text, e] : exprs_) // expression columns follow the run incrementally
        e->update();
    const int before = rows_;
    index_.update();
    const int now = static_cast<int>(std::min<std::size_t>(index_.rowCount(), INT_MAX));
    if (now > before) {
        beginInsertRows({}, before, now - 1);
        rows_ = now;
        endInsertRows();
    }
    if (before > 0) // the last old row may have got more delta cycles
        emit dataChanged(index(before - 1, 0), index(before - 1, columnCount() - 1));
}

int ListModel::addSignals(const std::vector<std::string>& paths, std::optional<Radix> radix,
                          const std::string& label) {
    int added = 0;
    for (const std::string& p : paths) {
        Column c;
        c.path = p;
        c.radix = radix.value_or(Radix::Default);
        if (paths.size() == 1)
            c.label = label;
        columns_.push_back(std::move(c));
        ++added;
    }
    if (added > 0) {
        rebuildIndex();
        emit columnsChanged();
    }
    return added;
}

void ListModel::removeSignalColumn(int i) {
    if (i < 0 || i >= static_cast<int>(columns_.size()))
        return;
    columns_.erase(columns_.begin() + i);
    if (mode_.trigger == ListTrigger::Signal &&
        std::none_of(columns_.begin(), columns_.end(),
                     [&](const Column& c) { return c.path == mode_.triggerPath; }))
        mode_ = ListRowMode{}; // the trigger signal is gone
    rebuildIndex();
    emit columnsChanged();
}

bool ListModel::moveSignalColumn(int from, int to) {
    const int n = static_cast<int>(columns_.size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to)
        return false;
    // Table columns are signal columns + 1; Qt wants the destination as "insert before" in the old numbering.
    const int dest = to > from ? to + 2 : to + 1;
    beginMoveColumns({}, from + 1, from + 1, {}, dest);
    Column c = std::move(columns_[static_cast<std::size_t>(from)]);
    const Signal* id = ids_[static_cast<std::size_t>(from)];
    columns_.erase(columns_.begin() + from);
    ids_.erase(ids_.begin() + from);
    columns_.insert(columns_.begin() + to, std::move(c));
    ids_.insert(ids_.begin() + to, id);
    endMoveColumns();
    emit columnsChanged();
    return true;
}

void ListModel::setRadix(int i, Radix radix) {
    if (i < 0 || i >= static_cast<int>(columns_.size()))
        return;
    columns_[static_cast<std::size_t>(i)].radix = radix;
    emit dataChanged(index(0, i + 1), index(std::max(0, rows_ - 1), i + 1));
    emit headerDataChanged(Qt::Horizontal, i + 1, i + 1);
    emit columnsChanged();
}

void ListModel::clearColumns() {
    columns_.clear();
    mode_ = ListRowMode{};
    rebuildIndex();
    emit columnsChanged();
}

void ListModel::setRowMode(const ListRowMode& mode) {
    if (mode == mode_)
        return;
    mode_ = mode;
    rebuildIndex();
    emit columnsChanged();
}

void ListModel::setTimeUnitExp(int exp) {
    unitChosen_ = true;
    if (exp == unitExp_)
        return;
    unitExp_ = exp;
    emit headerDataChanged(Qt::Horizontal, 0, 0);
    if (rows_ > 0)
        emit dataChanged(index(0, 0), index(rows_ - 1, 0));
}

QString ListModel::timeHeader() const {
    return tr("Time (%1)").arg(QLatin1String(unitName(unitExp_)));
}

QString ListModel::timeValue(Time t) const {
    const int ts = store_ ? store_->timescaleExponent() : -15;
    const int shift = ts - unitExp_; // t * 10^shift
    if (shift >= 0)
        return QString::number(t * pow10u(shift));
    const std::uint64_t div = pow10u(-shift);
    const std::uint64_t q = t / div, r = t % div;
    if (r == 0)
        return QString::number(q);
    QString frac = QString::number(r).rightJustified(-shift, QLatin1Char('0'));
    while (frac.endsWith(QLatin1Char('0')))
        frac.chop(1);
    return QString::number(q) + QLatin1Char('.') + frac;
}

Time ListModel::rowTime(int row) const {
    return index_.rowTime(static_cast<std::size_t>(std::max(0, row)));
}

int ListModel::rowAtOrBefore(Time t) const {
    const std::size_t r = index_.rowAtOrBefore(t);
    if (r == kNoIndex)
        return -1;
    return static_cast<int>(std::min<std::size_t>(r, static_cast<std::size_t>(std::max(0, rows_ - 1))));
}

QString ListModel::cellText(int row, int column) const {
    if (row < 0 || row >= rows_)
        return {};
    const Time t = rowTime(row);
    if (column == 0)
        return timeValue(t);
    const std::size_t c = static_cast<std::size_t>(column - 1);
    if (c >= columns_.size() || ids_[c] == nullptr)
        return QStringLiteral("?");
    const Signal& s = *ids_[c];
    const std::size_t i = s.holdIndex(t);
    if (i == kNoIndex)
        return QStringLiteral("-");
    return QString::fromStdString(formatValue(s, i, columns_[c].radix));
}

QString ListModel::headerText(int column) const {
    if (column == 0)
        return timeHeader();
    const Column& c = columns_.at(static_cast<std::size_t>(column - 1));
    if (!c.label.empty())
        return QString::fromStdString(c.label);
    if (!c.path.empty() && c.path[0] == '=')
        return QString::fromStdString(c.path.substr(1));
    const WaveSpace::Split sp = WaveSpace::split(c.path); // "ref:tb.dut.q" -> "ref:dut/q"
    const std::string path(sp.path);
    const auto dot = path.find('.');
    QString rel = QString::fromStdString(dot == std::string::npos ? path : path.substr(dot + 1));
    rel.replace(QLatin1Char('.'), QLatin1Char('/'));
    return sp.dataset.empty() ? rel
                              : QString::fromStdString(std::string(sp.dataset)) + QLatin1Char(':') + rel;
}

QString ListModel::rowsText(const std::vector<int>& rows, QChar sep) const {
    QString out;
    for (int c = 0; c < columnCount(); ++c) {
        if (c > 0)
            out += sep;
        out += headerText(c);
    }
    out += QLatin1Char('\n');
    for (int r : rows) {
        for (int c = 0; c < columnCount(); ++c) {
            if (c > 0)
                out += sep;
            out += cellText(r, c);
        }
        out += QLatin1Char('\n');
    }
    return out;
}

bool ListModel::writeCsv(QIODevice& out, QString* error) const {
    QString line = csvField(timeHeader());
    for (const Column& c : columns_)
        line += QLatin1Char(',') +
                csvField(c.label.empty() ? slashPath(c.path) : QString::fromStdString(c.label));
    QByteArray buf = line.toUtf8() + "\r\n";
    for (int r = 0; r < rows_; ++r) {
        line = timeValue(rowTime(r));
        for (int c = 1; c < columnCount(); ++c)
            line += QLatin1Char(',') + csvField(cellText(r, c));
        buf += line.toUtf8();
        buf += "\r\n";
        if (buf.size() > (1 << 20)) {
            if (out.write(buf) != buf.size()) {
                if (error)
                    *error = out.errorString();
                return false;
            }
            buf.clear();
        }
    }
    if (out.write(buf) != buf.size()) {
        if (error)
            *error = out.errorString();
        return false;
    }
    return true;
}

int ListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : rows_;
}

int ListModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(columns_.size()) + 1;
}

QVariant ListModel::data(const QModelIndex& idx, int role) const {
    if (!idx.isValid())
        return {};
    switch (role) {
    case Qt::DisplayRole:
        return cellText(idx.row(), idx.column());
    case Qt::TextAlignmentRole:
        return QVariant::fromValue(Qt::Alignment(idx.column() == 0 ? Qt::AlignRight | Qt::AlignVCenter
                                                                   : Qt::AlignLeft | Qt::AlignVCenter));
    case Qt::FontRole: {
        if (idx.column() == 0 || !store_)
            return {};
        const std::size_t c = static_cast<std::size_t>(idx.column() - 1);
        if (c >= ids_.size() || ids_[c] == nullptr)
            return {};
        const Time t = rowTime(idx.row());
        const auto [lo, hi] = ids_[c]->changesIn(t, t + 1);
        if (lo == hi)
            return {};
        QFont f;
        f.setBold(true);
        return f;
    }
    default:
        return {};
    }
}

QVariant ListModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || section < 0 || section >= columnCount())
        return QAbstractTableModel::headerData(section, orientation, role);
    if (role == Qt::DisplayRole)
        return headerText(section);
    if (role == Qt::ToolTipRole && section > 0) {
        const Column& c = columns_[static_cast<std::size_t>(section - 1)];
        QString tip =
            slashPath(c.path) + QStringLiteral(" (") + QLatin1String(radixName(c.radix)) + QLatin1Char(')');
        if (ids_[static_cast<std::size_t>(section - 1)] == nullptr)
            tip += tr(" - not in the waveform");
        return tip;
    }
    return {};
}

} // namespace lvd::gui
