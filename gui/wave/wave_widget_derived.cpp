// SPDX-License-Identifier: GPL-3.0-or-later
// WaveWidget: datasets (other waveforms next to the current one), expression rows and compare rows. The
// computing is in libwave (lvdwave/wave_space.hpp, expression.hpp, compare.hpp); this file keeps their
// results for the rows, follows the live run and offers the dialogs and the navigation over differences.
#include "rhi/rhi_wave_canvas.hpp"
#include "wave/compare_dialog.hpp"
#include "wave/expression_dialog.hpp"
#include "wave/view_state.hpp"
#include "wave/wave_widget.hpp"

#include <QMenu>

#include <algorithm>
#include <cmath>
#include <limits>

namespace lvd::gui {

using namespace lvd::wave;

namespace {

std::string compareKey(const DisplayItem& item) {
    const std::string ref = item.members.size() > 0 ? item.members[0].path : std::string();
    const std::string test = item.members.size() > 1 ? item.members[1].path : std::string();
    return ref + '\n' + test + '\n' + std::to_string(item.tolerance);
}

QString waveformName(const QString& dataset) {
    return dataset.isEmpty() ? QObject::tr("current") : dataset;
}

} // namespace

// ---- Menu
// ---------------------------------------------------------------------------------------------------

void WaveWidget::populateDerivedMenu(QMenu* menu, int row) {
    if (row >= rowCount())
        row = -1;
    const Row* clicked = row >= 0 ? &rows_[static_cast<std::size_t>(row)] : nullptr;
    QAction* addExpr = menu->addAction(tr("Add expression signal..."));
    addExpr->setObjectName("addExpression");
    addExpr->setEnabled(store_ != nullptr || !space_.datasetNames().empty());
    connect(addExpr, &QAction::triggered, this, [this] { showExpressionDialog(); });
    if (clicked != nullptr && clicked->item->kind == ItemKind::Expression && clicked->bit < 0) {
        QAction* edit = menu->addAction(tr("Edit expression..."));
        edit->setObjectName("editExpression");
        const ItemId id = clicked->item->id;
        connect(edit, &QAction::triggered, this, [this, id] { showExpressionDialog(id); });
    }
    const QStringList datasets = datasetNames();
    if (!datasets.isEmpty()) {
        bool signalSelected = false;
        for (const DisplayItem* it : selectedItems())
            signalSelected = signalSelected ||
                             (it->kind == ItemKind::Signal && WaveSpace::split(it->path).dataset.empty());
        QMenu* fromDs = menu->addMenu(tr("Add from dataset"));
        fromDs->setObjectName("addFromDatasetMenu");
        QMenu* cmpDs = menu->addMenu(tr("Compare with dataset"));
        cmpDs->setObjectName("compareWithMenu");
        fromDs->setEnabled(signalSelected);
        cmpDs->setEnabled(signalSelected);
        for (const QString& ds : datasets) {
            QAction* a = fromDs->addAction(ds);
            a->setObjectName("addFromDataset_" + ds);
            connect(a, &QAction::triggered, this, [this, ds] { addDatasetRowsForSelection(ds); });
            QAction* c = cmpDs->addAction(ds);
            c->setObjectName("compareWith_" + ds);
            connect(c, &QAction::triggered, this, [this, ds] { compareSelectionWith(ds); });
        }
        QAction* cmpAll = menu->addAction(tr("Compare waveforms..."));
        cmpAll->setObjectName("compareWaveforms");
        connect(cmpAll, &QAction::triggered, this, [this] { showCompareDialog(); });
    }
    if (compareRowCount() > 0) {
        QAction* next = menu->addAction(tr("Next difference"));
        next->setObjectName("nextDifference");
        connect(next, &QAction::triggered, this, [this] { jumpToDifference(+1); });
        QAction* prev = menu->addAction(tr("Previous difference"));
        prev->setObjectName("previousDifference");
        connect(prev, &QAction::triggered, this, [this] { jumpToDifference(-1); });
    }
}

// ---- Caches
// -------------------------------------------------------------------------------------------------

void WaveWidget::dropDerivedCaches() {
    indexes_.clear();
    busCache_.clear();
    bitCache_.clear();
    exprCache_.clear();
    compareCache_.clear();
}

void WaveWidget::pruneDerivedCaches() {
    auto forget = [&](const Signal* s) {
        for (auto it = indexes_.begin(); it != indexes_.end();)
            it = it->first.first == s ? indexes_.erase(it) : std::next(it);
        for (auto it = bitCache_.begin(); it != bitCache_.end();)
            it = it->first.first == s ? bitCache_.erase(it) : std::next(it);
    };
    for (auto it = exprCache_.begin(); it != exprCache_.end();) {
        if (it->second.used) {
            ++it;
            continue;
        }
        if (it->second.signal)
            forget(&it->second.signal->signal());
        it = exprCache_.erase(it);
    }
    for (auto it = compareCache_.begin(); it != compareCache_.end();) {
        if (it->second.used) {
            ++it;
            continue;
        }
        if (it->second.compare)
            forget(&it->second.compare->differences());
        it = compareCache_.erase(it);
    }
}

void WaveWidget::derivedRowsChanged() {
    dropDerivedCaches();
    ++storeRevision_;
    cache_.clear();
    if (canvas_ != nullptr)
        canvas_->requestCacheClear();
    rebuildRows();
    clampView();
    emitViewChanged();
}

const Signal* WaveWidget::expressionSignal(const DisplayItem& item, QString* error, Time* dataEnd) {
    ExprEntry& e = exprCache_[item.expr];
    e.used = true;
    if (!e.signal && e.error.isEmpty()) {
        // the resolver of the space, noting which waveforms the names come from
        std::vector<std::string>& used = e.datasets;
        const SignalResolver resolve = [this, &used](std::string_view name, std::string& err) {
            const Signal* s = space_.findName(name, err);
            const std::string ds(WaveSpace::split(name).dataset);
            if (s != nullptr && std::find(used.begin(), used.end(), ds) == used.end())
                used.push_back(ds);
            return s;
        };
        auto r = ExpressionSignal::create(item.expr, resolve, item.displayName());
        if (r.ok())
            e.signal = std::move(r).value();
        else
            e.error = QString::fromStdString(r.error());
    }
    if (error != nullptr)
        *error = e.error;
    if (dataEnd != nullptr && e.signal) {
        // an expression is known where all of its inputs are
        Time end = std::numeric_limits<Time>::max();
        for (const std::string& ds : e.datasets)
            if (const WaveStore* st = space_.store(ds))
                end = std::min(end, st->endTime());
        *dataEnd = end;
    }
    return e.signal ? &e.signal->signal() : nullptr;
}

Time WaveWidget::compareUntil(const DisplayItem& item) const {
    if (item.members.size() < 2)
        return 0;
    return std::min(space_.endTimeOf(item.members[0].path), space_.endTimeOf(item.members[1].path));
}

const Signal* WaveWidget::compareSignal(const DisplayItem& item, QString* error) {
    CompareEntry& e = compareCache_[compareKey(item)];
    e.used = true;
    if (!e.compare && e.error.isEmpty()) {
        const Signal* ref = item.members.size() > 0 ? space_.findPath(item.members[0].path) : nullptr;
        const Signal* test = item.members.size() > 1 ? space_.findPath(item.members[1].path) : nullptr;
        if (ref == nullptr || test == nullptr) {
            const std::string& missing = ref == nullptr ? item.members.at(0).path : item.members.at(1).path;
            e.error = tr("%1 is not in the waveforms").arg(QString::fromStdString(commandPath(missing)));
        } else {
            CompareOptions o;
            o.tolerance = item.tolerance;
            e.compare = std::make_unique<SignalCompare>(*ref, *test, o, item.displayName());
            e.compare->update(compareUntil(item));
        }
    }
    if (error != nullptr)
        *error = e.error;
    return e.compare ? &e.compare->differences() : nullptr;
}

const SignalCompare* WaveWidget::compareOf(const DisplayItem& item) const {
    const auto it = compareCache_.find(compareKey(item));
    return it == compareCache_.end() ? nullptr : it->second.compare.get();
}

// ---- Datasets ---------------------------------------------------------------------------------------------

QString WaveWidget::addDataset(const QString& name, std::shared_ptr<const WaveStore> store) {
    const std::string err = space_.addDataset(name.toStdString(), std::move(store));
    if (!err.empty())
        return QString::fromStdString(err);
    derivedRowsChanged();
    emit datasetsChanged();
    return {};
}

bool WaveWidget::removeDataset(const QString& name) {
    if (!space_.removeDataset(name.toStdString()))
        return false;
    derivedRowsChanged();
    emit datasetsChanged();
    return true;
}

QStringList WaveWidget::datasetNames() const {
    QStringList out;
    for (const std::string& n : space_.datasetNames())
        out << QString::fromStdString(n);
    return out;
}

const Signal* WaveWidget::rowSignal(int row) const {
    if (row < 0 || row >= rowCount())
        return nullptr;
    return rows_[static_cast<std::size_t>(row)].signal;
}

QString WaveWidget::rowError(int row) const {
    if (row < 0 || row >= rowCount())
        return {};
    return rows_[static_cast<std::size_t>(row)].error;
}

// ---- Expressions ------------------------------------------------------------------------------------------

QString WaveWidget::checkExpression(const QString& text, QString* description) const {
    auto r = Expression::compile(text.toStdString(), space_.resolver());
    if (!r.ok())
        return QString::fromStdString(r.error());
    if (description != nullptr)
        *description = QString::fromStdString(r.value().describe());
    return {};
}

bool WaveWidget::addExpression(const QString& name, const QString& text, Radix radix) {
    lastEditError_.clear();
    auto refuse = [&](const QString& why) {
        lastEditError_ = why;
        emit editRejected(why);
        return false;
    };
    const QString label = name.trimmed();
    if (label.isEmpty())
        return refuse(tr("An expression signal needs a name"));
    if (const QString err = checkExpression(text); !err.isEmpty())
        return refuse(tr("Expression: %1").arg(err));

    const DisplayItem* after = nullptr;
    for (const Row& r : rows_)
        if (r.bit < 0 && isSelected(r))
            after = r.item;
    DisplayItem item = DisplayModel::makeExpression(label.toStdString(), text.trimmed().toStdString(), radix);
    const ItemId id = item.id;
    Snapshot before{model_, selected_, tr("Add expression signal")};
    insertAfter(model_, after, std::move(item));
    pushUndo(std::move(before));
    selected_.assign(1, RowKey{id, -1});
    rebuildRows();
    ensureRowVisible(rowOfItem(id));
    emit selectionChanged();
    emit structureChanged();
    return true;
}

bool WaveWidget::editExpression(ItemId id, const QString& name, const QString& text) {
    lastEditError_.clear();
    auto refuse = [&](const QString& why) {
        lastEditError_ = why;
        emit editRejected(why);
        return false;
    };
    DisplayItem* it = findItem(model_, id);
    if (it == nullptr || it->kind != ItemKind::Expression)
        return refuse(tr("Not an expression row"));
    if (name.trimmed().isEmpty())
        return refuse(tr("An expression signal needs a name"));
    if (const QString err = checkExpression(text); !err.isEmpty())
        return refuse(tr("Expression: %1").arg(err));
    if (it->label == name.trimmed().toStdString() && it->expr == text.trimmed().toStdString())
        return false;
    Snapshot before{model_, selected_, tr("Edit expression signal")};
    it = findItem(model_, id);
    it->label = name.trimmed().toStdString();
    it->expr = text.trimmed().toStdString();
    pushUndo(std::move(before));
    rebuildRows();
    emit structureChanged();
    return true;
}

bool WaveWidget::showExpressionDialog(ItemId edit) {
    QStringList paths;
    for (const Row& r : rows_)
        if (r.bit < 0 && r.item->kind == ItemKind::Signal) {
            const QString p = QString::fromStdString(commandPath(r.item->path));
            if (!paths.contains(p))
                paths << p;
        }
    ExpressionDialog dialog(
        [this](const QString& text, QString* desc) { return checkExpression(text, desc); }, paths, this);
    if (edit != 0) {
        const DisplayItem* it = findItem(model_, edit);
        if (it == nullptr || it->kind != ItemKind::Expression)
            return false;
        dialog.setWindowTitle(tr("Edit Expression Signal"));
        dialog.setValues(QString::fromStdString(it->displayName()), QString::fromStdString(it->expr));
    }
    const bool accepted = exprRunner_ ? exprRunner_(dialog) : dialog.exec() == QDialog::Accepted;
    if (!accepted || !dialog.isValid())
        return false;
    return edit != 0 ? editExpression(edit, dialog.name(), dialog.expression())
                     : addExpression(dialog.name(), dialog.expression());
}

// ---- Compare ----------------------------------------------------------------------------------------------

std::size_t WaveWidget::insertBelowSelectedSignals(
    const QString& text, const std::function<std::vector<DisplayItem>(const DisplayItem&)>& make) {
    std::vector<ItemId> ids;
    for (const Row& r : rows_)
        if (r.bit < 0 && isSelected(r) && r.item->kind == ItemKind::Signal &&
            WaveSpace::split(r.item->path).dataset.empty() &&
            std::find(ids.begin(), ids.end(), r.item->id) == ids.end())
            ids.push_back(r.item->id);
    if (ids.empty())
        return 0;
    Snapshot before{model_, selected_, text};
    std::size_t added = 0;
    std::vector<RowKey> created;
    for (ItemId id : ids) {
        const DisplayItem* it = findItem(model_, id);
        if (it == nullptr)
            continue;
        std::vector<DisplayItem> items = make(*it);
        for (auto k = items.size(); k-- > 0;) {
            created.push_back(RowKey{items[k].id, -1});
            insertAfter(model_, findItem(model_, id), std::move(items[k]));
            ++added;
        }
    }
    if (added == 0)
        return 0;
    pushUndo(std::move(before));
    selected_ = created;
    rebuildRows();
    emit selectionChanged();
    emit structureChanged();
    return added;
}

std::size_t WaveWidget::addDatasetRowsForSelection(const QString& dataset) {
    const std::string ds = dataset.toStdString();
    if (space_.store(ds) == nullptr || ds.empty())
        return 0;
    return insertBelowSelectedSignals(tr("Add from dataset %1").arg(dataset), [&](const DisplayItem& it) {
        std::vector<DisplayItem> out;
        const std::string path = WaveSpace::join(ds, it.path);
        if (space_.findPath(path) != nullptr) {
            DisplayItem copy = DisplayModel::makeSignal(path, it.radix);
            copy.showBits = false;
            out.push_back(std::move(copy));
        }
        return out;
    });
}

std::size_t WaveWidget::compareSelectionWith(const QString& dataset, Time tolerance) {
    const std::string ds = dataset.toStdString();
    lastEditError_.clear();
    if (space_.store(ds) == nullptr || ds.empty()) {
        lastEditError_ = tr("No dataset named %1").arg(dataset);
        emit editRejected(lastEditError_);
        return 0;
    }
    std::size_t compares = 0;
    QStringList missing;
    insertBelowSelectedSignals(tr("Compare with %1").arg(dataset), [&](const DisplayItem& it) {
        std::vector<DisplayItem> out;
        const std::string path = WaveSpace::join(ds, it.path);
        if (space_.findPath(path) == nullptr) {
            missing << QString::fromStdString(commandPath(it.path));
            return out;
        }
        out.push_back(DisplayModel::makeSignal(path, it.radix));
        out.push_back(DisplayModel::makeCompare("diff " + it.displayName(), path, it.path, tolerance));
        ++compares;
        return out;
    });
    if (!missing.isEmpty()) {
        lastEditError_ = tr("Not in %1: %2").arg(dataset, missing.join(QStringLiteral(", ")));
        emit editRejected(lastEditError_);
    }
    if (compares > 0)
        emit navigationMessage(
            tr("%n difference(s) in the compared rows", nullptr, static_cast<int>(differenceCount())));
    return compares;
}

StoreDiff WaveWidget::compareDatasets(const QString& ref, const QString& test, Time tolerance,
                                      const QString& scope, std::size_t maxRows) {
    lastEditError_.clear();
    const WaveStore* rs = space_.store(ref.toStdString());
    const WaveStore* ts = space_.store(test.toStdString());
    if (rs == nullptr || ts == nullptr || rs == ts) {
        lastEditError_ =
            rs == ts && rs != nullptr
                ? tr("Choose two different waveforms")
                : tr("No waveform named %1").arg(rs == nullptr ? waveformName(ref) : waveformName(test));
        emit editRejected(lastEditError_);
        return {};
    }
    CompareOptions o;
    o.tolerance = tolerance;
    StoreDiff d = compareStores(*rs, *ts, o, scope.toStdString());
    QString summary = tr("%1 vs %2: %3 signal(s) compared, %4 differ, %5 difference(s)")
                          .arg(waveformName(ref), waveformName(test))
                          .arg(d.compared)
                          .arg(d.differing.size())
                          .arg(d.total);
    if (!d.onlyInRef.empty() || !d.onlyInTest.empty())
        summary += tr("; %1 only in %2, %3 only in %4")
                       .arg(d.onlyInRef.size())
                       .arg(waveformName(ref))
                       .arg(d.onlyInTest.size())
                       .arg(waveformName(test));
    if (d.differing.empty()) {
        emit navigationMessage(summary);
        return d;
    }
    DisplayItem group = DisplayModel::makeGroup(
        tr("Compare %1 vs %2").arg(waveformName(ref), waveformName(test)).toStdString());
    const std::string rd = ref.toStdString(), td = test.toStdString();
    for (std::size_t i = 0; i < d.differing.size() && i < maxRows; ++i) {
        const std::string& p = d.differing[i].path;
        group.children.push_back(DisplayModel::makeCompare(commandPath(p), WaveSpace::join(rd, p),
                                                           WaveSpace::join(td, p), tolerance));
    }
    if (d.differing.size() > maxRows)
        summary += tr("; the first %1 are shown").arg(maxRows);
    const ItemId gid = group.id;
    Snapshot before{model_, selected_, tr("Compare waveforms")};
    model_.root().children.push_back(std::move(group));
    pushUndo(std::move(before));
    selected_.assign(1, RowKey{gid, -1});
    rebuildRows();
    ensureRowVisible(rowOfItem(gid));
    emit selectionChanged();
    emit structureChanged();
    emit navigationMessage(summary);
    return d;
}

bool WaveWidget::showCompareDialog() {
    CompareDialog dialog(datasetNames(), store_ ? store_->timescaleExponent() : -15, this);
    const bool accepted = compareRunner_ ? compareRunner_(dialog) : dialog.exec() == QDialog::Accepted;
    if (!accepted || !dialog.isValid())
        return false;
    const StoreDiff d = compareDatasets(dialog.reference(), dialog.test(),
                                        static_cast<Time>(dialog.tolerance()), dialog.scope());
    return !d.differing.empty();
}

int WaveWidget::compareRowCount() const {
    int n = 0;
    for (const Row& r : rows_)
        if (r.item->kind == ItemKind::Compare && r.bit < 0)
            ++n;
    return n;
}

std::size_t WaveWidget::differenceCount() const {
    std::size_t n = 0;
    std::vector<const SignalCompare*> seen;
    for (const Row& r : rows_) {
        if (r.item->kind != ItemKind::Compare || r.bit >= 0)
            continue;
        const SignalCompare* c = compareOf(*r.item);
        if (c == nullptr || std::find(seen.begin(), seen.end(), c) != seen.end())
            continue;
        seen.push_back(c);
        n += c->count();
    }
    return n;
}

bool WaveWidget::jumpToDifference(int dir) {
    if (dir == 0)
        return false;
    std::vector<const Row*> rows;
    for (const Row& r : rows_)
        if (r.item->kind == ItemKind::Compare && r.bit < 0 && isSelected(r))
            rows.push_back(&r);
    const bool selection = !rows.empty();
    if (!selection)
        for (const Row& r : rows_)
            if (r.item->kind == ItemKind::Compare && r.bit < 0)
                rows.push_back(&r);
    if (rows.empty()) {
        emit navigationMessage(tr("There are no compare rows"));
        return false;
    }
    const auto cursor = activeCursorTime();
    const double ref = cursor ? *cursor : view_.startTime;
    const std::optional<Time> from =
        static_cast<Time>(dir > 0 ? std::floor(std::max(0.0, ref)) : std::ceil(std::max(0.0, ref)));
    const SearchDirection sd = dir > 0 ? SearchDirection::Forward : SearchDirection::Backward;
    std::optional<Time> best;
    const Row* bestRow = nullptr;
    for (const Row* r : rows) {
        const SignalCompare* c = compareOf(*r->item);
        if (c == nullptr)
            continue;
        const auto hit = findDifference(*c, from, sd);
        if (hit && (!best || (dir > 0 ? *hit < *best : *hit > *best))) {
            best = hit;
            bestRow = r;
        }
    }
    const std::size_t total = differenceCount();
    if (!best) {
        emit navigationMessage(tr("No difference %1 %2 on %3 (%4 in all)")
                                   .arg(dir > 0 ? tr("after") : tr("before"), timeText(ref),
                                        selection ? tr("the selected compare rows") : tr("the compare rows"))
                                   .arg(total));
        return false;
    }
    moveActiveCursorTo(static_cast<double>(*best));
    emit navigationMessage(
        tr("Difference at %1 in %2 (%3 in all)")
            .arg(timeText(static_cast<double>(*best)), QString::fromStdString(bestRow->item->displayName()))
            .arg(total));
    return true;
}

} // namespace lvd::gui
