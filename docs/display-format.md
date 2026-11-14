# Wave display layout format (version 1)

Written by `DisplayModel::serialize`, read by `DisplayModel::parse`. Line based, UTF-8. Blank lines and
lines starting with `#` are ignored. Indentation is cosmetic. Signals are referenced by hierarchical
path (`top.dut.clk`), never by id, so a layout can be reloaded against a different run of the design.

```
lvdwave-display 1
group "Clocks" expanded
  signal "top.clk" radix=binary
  group "Inner" collapsed
    signal "top.rst" radix=default
  end
end
divider "data path"
bus "addr" radix=hex
  member "top.a_hi"
  member "top.a_lo"
end
signal "top.adc" radix=unsigned label="ADC value" analog=-1.5:3.25 linear signed height=6
```

- `group "label" expanded|collapsed` ... `end`
- `signal "path" [radix=R] [label="text"] [analog=auto|MIN:MAX] [linear] [signed] [height=N] [heightpx=P]`
- `bus "label" [radix=R] [analog...] [heightpx=P]` followed by `member "path"` lines (most significant first), then `end`
- `divider "text" [heightpx=P]`
- `height=N` is the height of an analog trace in lines of 22 pixels (default 4). `heightpx=P` is the row height in pixels of any
  row (signal, bus, expression, compare, divider; 12 to 800, other values are brought into range); it wins over `height=N` and is
  only written when the row has its own height. In the `.do` export it is `add wave -height P` (pixels, on every row kind).
- Radix names: binary, octal, hex, unsigned, signed, ascii, default (aliases: bin, oct, decimal).
- Strings use `\"` and `\` escapes. Errors report the line number.

## Expression, compare and dataset rows
- `expr "name" "expression" [radix=R] [color=#rrggbb] [analog...]`: an expression signal (syntax in expressions.md). The
  expression is kept as text and evaluated against the waveforms when the layout is shown; one that does not compile is
  shown as a row with its error.
- `compare "label" "reference path" "test path" [tolerance=N] [color=#rrggbb]`: a compare row. `tolerance` is in units of the
  waveform's time scale (femtoseconds for NVC), 0 by default.
- A path may carry a dataset prefix, `ref:top.dut.q`, in `signal`, `member` and `compare` statements.
- In the `.do` export: `add wave -label NAME -expr {EXPRESSION}`, `add wave -compare -label L [-tolerance N] REF TEST`, and
  dataset paths as `ref:/top/dut/q`.

## Additions (bits, colours, sparse buses)
- `signal "path" bits` shows one row per bit under a vector signal.
- `color=#rrggbb` on `signal` and `bus` overrides the trace colour (outlines, levels, analog line, name text).
- A bus member can be a single bit: `member "top.cnt" bit=7`. Bit 0 is the least significant bit. Members
  may come from different signals in any order, so a sparse selection of bits forms one virtual signal.

## Arranging rows (items, ids, operations)

The wave window keeps the layout as a tree: groups hold signals, virtual buses, dividers and other groups
(groups nest to any depth). The per-bit rows of an expanded vector are not tree items: they follow their
signal and cannot be moved, grouped or renamed on their own.

Every item has a process-wide unique **id** (`lvd::wave::ItemId`, assigned when the item is created). Copies keep
the id, so moving, grouping, ungrouping and undo (which restores a copy of the tree) never change the id of a row.
Ids are not saved: loading a layout creates new items. The wave window uses the id as the identity of a row in its
tile cache, so a moved row keeps its cached tiles.

The operations are in `libwave/include/lvdwave/display_edit.hpp` (no Qt). Each returns `lvd::Result<EditOutcome>`: an
error message when the edit is refused (the tree is untouched), otherwise `changed` and the ids of the items that were
`moved`, `created`, `removed`, `renamed` and of the `parents` whose child list changed (`kTopLevel` = 0 for the root).
An edit with nothing to do (the first row moved up) succeeds with `changed == false`.

| Function | Meaning |
|---|---|
| `moveItems(model, ids, delta)` | up (negative) or down by \|delta\| places among the siblings; each item stays in its group; a selected block that reaches the edge stays there |
| `moveItemsToEdge(model, ids, toTop)` | top or bottom of the own group, relative order kept |
| `moveItemsToGroup(model, ids, parent, index)` | into group `parent` (`kTopLevel` or the root id: top level) with the first item at `index` of the child list (counted before the move, clamped); items may come from different groups, their display order is kept |
| `checkMoveToGroup(model, ids, parent)` | the reason `moveItemsToGroup` would refuse, or an empty string (used while dragging) |
| `wrapInGroup(model, ids, label)` | new expanded group where the first item was; an empty label becomes `Group`, `Group 2`, ... (unique in the whole tree) |
| `ungroup(model, ids)` | replaces each selected group by its children, in place |
| `renameItem(model, id, label)` | group, divider, bus or signal label |
| `insertItem`, `removeItemsById` | add an item at a position / remove items with everything below them |
| `normalizeSelection`, `locate`, `findItem`, `allItemIds`, `isSameOrDescendant`, `uniqueGroupName` | helpers |

Rules:

- A selection is a list of ids in any order; unknown ids make the edit fail, duplicates are dropped, and an item whose
  ancestor is also selected moves with the ancestor. Items are handled in display (depth first) order.
- A group cannot be moved into itself or into one of its own groups.
- Group names are unique among the groups of one parent (the `.do` export addresses a group by its path). An edit that
  would create a clash (rename, wrap with a given name, move into a group that has a group of that name, ungroup into
  a parent that has one) fails. A group name cannot be empty, contain `/` or a line break. A bus needs a name. A
  divider may be empty (`divider ""`). An empty signal label shows the path again. Names are trimmed.

### In the `.do` export

`File > Save wave format` writes one `add wave` command per row, in display order, so the order, group membership,
labels and collapsed state survive a round trip (tested in `libwave/tests/test_wave_commands.cpp`). This is what the
window of the two screenshots `docs/images/wave_reorder_before.png` and `wave_reorder_after.png` exports after
moving `Datapath` up, moving `valid` and `ready` into `Control`, renaming it, collapsing `Datapath` and wrapping `Alarms` in `Outer`:

```
# lvdsim wave layout: familiar add wave commands (virtual bus, group collapse, show-bits, nested group paths and empty groups are lvdsim extensions)
add wave -divider Inputs
add wave -group Datapath -collapse -radix unsigned -label cnt /top/cnt
add wave -group Datapath -radix hex -label data {/top/data[7:0]}
add wave -group Datapath -radix unsigned -label sum /top/sum
add wave -group {Control and flags} -label clk /top/clk
add wave -group {Control and flags} -label rst /top/rst
add wave -group {Control and flags} -label enable /top/enable
add wave -group {Control and flags} -label valid /top/valid
add wave -group {Control and flags} -label ready /top/ready
add wave -divider Status
add wave -group Outer
add wave -group Outer/Alarms -label err /top/err
```

- `-group A/B` names the group `B` inside group `A` (levels are separated by `/`; missing levels are created). This
  replaces the earlier flattening of nested groups into a single group literally named `A/B`.
- `-collapse` on the line that first mentions a group makes that group start collapsed.
- `add wave -group NAME` without signals declares an empty group. The exporter writes this line for groups that are
  empty and for a group whose first child is another group (so that the outer group, and its collapsed state, exist
  before the inner one).
- A virtual bus always writes its radix unless it is hex (the default of `-vbus`), so `default` survives.

The lvdsim layout files (`File > Save wave layout`, `.lvdlayout`) are the format at the top of this page and keep
everything, nesting included.

### Scripted edits for headless runs

`lvdsim-gui ... --wave-op "OP ARGS"` (repeatable, applied in order after `--send`) edits the rows by name before
`--screenshot`; see `SimWindow::applyWaveOp`: `move-up N [steps]`, `move-down`, `move-top`, `move-bottom`,
`group N,N [label]`, `ungroup N`, `rename N new`, `move-to-group N,N GROUP|-`, `divider-above N [label]`,
`divider-below N [label]`, `delete N`, `toggle N`, `undo`, `redo`, `select N,N`, `edit N` (opens the inline rename
editor) and `drag N,N TARGET FRACTION` (holds a drag over a row so the drop indicator is visible).
`--wave-context-menu NAME` also pops up the context menu on a row. Row names are the names the wave window shows.

### Analog range and virtual buses

A virtual bus (`bus` line) can be drawn as an analog trace like a vector: its composed value (members most significant first) is read
as an unsigned number, or two's complement with the `signed` radix or the `signed` option; a value with an X/U/Z bit has no value
(nothing is drawn while it lasts), buses wider than 64 bits stay bus rows. `add wave -vbus NAME -format analog-step|analog-interpolated
[-min A -max B] [-height P] members...` creates one, and the `.do` export writes these options for analog buses.


`analog=auto` (default) or `analog=MIN:MAX` on `signal` and `bus` lines; in the `.do` export `-min V -max V` of `add wave`. Values are
written with 17 significant digits and read back exactly; scientific notation (`2e-3`) is accepted everywhere. A manual range needs
`MIN < MAX` and both finite: the dialog refuses anything else, `add wave -min/-max` keeps the automatic range when only one limit is
given or the pair is not valid, and the drawing widens an invalid stored range to `[v - 0.5, v + 0.5]` instead of dividing by zero.
`lvd::wave::resolveAnalogRange` is the one place that decides the range, `analogFraction` clamps a value onto the row (values outside
are drawn on the top or bottom edge). Auto range is the minimum and maximum of every value of the signal (`NumericIndex::range`), so
it does not depend on the tile or the view; the row revision (part of the tile cache key) contains the mode and both limits, so a
change redraws the QPainter tiles and the GPU atlas entries.

### Scroll bars and Follow (GUI)

The horizontal bar maps `[store start, store end]` onto 2^30 steps (thumb = the visible window); `WaveWidget::setFollow(true)` keeps the
right edge at the end of the data with the current width (see the class comment of `WaveWidget` for the exact rules). `--wave-op`
adds `follow on|off`, `zoom FACTOR`, `scroll-time FRACTION` and `analog-range NAME[,NAME] auto|MIN MAX`.

## Drawing rules of the wave area

- **End of data.** Rows draw nothing for times after the end of the recorded data: the simulation time the store has reached
  (`WaveStore::endTime()`; with a live run the time of the latest `#t` record of the stream, with an FST file its end time). It is
  the same for every signal: a signal whose last change was early keeps its value until that end, and not beyond. The area after it
  has a faint tint. A cursor after the end shows "-" for every row. The cut is in the tile geometry (`TileRequest::validEnd`), so the
  QPainter and the GPU renderer agree; a `TileRequest` without it (tests, tools) holds the last value for ever.
- **Analog rows.** Each pixel column shows the vertical extent of the trace over its time span: the value held into the column, the
  values of the changes inside it and, for `linear` traces, the interpolated values at its left and right edge. There is one rule for
  every zoom level, so zooming out draws the envelope of what zooming in draws. Without `linear` the value is held between changes
  (step); there is no other interpolation.
