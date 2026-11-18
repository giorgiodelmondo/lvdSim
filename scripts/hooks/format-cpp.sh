#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# PostToolUse hook: run clang-format on the C/C++ file that was just edited or written.
# Reads the hook JSON from stdin, extracts tool_input.file_path, formats in place.
# Always exits 0 so a formatting problem never blocks Claude Code.

input=$(cat)
file=$(printf '%s' "$input" | sed -n 's/.*"file_path"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -n 1)
[ -z "$file" ] && exit 0

case "$file" in
  *.c|*.cc|*.cpp|*.cxx|*.h|*.hpp|*.hh) ;;
  *) exit 0 ;;
esac

# Never reformat vendored or kernel code.
case "$file" in
  *third_party/*|*kernel/nvc/*) exit 0 ;;
esac

# Normalise Windows backslashes for sh.
file=$(printf '%s' "$file" | sed 's#\\\\#/#g; s#\\#/#g')
[ -f "$file" ] || exit 0

cf="${LVDSIM_CLANG_FORMAT:-clang-format}"
if ! command -v "$cf" >/dev/null 2>&1 && [ ! -x "$cf" ]; then
  exit 0
fi

"$cf" -i --style=file "$file" >/dev/null 2>&1
exit 0
