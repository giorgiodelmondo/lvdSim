// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/coverage_pane.hpp"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <map>

namespace lvd::gui {

namespace cov = lvd::tools::cov;

namespace {

constexpr int kFileRole = Qt::UserRole + 3;
constexpr int kLineRole = Qt::UserRole + 4;
constexpr int kMissRole = Qt::UserRole + 5; ///< true on a detail row of something not covered

const QColor kMissRed(0xc0, 0x30, 0x30);

QColor barColour(double pct) {
    if (pct >= 100.0)
        return {0x4c, 0xaf, 0x50};
    if (pct >= 80.0)
        return {0x9c, 0xcc, 0x45};
    if (pct >= 50.0)
        return {0xf0, 0xa8, 0x30};
    return {0xe0, 0x50, 0x50};
}

/// A percentage cell: a bar as wide as the value, coloured from red to green, with the number on it.
class BarDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const QVariant v = index.data(CoveragePane::kPercentRole);
        if (!v.isValid()) {
            QStyledItemDelegate::paint(p, option, index);
            return;
        }
        QStyleOptionViewItem o = option;
        initStyleOption(&o, index);
        const QString text = o.text;
        o.text.clear();
        o.widget->style()->drawControl(QStyle::CE_ItemViewItem, &o, p, o.widget);
        const double pct = v.toDouble();
        const QRect r = option.rect.adjusted(3, 3, -3, -3);
        p->save();
        if (pct >= 0) {
            p->fillRect(r, QColor(0xe8, 0xe8, 0xe8));
            const int w = static_cast<int>(r.width() * qBound(0.0, pct, 100.0) / 100.0 + 0.5);
            p->fillRect(QRect(r.left(), r.top(), w, r.height()), barColour(pct));
            p->setPen(QColor(0x20, 0x20, 0x20));
        } else {
            p->setPen(option.palette.color(QPalette::Mid));
        }
        p->drawText(r, Qt::AlignCenter, text);
        p->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QSize s = QStyledItemDelegate::sizeHint(option, index);
        s.setWidth(qMax(s.width(), 64));
        return s;
    }
};

} // namespace

QString CoveragePane::unitText(const std::string& blockName) {
    // ENTITY-ARCHITECTURE -> entity(architecture). An extended identifier (\my-unit\) may hold a dash itself:
    // the separator follows its closing backslash.
    const std::string& n = blockName;
    const std::size_t from = !n.empty() && n.front() == '\\' ? n.find('\\', 1) : 0;
    const std::size_t dash = from == std::string::npos ? std::string::npos : n.find('-', from);
    if (dash == std::string::npos)
        return QString::fromStdString(cov::displayName(n));
    return QString::fromStdString(cov::displayName(n.substr(0, dash)) + "(" +
                                  cov::displayName(n.substr(dash + 1)) + ")");
}

namespace {

QString countsTip(const cov::Counts& c, cov::Kind k) {
    const int i = static_cast<int>(k);
    return QStringLiteral("%1 of %2").arg(c.hit[i]).arg(c.total[i]);
}

void setPercent(QTreeWidgetItem* item, int column, double pct) {
    item->setData(column, CoveragePane::kPercentRole, pct);
    item->setText(column, CoveragePane::percentText(pct));
    item->setTextAlignment(column, Qt::AlignCenter);
}

} // namespace

QString CoveragePane::kindName(cov::Kind k, qint64 n) {
    const bool one = n == 1;
    switch (k) {
    case cov::Kind::Statement:
        return one ? tr("statement") : tr("statements");
    case cov::Kind::Branch:
        return one ? tr("branch") : tr("branches");
    case cov::Kind::Expression:
        return one ? tr("expression") : tr("expressions");
    case cov::Kind::Toggle:
        return one ? tr("toggle") : tr("toggles");
    case cov::Kind::State:
        return one ? tr("FSM state") : tr("FSM states");
    case cov::Kind::Functional:
        return one ? tr("functional item") : tr("functional items");
    }
    return {};
}

QString CoveragePane::percentText(double percent) {
    if (percent < 0)
        return QStringLiteral("-");
    if (percent >= 100.0)
        return QStringLiteral("100%");
    // rounded as nvc's report does, but never 100% for something not complete
    const double shown = std::min(std::round(percent * 10.0) / 10.0, 99.9);
    return QString::number(shown, 'f', 1) + QLatin1Char('%');
}

CoveragePane::CoveragePane(QWidget* parent)
    : QWidget(parent) {
    summary_ = new QLabel(this);
    summary_->setObjectName("coverageSummary");
    summary_->setWordWrap(true);
    summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    filter_ = new QLineEdit(this);
    filter_->setObjectName("coverageFilter");
    filter_->setPlaceholderText(tr("Filter instances"));
    filter_->setClearButtonEnabled(true);
    uncovered_ = new QCheckBox(tr("Uncovered only"), this);
    uncovered_->setObjectName("coverageUncoveredOnly");
    uncovered_->setToolTip(tr("Show only instances and items that are not fully covered"));

    tree_ = new QTreeWidget(this);
    tree_->setObjectName("coverageTree");
    QStringList headers = {tr("Instance"), tr("Design unit")};
    for (cov::Kind k : cov::kAllKinds)
        headers << QString::fromLatin1(cov::kindLabel(k));
    headers << tr("Total");
    tree_->setHeaderLabels(headers);
    tree_->setUniformRowHeights(true);
    tree_->setAlternatingRowColors(true);
    auto* bars = new BarDelegate(tree_);
    for (int c = kFirstKindColumn; c <= kTotalColumn; ++c)
        tree_->setItemDelegateForColumn(c, bars);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(kInstanceColumn, QHeaderView::Interactive);
    tree_->setColumnWidth(kInstanceColumn, 170);
    tree_->setColumnWidth(kUnitColumn, 140);
    for (int c = kFirstKindColumn; c <= kTotalColumn; ++c)
        tree_->setColumnWidth(c, 66);

    details_ = new QTreeWidget(this);
    details_->setObjectName("coverageDetails");
    details_->setHeaderLabels({tr("Item"), tr("Count"), tr("Line")});
    details_->setUniformRowHeights(true);
    details_->setColumnWidth(0, 260);
    details_->setColumnWidth(1, 110);

    auto* bar = new QHBoxLayout;
    bar->addWidget(filter_, 1);
    bar->addWidget(uncovered_);
    auto* split = new QSplitter(Qt::Vertical, this);
    split->addWidget(tree_);
    split->addWidget(details_);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->addWidget(summary_);
    layout->addLayout(bar);
    layout->addWidget(split, 1);

    connect(filter_, &QLineEdit::textChanged, this, [this] { applyFilter(); });
    connect(uncovered_, &QCheckBox::toggled, this, [this] {
        applyFilter();
        showDetails(selected_);
    });
    connect(tree_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* cur, QTreeWidgetItem*) { onCurrentInstance(cur); });
    connect(details_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* it, int) { onDetail(it); });
    connect(details_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* cur, QTreeWidgetItem*) { onDetail(cur); });
    clear();
}

void CoveragePane::setDatabase(std::shared_ptr<const cov::Database> db, const QString& origin) {
    db_ = std::move(db);
    origin_ = origin;
    selected_ = -1;
    rebuildTree();
}

void CoveragePane::clear(const QString& message) {
    db_.reset();
    origin_.clear();
    selected_ = -1;
    tree_->clear();
    details_->clear();
    summary_->setText(message.isEmpty()
                          ? tr("No coverage loaded. Enable Simulation > Collect coverage, or open a "
                               "coverage database with File > Open coverage.")
                          : message);
}

QString CoveragePane::fileName(int file) const {
    if (!db_ || file < 0 || static_cast<std::size_t>(file) >= db_->files.size())
        return {};
    return QString::fromStdString(db_->files[static_cast<std::size_t>(file)]);
}

void CoveragePane::rebuildTree() {
    const QSignalBlocker block(tree_);
    tree_->clear();
    details_->clear();
    if (!db_) {
        clear();
        return;
    }
    const cov::Counts all = db_->totals();
    for (cov::Kind k : cov::kAllKinds)
        tree_->setColumnHidden(kFirstKindColumn + static_cast<int>(k), !all.has(k));
    QStringList parts;
    for (cov::Kind k : cov::kAllKinds)
        if (all.has(k))
            parts << QStringLiteral("%1 %2").arg(QString::fromLatin1(cov::kindLabel(k)),
                                                 percentText(all.percent(k)));
    // nvc instruments VHDL only: a Verilog design gives an empty database, a mixed one only its VHDL part.
    QString text = all.allTotal() == 0
                       ? tr("The coverage database has no items. nvc collects code coverage of "
                            "VHDL only; Verilog and SystemVerilog modules have none.")
                       : tr("<b>Total %1</b> (%2 of %3 items) &nbsp; %4")
                             .arg(percentText(all.overall()))
                             .arg(all.allHit())
                             .arg(all.allTotal())
                             .arg(parts.join(QStringLiteral(" &nbsp; ")));
    if (!origin_.isEmpty())
        text += QStringLiteral("<br><span style='color:gray'>%1</span>").arg(origin_.toHtmlEscaped());
    summary_->setText(text);
    for (int top : db_->topInstances())
        addInstance(nullptr, top);
    tree_->expandToDepth(1);
    applyFilter();
}

void CoveragePane::addInstance(QTreeWidgetItem* parent, int scope) {
    const cov::Scope& s = db_->scopes[static_cast<std::size_t>(scope)];
    auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree_);
    item->setText(kInstanceColumn, QString::fromStdString(cov::displayName(s.name)));
    item->setText(kUnitColumn, unitText(s.blockName));
    item->setData(kInstanceColumn, kScopeRole, scope);
    const cov::Counts rec = db_->counts(scope, true);
    const cov::Counts self = db_->counts(scope, false);
    item->setToolTip(kInstanceColumn, QString::fromStdString(cov::displayPath(*db_, scope)));
    for (cov::Kind k : cov::kAllKinds) {
        const int col = kFirstKindColumn + static_cast<int>(k);
        setPercent(item, col, rec.percent(k));
        if (rec.has(k))
            item->setToolTip(col, tr("%1 covered: %2 (with the instances below)\nthis instance alone: %3")
                                      .arg(kindName(k, 2), countsTip(rec, k), countsTip(self, k)));
    }
    setPercent(item, kTotalColumn, rec.overall());
    item->setToolTip(
        kTotalColumn,
        tr("%1 of %2 items covered (with the instances below)").arg(rec.allHit()).arg(rec.allTotal()));
    for (int child : db_->childInstances(scope))
        addInstance(item, child);
}

bool CoveragePane::filterItem(QTreeWidgetItem* item, const QString& text, bool uncovered) {
    bool anyChild = false;
    for (int i = 0; i < item->childCount(); ++i)
        anyChild = filterItem(item->child(i), text, uncovered) || anyChild;
    const int scope = item->data(kInstanceColumn, kScopeRole).toInt();
    const QString path = QString::fromStdString(cov::displayPath(*db_, scope));
    bool self = text.isEmpty() || path.contains(text, Qt::CaseInsensitive) ||
                item->text(kUnitColumn).contains(text, Qt::CaseInsensitive);
    if (uncovered) {
        const double pct = item->data(kTotalColumn, kPercentRole).toDouble();
        self = self && pct >= 0 && pct < 100.0;
    }
    const bool show = self || anyChild;
    item->setHidden(!show);
    return show;
}

void CoveragePane::applyFilter() {
    if (!db_)
        return;
    const QString text = filter_->text().trimmed();
    for (int i = 0; i < tree_->topLevelItemCount(); ++i)
        filterItem(tree_->topLevelItem(i), text, uncovered_->isChecked());
    if (!text.isEmpty() || uncovered_->isChecked())
        tree_->expandAll();
}

void CoveragePane::setFilter(const QString& text) {
    filter_->setText(text);
}

void CoveragePane::setUncoveredOnly(bool on) {
    uncovered_->setChecked(on);
}

bool CoveragePane::uncoveredOnly() const {
    return uncovered_->isChecked();
}

bool CoveragePane::selectInstance(const QString& path) {
    if (!db_)
        return false;
    const int scope = db_->findScope(path.toStdString());
    if (scope < 0)
        return false;
    for (QTreeWidgetItemIterator it(tree_); *it; ++it) {
        if ((*it)->data(kInstanceColumn, kScopeRole).toInt() != scope)
            continue;
        if ((*it)->isHidden())
            return false;
        tree_->setCurrentItem(*it);
        tree_->scrollToItem(*it);
        return true;
    }
    return false;
}

QString CoveragePane::selectedInstancePath() const {
    if (!db_ || selected_ < 0)
        return {};
    return QString::fromStdString(cov::displayPath(*db_, selected_));
}

QStringList CoveragePane::visibleInstancePaths() const {
    QStringList out;
    if (!db_)
        return out;
    for (QTreeWidgetItemIterator it(tree_); *it; ++it) {
        bool hidden = false;
        for (QTreeWidgetItem* p = *it; p && !hidden; p = p->parent())
            hidden = p->isHidden();
        if (!hidden)
            out << QString::fromStdString(
                cov::displayPath(*db_, (*it)->data(kInstanceColumn, kScopeRole).toInt()));
    }
    return out;
}

void CoveragePane::onCurrentInstance(QTreeWidgetItem* item) {
    if (!db_ || !item)
        return;
    selected_ = item->data(kInstanceColumn, kScopeRole).toInt();
    showDetails(selected_);
    emit instanceSelected(selected_);
    // The instance's own source: the file most of its items are in, at the first line with code not covered
    // (else the first line not covered, else the first line with an item).
    const int file = db_->mainFile(selected_);
    if (file < 0)
        return;
    const auto lines = cov::lineCoverage(*db_, selected_, file);
    int line = 0;
    for (const auto& [n, l] : lines) {
        const auto& m = l.missed;
        if (m[static_cast<int>(cov::Kind::Statement)] + m[static_cast<int>(cov::Kind::Branch)] +
                m[static_cast<int>(cov::Kind::Expression)] >
            0) {
            line = n;
            break;
        }
    }
    for (auto it = lines.begin(); line == 0 && it != lines.end(); ++it)
        if (!it->second.covered())
            line = it->first;
    if (line == 0 && !lines.empty())
        line = lines.begin()->first;
    emit sourceRequested(fileName(file), line);
}

void CoveragePane::onDetail(QTreeWidgetItem* item) {
    if (!db_ || !item)
        return;
    const int line = item->data(0, kLineRole).toInt();
    const int file = item->data(0, kFileRole).isValid() ? item->data(0, kFileRole).toInt() : -1;
    if (line > 0 && file >= 0)
        emit sourceRequested(fileName(file), line);
}

void CoveragePane::showDetails(int scope) {
    details_->clear();
    if (!db_ || scope < 0)
        return;
    const bool onlyMissed = uncovered_->isChecked();
    auto place = [](QTreeWidgetItem* it, int file, int line) {
        it->setData(0, kFileRole, file);
        it->setData(0, kLineRole, line);
        if (line > 0)
            it->setText(2, QString::number(line));
    };
    auto missed = [](QTreeWidgetItem* it) {
        it->setData(0, kMissRole, true);
        for (int c = 0; c < 3; ++c)
            it->setForeground(c, kMissRed);
    };

    // Lines of the instance, per file, with what is not covered on them.
    {
        auto* group = new QTreeWidgetItem(details_);
        int shown = 0, total = 0;
        std::vector<int> files;
        for (int b : db_->instanceBins(scope)) {
            const int f = db_->bins[static_cast<std::size_t>(b)].file;
            if (f >= 0 && std::find(files.begin(), files.end(), f) == files.end())
                files.push_back(f);
        }
        for (int f : files) {
            for (const auto& [line, l] : cov::lineCoverage(*db_, scope, f)) {
                ++total;
                if (onlyMissed && l.covered())
                    continue;
                ++shown;
                auto* it = new QTreeWidgetItem(group);
                QStringList miss;
                for (cov::Kind k : cov::kAllKinds)
                    if (const int n = l.missed[static_cast<int>(k)]; n > 0)
                        miss << QStringLiteral("%1 %2").arg(n).arg(kindName(k, n));
                const QString where = files.size() > 1 ? QStringLiteral("%1:%2").arg(fileName(f)).arg(line)
                                                       : tr("line %1").arg(line);
                it->setText(0, miss.isEmpty() ? where : tr("%1: not covered %2").arg(where, miss.join(", ")));
                it->setText(1, QString::number(l.hits));
                place(it, f, line);
                if (!l.covered())
                    missed(it);
            }
        }
        group->setText(0, onlyMissed ? tr("Lines not fully covered (%1 of %2)").arg(shown).arg(total)
                                     : tr("Lines (%1)").arg(total));
        group->setExpanded(true);
        if (total == 0)
            delete group;
    }

    // Toggles per signal and bit.
    const std::vector<cov::ToggleSignal> toggles = cov::toggleSignals(*db_, scope);
    if (!toggles.empty()) {
        auto* group = new QTreeWidgetItem(details_);
        int bits = 0, coveredBits = 0;
        for (const cov::ToggleSignal& t : toggles) {
            bits += static_cast<int>(t.bits.size());
            coveredBits += t.coveredBits();
            const bool all = t.coveredBits() == static_cast<int>(t.bits.size());
            if (onlyMissed && all)
                continue;
            auto* sig = new QTreeWidgetItem(group);
            const QString name = QString::fromStdString(cov::displayName(t.name));
            sig->setText(0, name);
            sig->setText(1, tr("%1/%2 bits").arg(t.coveredBits()).arg(t.bits.size()));
            place(sig, t.file, t.line);
            if (!all)
                missed(sig);
            for (const cov::ToggleSignal::Bit& b : t.bits) {
                if (onlyMissed && b.covered())
                    continue;
                if (t.bits.size() == 1 && b.index.empty()) {
                    // a scalar: the counts on the signal row
                    sig->setText(1, b.unreachable ? tr("cannot toggle")
                                                  : QStringLiteral("%1 / %2").arg(b.rise).arg(b.fall));
                    sig->setToolTip(1, tr("0 -> 1 / 1 -> 0"));
                    break;
                }
                auto* bit = new QTreeWidgetItem(sig);
                bit->setText(0, name + QString::fromStdString(cov::displayName(b.index)));
                bit->setText(1, b.unreachable ? tr("cannot toggle")
                                              : QStringLiteral("%1 / %2").arg(b.rise).arg(b.fall));
                bit->setToolTip(1, tr("0 -> 1 / 1 -> 0"));
                place(bit, t.file, t.line);
                if (!b.covered())
                    missed(bit);
            }
        }
        group->setText(0, tr("Toggles (%1 of %2 bits)").arg(coveredBits).arg(bits));
        group->setExpanded(true);
    }

    // FSM states.
    const std::vector<cov::StateVariable> states = cov::stateVariables(*db_, scope);
    if (!states.empty()) {
        auto* group = new QTreeWidgetItem(details_);
        int n = 0, hit = 0;
        for (const cov::StateVariable& v : states) {
            const int vHit = static_cast<int>(
                std::count_if(v.states.begin(), v.states.end(), [](const auto& s) { return s.second > 0; }));
            n += static_cast<int>(v.states.size());
            hit += vHit;
            if (onlyMissed && vHit == static_cast<int>(v.states.size()))
                continue;
            auto* var = new QTreeWidgetItem(group);
            var->setText(0, QString::fromStdString(cov::displayName(v.name)));
            var->setText(1, tr("%1/%2 states").arg(vHit).arg(v.states.size()));
            place(var, v.file, v.line);
            if (vHit < static_cast<int>(v.states.size()))
                missed(var);
            for (const auto& [state, count] : v.states) {
                if (onlyMissed && count > 0)
                    continue;
                auto* st = new QTreeWidgetItem(var);
                st->setText(0, QString::fromStdString(cov::displayName(state)));
                st->setText(1, QString::number(count));
                place(st, v.file, v.line);
                if (count == 0)
                    missed(st);
            }
            var->setExpanded(true);
        }
        group->setText(0, tr("FSM states (%1 of %2)").arg(hit).arg(n));
        group->setExpanded(true);
    }
}

QHash<int, CoverageMark> CoveragePane::marksFor(const cov::Database& db, int instance, int file) {
    QHash<int, CoverageMark> out;
    for (const auto& [line, l] : cov::lineCoverage(db, instance, file)) {
        CoverageMark m;
        m.hits = l.hits;
        m.items = l.bins;
        m.hitItems = l.hitBins;
        QStringList miss;
        for (cov::Kind k : cov::kAllKinds)
            if (const int n = l.missed[static_cast<int>(k)]; n > 0)
                miss << QStringLiteral("%1 %2").arg(n).arg(kindName(k, n));
        if (!miss.isEmpty())
            m.tip = tr("Not covered: %1").arg(miss.join(QStringLiteral(", ")));
        out.insert(line, m);
    }
    return out;
}

QHash<QString, QHash<int, CoverageMark>> CoveragePane::lineMarks() const {
    QHash<QString, QHash<int, CoverageMark>> out;
    if (!db_ || selected_ < 0)
        return out;
    std::vector<int> files;
    for (int b : db_->instanceBins(selected_)) {
        const int f = db_->bins[static_cast<std::size_t>(b)].file;
        if (f >= 0 && std::find(files.begin(), files.end(), f) == files.end())
            files.push_back(f);
    }
    for (int f : files)
        out.insert(fileName(f), marksFor(*db_, selected_, f));
    return out;
}

} // namespace lvd::gui
