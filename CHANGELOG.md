# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/) (while the major version is 0, minor versions may change behaviour).

## [Unreleased]

### Added
- Wave window: a horizontal scroll bar for time (range = the run known to the store, grows while live; thumb = visible window) and a
  dark-themed vertical bar for rows (name, value and wave columns scroll together; 10 000 rows tested). Hit testing of rows is now a
  binary search.
- Wave toolbar: a checkable **Follow simulation (stay at end)** button (also View > Follow simulation, Ctrl+Shift+F, saved as `wave/follow`).
  It keeps the right edge of the view at the end of the live data with the current zoom width. Manual panning, Home/End, Go to and
  Zoom full turn it off; zooming keeps it on with the end pinned.
- **Analog Settings...** dialog (context menu of analog rows, Wave menu): auto range or manual Min/Max with validation, one undo step,
  saved in layouts and `.do` files. Values outside a manual range are clipped to the row edges (QPainter and GPU).
- `lvd::wave::resolveAnalogRange`, `analogFraction`, `analogRangeError`; `add wave -min/-max` now ignores invalid pairs.
- `--wave-op` commands `follow`, `zoom`, `scroll-time` and `analog-range` for headless screenshots.

### Changed
- The wave area is shorter by the height of the horizontal bar (`WaveWidget::rowsBottom()`); the first live `dataChanged` no longer
  treats a freshly attached store as "showing the end".

