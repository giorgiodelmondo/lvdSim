// SPDX-License-Identifier: GPL-3.0-or-later
// Arranging the rows of a WaveWidget: move, group, ungroup, rename, drag and drop, undo and redo.
// The tree operations themselves live in libwave (lvd::wave, display_edit.hpp); this file connects them to
// the selection, the scroll position, the menus and the mouse.
#include "wave/wave_widget.hpp"

#include <QApplication>
#include <QEvent>
#include <QFocusEvent>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>
#include <QToolTip>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace lvd::gui {

using namespace lvd::wave;

namespace {

constexpr std::size_t kMaxUndo = 100;

struct ViewFlags {
    bool expanded;
    bool showBits;
};

void collectViewFlags(const DisplayItem& g, std::unordered_map<ItemId, ViewFlags>& out) {
    for (const DisplayItem& c : g.children) {
        out[c.id] = {c.expanded, c.showBits};
        if (c.kind == ItemKind::Group)
            collectViewFlags(c, out);
    }
}

void applyViewFlags(DisplayItem& g, const std::unordered_map<ItemId, ViewFlags>& flags) {
    for (DisplayItem& c : g.children) {
        const auto it = flags.find(c.id);
        if (it != flags.end()) {
            c.expanded = it->second.expanded;
            c.showBits = it->second.showBits;
        }
        if (c.kind == ItemKind::Group)
            applyViewFlags(c, flags);
    }
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Lookup helpers

std::vector<WaveWidget::ItemId> WaveWidget::selectedItemIds() const {
    std::vector<ItemId> ids;
    for (const RowKey& k : selected_)
        if (k.bit < 0 && std::find(ids.begin(), ids.end(), k.id) == ids.end())
            ids.push_back(k.id);
    return ids;
}

WaveWidget::ItemId WaveWidget::itemIdAtRow(int row) const {
    if (row < 0 || row >= rowCount())
        return 0;
    return rows_[static_cast<std::size_t>(row)].item->id;
}

int WaveWidget::rowOfItem(ItemId id) const {
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].item->id == id && rows_[i].bit < 0)
            return static_cast<int>(i);
    return -1;
}

bool WaveWidget::isRowSelected(int row) const {
    return row >= 0 && row < rowCount() && isSelected(rows_[static_cast<std::size_t>(row)]);
}

void WaveWidget::ensureRowVisible(int row) {
    if (row < 0 || row >= rowCount())
        return;
    const Row& r = rows_[static_cast<std::size_t>(row)];
    const int page = std::max(1, rowsBottom() - kRowsTop);
    if (r.y < scrollY_)
        vbar_->setValue(r.y);
    else if (r.y + r.h > scrollY_ + page)
        vbar_->setValue(r.y + r.h - page);
}

// ---------------------------------------------------------------------------------------------
// Undo history

void WaveWidget::pushUndo(Snapshot before) {
    undo_.push_back(std::move(before));
    while (undo_.size() > kMaxUndo)
        undo_.pop_front();
    redo_.clear();
    emit undoStateChanged();
}

QString WaveWidget::undoText() const {
    return undo_.empty() ? QString() : undo_.back().text;
}
QString WaveWidget::redoText() const {
    return redo_.empty() ? QString() : redo_.back().text;
}

void WaveWidget::clearUndoHistory() {
    if (undo_.empty() && redo_.empty())
        return;
    undo_.clear();
    redo_.clear();
    emit undoStateChanged();
}

void WaveWidget::restore(Snapshot snap) {
    cancelRowDrag();
    cancelRename();
    // Expanded or collapsed groups and shown bits are view state: they stay as the user has them now.
    std::unordered_map<ItemId, ViewFlags> flags;
    collectViewFlags(model_.root(), flags);
    applyViewFlags(snap.model.root(), flags);
    model_ = std::move(snap.model);
    selected_ = std::move(snap.selection);
    afterEdit();
}

bool WaveWidget::undo() {
    if (undo_.empty())
        return false;
    Snapshot target = std::move(undo_.back());
    undo_.pop_back();
    redo_.push_back(Snapshot{model_, selected_, target.text});
    restore(std::move(target));
    emit undoStateChanged();
    return true;
}

bool WaveWidget::redo() {
    if (redo_.empty())
        return false;
    Snapshot target = std::move(redo_.back());
    redo_.pop_back();
    undo_.push_back(Snapshot{model_, selected_, target.text});
    restore(std::move(target));
    emit undoStateChanged();
    return true;
}

void WaveWidget::afterEdit() {
    std::unordered_set<ItemId> alive;
    for (ItemId id : allItemIds(model_))
        alive.insert(id);
    selected_.erase(std::remove_if(selected_.begin(), selected_.end(),
                                   [&](const RowKey& k) { return alive.count(k.id) == 0; }),
                    selected_.end());
    rebuildRows();
    anchorRow_ = -1;
    for (int i = rowCount(); i-- > 0;)
        if (isSelected(rows_[static_cast<std::size_t>(i)])) {
            anchorRow_ = i;
            break;
        }
    emit selectionChanged();
    emit structureChanged();
}

// ---------------------------------------------------------------------------------------------
// Edits

bool WaveWidget::runEdit(const QString& text, const EditFn& fn,
                         const std::function<void(const EditOutcome&)>& select, ItemId reveal) {
    lastEditError_.clear();
    cancelRowDrag();
    Snapshot before{model_, selected_, text};
    lvd::Result<EditOutcome> r = fn(model_);
    if (!r.ok()) {
        lastEditError_ = QString::fromStdString(r.error());
        emit editRejected(lastEditError_);
        return false;
    }
    const EditOutcome& o = r.value();
    if (!o.changed)
        return false;
    pushUndo(std::move(before));
    if (select)
        select(o);
    afterEdit();
    if (reveal != 0)
        ensureRowVisible(rowOfItem(reveal));
    return true;
}

bool WaveWidget::moveSelectedRows(int delta) {
    const std::vector<ItemId> ids = selectedItemIds();
    if (ids.empty() || delta == 0)
        return false;
    const std::vector<ItemId> norm = normalizeSelection(model_, ids);
    const ItemId reveal = norm.empty() ? 0 : (delta < 0 ? norm.front() : norm.back());
    return runEdit(
        delta < 0 ? tr("Move up") : tr("Move down"),
        [&](DisplayModel& m) { return moveItems(m, ids, delta); }, {}, reveal);
}

bool WaveWidget::moveSelectedRowsToEdge(bool toTop) {
    const std::vector<ItemId> ids = selectedItemIds();
    if (ids.empty())
        return false;
    const std::vector<ItemId> norm = normalizeSelection(model_, ids);
    const ItemId reveal = norm.empty() ? 0 : (toTop ? norm.front() : norm.back());
    return runEdit(
        toTop ? tr("Move to top") : tr("Move to bottom"),
        [&](DisplayModel& m) { return moveItemsToEdge(m, ids, toTop); }, {}, reveal);
}

bool WaveWidget::moveRowsToGroup(const std::vector<ItemId>& ids, ItemId parent, std::size_t index) {
    const std::vector<ItemId> norm = normalizeSelection(model_, ids);
    return runEdit(
        tr("Move to group"),
        [&](DisplayModel& m) {
            auto r = moveItemsToGroup(m, ids, parent, index);
            if (r.ok() && r.value().changed && parent != kTopLevel) {
                if (DisplayItem* g = findItem(m, parent))
                    g->expanded = true; // keep the moved rows in view
            }
            return r;
        },
        {}, norm.empty() ? 0 : norm.front());
}

bool WaveWidget::moveSelectedToGroup(ItemId parent) {
    return moveRowsToGroup(selectedItemIds(), parent, kAppend);
}

bool WaveWidget::groupSelected(const std::string& label) {
    const std::vector<ItemId> ids = selectedItemIds();
    if (ids.empty())
        return false;
    ItemId created = 0;
    const bool ok = runEdit(
        tr("Group"), [&](DisplayModel& m) { return wrapInGroup(m, ids, label); },
        [&](const EditOutcome& o) {
            created = o.created.empty() ? 0 : o.created.front();
            selected_.assign(1, RowKey{created, -1});
        });
    if (ok)
        ensureRowVisible(rowOfItem(created));
    return ok;
}

bool WaveWidget::ungroupSelected() {
    const std::vector<ItemId> ids = selectedItemIds();
    if (ids.empty())
        return false;
    ItemId first = 0;
    const bool ok = runEdit(
        tr("Ungroup"), [&](DisplayModel& m) { return ungroup(m, ids); },
        [&](const EditOutcome& o) {
            selected_.clear();
            for (ItemId id : o.moved)
                selected_.push_back({id, -1});
            first = o.moved.empty() ? 0 : o.moved.front();
        });
    if (ok)
        ensureRowVisible(rowOfItem(first));
    return ok;
}

bool WaveWidget::renameItem(ItemId id, const std::string& label) {
    return runEdit(
        tr("Rename"), [&](DisplayModel& m) { return lvd::wave::renameItem(m, id, label); }, {}, id);
}

static bool insertDividerRelative(WaveWidget& w, const DisplayModel& model,
                                  std::vector<WaveWidget::ItemId> ids, bool below, const std::string& label,
                                  std::function<bool(DisplayItem, ItemId, std::size_t)> insert) {
    (void)w;
    ItemId parent = kTopLevel;
    std::size_t index = below ? WaveWidget::kAppend : 0;
    const std::vector<ItemId> norm = normalizeSelection(model, ids);
    if (!norm.empty()) {
        const auto loc = locate(model, below ? norm.back() : norm.front());
        if (loc) {
            parent = loc->parent;
            index = loc->index + (below ? 1 : 0);
        }
    }
    return insert(DisplayModel::makeDivider(label), parent, index);
}

bool WaveWidget::insertDividerAbove(const std::string& label) {
    return insertDividerRelative(
        *this, model_, selectedItemIds(), false, label, [&](DisplayItem d, ItemId parent, std::size_t index) {
            const ItemId id = d.id;
            return runEdit(
                tr("Insert divider"), [&](DisplayModel& m) { return insertItem(m, parent, index, d); },
                [&](const EditOutcome&) {
                    selected_.assign(1, RowKey{id, -1});
                },
                id);
        });
}

bool WaveWidget::insertDividerBelow(const std::string& label) {
    return insertDividerRelative(
        *this, model_, selectedItemIds(), true, label, [&](DisplayItem d, ItemId parent, std::size_t index) {
            const ItemId id = d.id;
            return runEdit(
                tr("Insert divider"), [&](DisplayModel& m) { return insertItem(m, parent, index, d); },
                [&](const EditOutcome&) {
                    selected_.assign(1, RowKey{id, -1});
                },
                id);
        });
}

// ---------------------------------------------------------------------------------------------
// Inline rename editor

bool WaveWidget::beginRename(int row) {
    if (row < 0) {
        const std::vector<ItemId> ids = selectedItemIds();
        if (ids.size() == 1) {
            row = rowOfItem(ids.front());
        } else if (anchorRow_ >= 0 && isRowSelected(anchorRow_)) {
            row = anchorRow_;
        }
    }
    if (row < 0 || row >= rowCount())
        return false;
    const Row& r = rows_[static_cast<std::size_t>(row)];
    if (r.bit >= 0)
        return false;
    closeEditor();

    ensureRowVisible(row);
    renameId_ = r.item->id;
    editor_ = new QLineEdit(this);
    editor_->setObjectName("renameEditor");
    editor_->setFrame(true);
    editor_->setText(
        QString::fromStdString(r.item->kind == ItemKind::Divider ? r.item->label : r.item->displayName()));
    editor_->setToolTip(r.item->kind == ItemKind::Group ? tr("Group name: Enter to apply, Escape to cancel")
                                                        : tr("Enter to apply, Escape to cancel"));
    editor_->installEventFilter(this);
    connect(editor_, &QLineEdit::returnPressed, this, [this] { commitRename(); });
    connect(editor_, &QLineEdit::textEdited, this, [this] {
        if (editor_ != nullptr)
            editor_->setStyleSheet({});
    });
    positionEditor();
    editor_->show();
    editor_->selectAll();
    editor_->setFocus();
    return true;
}

void WaveWidget::positionEditor() {
    if (editor_ == nullptr)
        return;
    const int row = rowOfItem(renameId_);
    if (row < 0) { // the item is gone, or its group is collapsed
        cancelRename();
        return;
    }
    const Row& r = rows_[static_cast<std::size_t>(row)];
    const int top = rowTop(row);
    const int x = 6 + r.depth * 14 + (r.item->kind == ItemKind::Divider ? 0 : 14) - 2;
    editorBusy_ = true;
    editor_->setGeometry(x, top, std::max(60, nameWidth_ - x - 3), kRowHeight - 1);
    const bool visible = top >= kRowsTop && top + kRowHeight <= rowsBottom();
    if (editor_->isHidden() == visible)
        editor_->setVisible(visible);
    editorBusy_ = false;
}

void WaveWidget::closeEditor() {
    if (editor_ == nullptr)
        return;
    QLineEdit* e = editor_;
    editor_ = nullptr;
    renameId_ = 0;
    e->removeEventFilter(this);
    e->hide();
    e->deleteLater();
}

void WaveWidget::cancelRename() {
    if (editor_ == nullptr)
        return;
    const bool hadFocus = editor_->hasFocus();
    closeEditor();
    if (hadFocus)
        setFocus();
    update();
}

bool WaveWidget::commitRename() {
    if (editor_ == nullptr)
        return false;
    const ItemId id = renameId_;
    const std::string text = editor_->text().toStdString();
    const bool ok = renameItem(id, text);
    if (!ok && !lastEditError_.isEmpty()) { // refused: stay open and say why
        if (editor_ != nullptr) {
            editor_->setStyleSheet("QLineEdit { background: #5a2424; color: white; }");
            editor_->setToolTip(lastEditError_);
            QToolTip::showText(editor_->mapToGlobal(QPoint(0, editor_->height())), lastEditError_, editor_);
        }
        return false;
    }
    const bool hadFocus = editor_ != nullptr && editor_->hasFocus();
    closeEditor();
    if (hadFocus)
        setFocus();
    update();
    return ok;
}

bool WaveWidget::eventFilter(QObject* watched, QEvent* event) {
    if (watched == editor_ && editor_ != nullptr) {
        if (event->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(event);
            if (ke->key() == Qt::Key_Escape) {
                cancelRename();
                return true;
            }
        } else if (event->type() == QEvent::FocusOut && !editorBusy_) {
            const Qt::FocusReason reason = static_cast<QFocusEvent*>(event)->reason();
            if (reason != Qt::ActiveWindowFocusReason && reason != Qt::PopupFocusReason) {
                if (!commitRename() && editor_ != nullptr)
                    cancelRename(); // a refused name is dropped on focus loss
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

// ---------------------------------------------------------------------------------------------
// Menus

std::optional<QString> WaveWidget::promptText(const QString& title, const QString& label,
                                              const QString& initial) {
    if (prompt_)
        return prompt_(title, label, initial);
    bool ok = false;
    const QString text = QInputDialog::getText(this, title, label, QLineEdit::Normal, initial, &ok);
    if (!ok)
        return std::nullopt;
    return text;
}

void WaveWidget::addGroupTargets(QMenu* sub, const std::vector<ItemId>& ids) {
    QAction* top = sub->addAction(tr("Top level"));
    top->setObjectName("moveToTopLevel");
    top->setToolTip(tr("Take the selected rows out of their groups"));
    const QString whyTop = QString::fromStdString(checkMoveToGroup(model_, ids, kTopLevel));
    top->setEnabled(whyTop.isEmpty());
    connect(top, &QAction::triggered, this, [this] { moveSelectedToGroup(kTopLevel); });

    QAction* fresh = sub->addAction(tr("New group..."));
    fresh->setObjectName("moveToNewGroup");
    fresh->setToolTip(tr("Wrap the selected rows in a new group"));
    connect(fresh, &QAction::triggered, this, [this] {
        const auto name =
            promptText(tr("New group"), tr("Group name:"), QString::fromStdString(uniqueGroupName(model_)));
        if (name)
            groupSelected(name->toStdString());
    });
    sub->addSeparator();

    int count = 0;
    std::function<void(const DisplayItem&, const QString&)> walk = [&](const DisplayItem& g,
                                                                       const QString& path) {
        for (const DisplayItem& c : g.children) {
            if (c.kind != ItemKind::Group || count >= 200)
                continue;
            ++count;
            const QString name = path.isEmpty() ? QString::fromStdString(c.label)
                                                : path + "/" + QString::fromStdString(c.label);
            QAction* a = sub->addAction(name);
            a->setObjectName(QString("moveToGroup_%1").arg(c.id));
            const QString why = QString::fromStdString(checkMoveToGroup(model_, ids, c.id));
            a->setEnabled(why.isEmpty());
            if (!why.isEmpty())
                a->setToolTip(why);
            const ItemId gid = c.id;
            connect(a, &QAction::triggered, this, [this, gid] { moveSelectedToGroup(gid); });
            walk(c, name);
        }
    };
    walk(model_.root(), {});
}

void WaveWidget::populateArrangeMenu(QMenu* menu, bool withAnalogSettings) {
    const std::vector<ItemId> ids = selectedItemIds();
    const bool any = !ids.empty();
    bool anyGroup = false;
    for (ItemId id : ids)
        if (const DisplayItem* it = findItem(model_, id); it != nullptr && it->kind == ItemKind::Group)
            anyGroup = true;
    bool canRename = ids.size() == 1;
    if (!canRename && anchorRow_ >= 0 && isRowSelected(anchorRow_))
        canRename = rows_[static_cast<std::size_t>(anchorRow_)].bit < 0;

    auto add = [&](const char* name, const QString& text, const QString& tip, bool enabled,
                   std::function<void()> fn) {
        QAction* a = menu->addAction(text);
        a->setObjectName(name);
        a->setToolTip(tip);
        a->setStatusTip(tip);
        a->setEnabled(enabled);
        connect(a, &QAction::triggered, this, std::move(fn));
        return a;
    };

    add("moveUp", tr("Move up\tCtrl+Up"), tr("Move the selected rows up among their siblings"), any,
        [this] { moveSelectedRows(-1); });
    add("moveDown", tr("Move down\tCtrl+Down"), tr("Move the selected rows down among their siblings"), any,
        [this] { moveSelectedRows(1); });
    add("moveTop", tr("Move to top\tCtrl+Shift+Up"), tr("Move the selected rows to the top of their group"),
        any, [this] { moveSelectedRowsToEdge(true); });
    add("moveBottom", tr("Move to bottom\tCtrl+Shift+Down"),
        tr("Move the selected rows to the bottom of their group"), any,
        [this] { moveSelectedRowsToEdge(false); });
    QMenu* sub = menu->addMenu(tr("Move to group"));
    sub->menuAction()->setObjectName("moveToGroupMenu");
    sub->setObjectName("moveToGroupSubmenu");
    sub->setEnabled(any);
    if (any)
        addGroupTargets(sub, ids);

    menu->addSeparator();
    add("groupSelected", tr("Group selected\tCtrl+G"), tr("Wrap the selected rows in a new group"), any,
        [this] { groupSelected(); });
    add("ungroup", tr("Ungroup\tCtrl+Shift+G"), tr("Replace the selected groups by their rows"), anyGroup,
        [this] { ungroupSelected(); });
    add("rename", tr("Rename\tF2"), tr("Rename the group, divider, bus or signal label"), canRename,
        [this] { beginRename(); });
    add("insertDividerAbove", tr("Insert divider above"), tr("Add a divider above the selection"), true,
        [this] {
            if (insertDividerAbove())
                beginRename();
        });
    add("insertDividerBelow", tr("Insert divider below"), tr("Add a divider below the selection"), true,
        [this] {
            if (insertDividerBelow())
                beginRename();
        });
    add("delete", tr("Delete\tDel"), tr("Remove the selected rows (a group takes its rows with it)"), any,
        [this] { removeSelection(); });

    menu->addSeparator();
    const QString ut = undoText(), rt = redoText();
    add("undo", ut.isEmpty() ? tr("Undo\tCtrl+Z") : tr("Undo %1\tCtrl+Z").arg(ut),
        tr("Undo the last change to the rows"), canUndo(), [this] { undo(); });
    add("redo", rt.isEmpty() ? tr("Redo\tCtrl+Y") : tr("Redo %1\tCtrl+Y").arg(rt),
        tr("Redo the change that was undone"), canRedo(), [this] { redo(); });

    if (withAnalogSettings) {
        menu->addSeparator();
        add("analogSettings", tr("Analog Settings..."), tr("Auto or manual (Min, Max) range of the selected analog traces"),
            analogSelectionAvailable(), [this] { showAnalogSettings(); });
    }
}

// ---------------------------------------------------------------------------------------------
// Drag and drop of rows

WaveWidget::RowDropTarget WaveWidget::dropTargetAt(const QPoint& pos) const {
    RowDropTarget t;
    const std::vector<ItemId> dragged = drag_ == Drag::Rows ? dragIds_ : selectedItemIds();
    if (pos.x() < 0 || pos.x() >= nameWidth_ || pos.y() < kRowsTop || pos.y() >= rowsBottom()) {
        t.outside = true;
        t.reason = tr("Drop the rows on the name column");
        return t;
    }

    const DisplayItem& root = model_.root();
    const int idx = rowAtY(pos.y());
    int depth = 0;
    if (idx < 0) { // below the last row: the end of the top level, which is the way out of a group
        t.parent = kTopLevel;
        t.index = root.children.size();
        t.lineY = rows_.empty() ? kRowsTop : rowTop(rowCount() - 1) + rows_.back().h - 1;
    } else {
        const Row& r = rows_[static_cast<std::size_t>(idx)];
        t.row = idx;
        // Last row of this item (its bit rows follow it) gives the y of "below".
        int last = idx;
        while (last + 1 < rowCount() && rows_[static_cast<std::size_t>(last + 1)].item == r.item &&
               rows_[static_cast<std::size_t>(last + 1)].bit >= 0)
            ++last;
        const int belowY = rowTop(last) + rows_[static_cast<std::size_t>(last)].h - 1;
        const auto loc = locate(model_, r.item->id);
        const double f = static_cast<double>(pos.y() - rowTop(idx)) / std::max(1, r.h);
        if (!loc) {
            t.outside = true;
            return t;
        }
        if (r.bit >= 0) { // a bit row is part of its signal: drop after the signal
            t.parent = loc->parent;
            t.index = loc->index + 1;
            t.lineY = belowY;
            depth = r.depth - 1;
        } else if (r.item->kind == ItemKind::Group) {
            if (f < 0.25) {
                t.parent = loc->parent;
                t.index = loc->index;
                t.lineY = rowTop(idx);
                depth = r.depth;
            } else if (f > 0.75) {
                if (r.item->expanded) { // just below an open group's row: its first child
                    t.parent = r.item->id;
                    t.index = 0;
                    t.lineY = belowY;
                    depth = r.depth + 1;
                } else {
                    t.parent = loc->parent;
                    t.index = loc->index + 1;
                    t.lineY = belowY;
                    depth = r.depth;
                }
            } else {
                t.parent = r.item->id;
                t.index = r.item->children.size();
                t.intoGroup = true;
                depth = r.depth + 1;
            }
        } else if (f < 0.5) {
            t.parent = loc->parent;
            t.index = loc->index;
            t.lineY = rowTop(idx);
            depth = r.depth;
        } else {
            t.parent = loc->parent;
            t.index = loc->index + 1;
            t.lineY = belowY;
            depth = r.depth;
        }
    }
    t.lineX = 6 + std::max(0, depth) * 14;
    t.reason = QString::fromStdString(checkMoveToGroup(model_, dragged, t.parent));
    t.valid = t.reason.isEmpty();
    return t;
}

bool WaveWidget::startRowDrag() {
    const std::vector<ItemId> ids = selectedItemIds();
    if (ids.empty())
        return false;
    cancelRename();
    dragIds_ = ids;
    drag_ = Drag::Rows;
    rowPressPending_ = false;
    deferSelect_ = false;
    dropTarget_ = RowDropTarget{};
    autoScroll_->start();
    update();
    return true;
}

void WaveWidget::updateRowDrag(const QPoint& pos) {
    if (drag_ != Drag::Rows)
        return;
    lastMouse_ = pos;
    dropTarget_ = dropTargetAt(pos);
    setCursor(dropTarget_.valid ? Qt::DragMoveCursor : Qt::ForbiddenCursor);
    update();
}

void WaveWidget::cancelRowDrag() {
    rowPressPending_ = false;
    deferSelect_ = false;
    if (drag_ != Drag::Rows)
        return;
    drag_ = Drag::None;
    autoScroll_->stop();
    dragIds_.clear();
    dropTarget_ = RowDropTarget{};
    setCursor(Qt::ArrowCursor);
    update();
}

bool WaveWidget::finishRowDrag(const QPoint& pos) {
    if (drag_ != Drag::Rows)
        return false;
    const RowDropTarget target = dropTargetAt(pos);
    const std::vector<ItemId> ids = dragIds_;
    drag_ = Drag::None;
    autoScroll_->stop();
    dragIds_.clear();
    dropTarget_ = RowDropTarget{};
    setCursor(Qt::ArrowCursor);
    update();
    if (target.outside)
        return false; // dropped somewhere else: ignored
    if (!target.valid) {
        lastEditError_ = target.reason;
        emit editRejected(target.reason);
        return false;
    }
    return moveRowsToGroup(ids, target.parent, target.index);
}

int WaveWidget::autoScrollStep() {
    if (drag_ != Drag::Rows)
        return 0;
    const QPoint p = lastMouse_;
    if (p.x() < 0 || p.x() >= waveLeft())
        return 0;
    constexpr int kZone = 28;
    int dy = 0;
    if (p.y() < kRowsTop + kZone)
        dy = -std::clamp((kRowsTop + kZone - p.y()) / 2 + 2, 2, 30);
    else if (p.y() > rowsBottom() - kZone)
        dy = std::clamp((p.y() - (rowsBottom() - kZone)) / 2 + 2, 2, 30);
    if (dy == 0)
        return 0;
    const int before = vbar_->value();
    vbar_->setValue(before + dy);
    return vbar_->value() - before;
}

void WaveWidget::drawDropIndicator(QPainter& p) const {
    const RowDropTarget& t = dropTarget_;
    if (!t.valid)
        return;
    const QColor accent(0x4a, 0xa8, 0xff);
    p.save();
    p.setClipRect(QRect(0, kRowsTop, waveLeft(), std::max(0, rowsBottom() - kRowsTop)));
    if (t.intoGroup && t.row >= 0 && t.row < rowCount()) {
        const int top = rowTop(t.row);
        const int h = rows_[static_cast<std::size_t>(t.row)].h;
        QColor fill = accent;
        fill.setAlpha(70);
        p.fillRect(QRect(0, top, waveLeft(), h - 1), fill);
        p.setPen(QPen(accent, 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRect(1, top + 1, waveLeft() - 3, h - 3));
    } else if (t.lineY >= 0) {
        p.setPen(QPen(accent, 2));
        p.drawLine(t.lineX, t.lineY, waveLeft() - 4, t.lineY);
        p.setBrush(accent);
        p.drawEllipse(QPoint(t.lineX, t.lineY), 3, 3);
    }
    p.restore();
}

} // namespace lvd::gui
