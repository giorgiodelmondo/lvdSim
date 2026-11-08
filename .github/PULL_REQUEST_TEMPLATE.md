## What and why

<!-- A short description of the change and the reason for it. Link the issue if there is one. -->

## Checklist

- [ ] Builds without warnings on the project settings (`-Wall -Wextra -Werror`, MinGW preset or `linux-debug`)
- [ ] Tests added or updated (Catch2 for `libwave` and `tools`, QTest for the GUI); a change without tests is incomplete
- [ ] `ctest` passes (state the preset and the number of tests)
- [ ] Documentation updated (`docs/`, and `docs/command-compat.md` for a new command)
- [ ] Kernel changes: commit on the `lvdsim` branch of the NVC fork, PR folder in `kernel/upstream/` and a row in `kernel/PATCHES.md`
- [ ] No code, documentation text, Tcl scripts or help strings copied from other products, and no vendor product names added
- [ ] New source files carry `SPDX-License-Identifier: GPL-3.0-or-later` (`python3 scripts/check-headers.py`)
- [ ] AI assistance: I used / did not use an AI tool for this change (if used, I describe how below and I have reviewed every line)
- [ ] I have the right to submit this work and agree that it is licensed under GPL-3.0-or-later (see CONTRIBUTING.md)

## How it was tested

<!-- Operating system, preset, commands, number of tests that ran. -->
