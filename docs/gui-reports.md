# HDL reports in the transcript and the wave window

NVC prints every `report` and `assert` as `** <Severity>: <time>+<delta>: <message>` followed by continuation lines.
The time is `<integer><unit>+<delta>`, the unit is the largest of fs, ps, ns, us, ms in which the time is a whole
number (`100ns+0`, `2us+0`, `1000ms+0`); there is no space in what NVC prints, the parser also accepts one (`20 us+1`).
Severities: Note, Warning, Error, Failure (Fatal counts as Failure, Debug as Note). Reports made before elaboration
finished print `(init):` instead of a time and Verilog `$display` prints no prefix at all: both stay plain text.

* `gui/sim/parsers/report_lines.{hpp,cpp}`: `parseReportLine`, a pure function (no Qt), tested in `test_report_lines`.
* Transcript (`TranscriptWidget`): when a line is complete the time stamp becomes an underlined link in the colour of the
  severity. Hover shows a hand cursor and a tooltip with the exact time in femtoseconds; a click emits `timeActivated`,
  which `SimWindow` connects to `WaveWidget::showTimeFs`: cursor 1 moves there (created if missing) and the view scrolls
  only when the time is outside it. Every report also emits `reportLogged`, which becomes an event marker.
* Wave window: between the ruler and the first row there is a constant 18 px band (`WaveWidget::eventBandHeight()`,
  `rowsTop()`). It holds the event arrows and the cursor delta labels, so neither overlaps a wave. Cursor lines still
  cross the band and the rows. Rows, hit tests, the scroll bar, the GPU canvas and `buildGpuFrame` start at `rowsTop()`;
  the band and its arrows are drawn by the QPainter code in both back ends (overlay widget in GPU mode).
* Markers: `setEventMarkers`, `addEventMarker`, `clearEventMarkers` with `EventMarker {timeFs, severity, text}`; stored
  sorted by time, with prefix counts per severity. Drawing finds the visible range with binary search and merges all
  events of one pixel column into one arrow (highest severity, a light outline, count in the tooltip), so the cost is
  bounded by the width of the window, not by the number of events. Clicking an arrow moves cursor 1 to its first event.
  Colours: note teal blue, warning amber, error red, failure magenta.
* `SimWindow` clears the markers when a simulation (re)starts; markers that arrive before the wave data exists are kept.
* Example: `examples/reports/src/tb_reports.vhd` (all severities, ns/us) and `mon.v` (Verilog `$display`, no stamp).
  `examples/reports/run.sh` (or `run.bat`) compiles it and opens the GUI with `sim.do`; `--batch` prints the reports without a window.
  Screenshot: `lvdsim-gui -do "add wave -r /*; run 3 us" work.tb_reports --cursor 100000000 --cursor 500000000 --screenshot out.png`.
