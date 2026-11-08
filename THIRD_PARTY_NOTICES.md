# Third-party notices

lvdsim itself is licensed under the GNU General Public License, version 3 or (at your option) any later
version; see [LICENSE](LICENSE). This file lists the software it contains, builds with, or ships in its binary
packages, with the licence of each. The notice file that goes into the binary packages is
[packaging/licenses/NOTICE-third-party.txt](packaging/licenses/NOTICE-third-party.txt); the Qt notice is
[packaging/licenses/NOTICE-Qt.txt](packaging/licenses/NOTICE-Qt.txt).

## In the source repository

| Component | Where | Licence | Notes |
| --- | --- | --- | --- |
| fstapi (from [libfst](https://github.com/gtkwave/libfst), revision in `third_party/fstapi/VENDOR.txt`) | `third_party/fstapi/`, linked statically | MIT, Copyright (c) 2009-2025 Tony Bybell | Licence text: `third_party/fstapi/LICENSE`. Used to read and write FST waveform files. |
| LZ4 (inside fstapi) | `third_party/fstapi/lz4.c`, `lz4.h` | BSD 2-Clause, Copyright (c) 2011-2020 Yann Collet | Text in `third_party/fstapi/LICENSE`. |
| FastLZ (inside fstapi) | `third_party/fstapi/fastlz.c`, `fastlz.h` | MIT, Copyright (c) 2005-2007 Ariya Hidayat | Text in `third_party/fstapi/LICENSE`. |

## Fetched when you build

CMake downloads these with FetchContent on the first configure (network access needed). They are not stored in
this repository.

| Component | Version | Licence | Used for |
| --- | --- | --- | --- |
| zlib | 1.3.1 | zlib licence, Copyright (c) 1995-2024 Jean-loup Gailly and Mark Adler | Compression in fstapi. Linked statically into the lvdsim programs and shipped inside them. |
| Catch2 | 3.7.1 | Boost Software License 1.0 | Unit tests of libwave and the tools only. Not shipped. |

## NVC, the simulation kernel

The simulator is [NVC](https://github.com/nickg/nvc) by Nick Gasson and contributors, GPL-3.0-or-later. lvdsim
builds it from the branch `lvdsim` of the fork `https://github.com/giorgiodelmondo/nvc` (upstream plus the changes listed in
[kernel/PATCHES.md](kernel/PATCHES.md); changes to NVC are under NVC's own licence, GPL-3.0-or-later). This repository does not
contain NVC; `kernel/build/fetch-nvc.sh` fetches it ([docs/getting-the-kernel.md](docs/getting-the-kernel.md)).
lvdsim never links NVC: it runs the `nvc` program as a separate process (command line, standard input and
output, waveform files). The binary packages ship `nvc`, its VHDL libraries and its licence text
(`NVC-GPL-3.0.txt`). Its source is the upstream repository plus our patches.

## Libraries used by the programs

| Component | Licence | Where it appears |
| --- | --- | --- |
| Qt 6 (Core, Gui, Widgets, Network, Test; platform and image plugins) | LGPL-3.0 (with GPL-3.0 as a base) | Linked dynamically by `lvdsim-gui`. The Windows package ships the unmodified Qt DLLs, the Linux bundle the Qt libraries of the build machine. No Qt Charts, Qt DataVisualization or Qt Quick. Qt source: <https://download.qt.io/> and <https://code.qt.io/>. You may replace the Qt libraries with your own build of the same version. |

## Only in the binary packages

These are runtime libraries that `nvc` and the programs need. They are not part of the source repository; they
are copied from the build machine (MSYS2 `mingw64` on Windows, the distribution on Linux). Each keeps its own
licence; the licence files that the package manager installed are copied to the `licenses` folder of the
package where the distribution provides them.

| Component | Licence |
| --- | --- |
| MinGW-w64 runtime (`libstdc++`, `libgcc`, `libwinpthread`), Windows package | GPL-3.0 with the GCC Runtime Library Exception; winpthreads under an MIT/BSD style licence |
| LLVM (code generation in `nvc`) | Apache-2.0 with LLVM Exceptions |
| Tcl 8.6 (the interactive shell of `nvc`) | Tcl/Tk licence (BSD style) |
| GNU readline (line editing in the shell) | GPL-3.0 |
| libffi | MIT |
| zstd | BSD (dual BSD / GPL-2.0; used under BSD) |
| expat, libxml2, libiconv, ncurses, termcap, zlib (as DLLs of `nvc`) | their own permissive or LGPL licences, as installed by MSYS2 |

The Linux `.run` installer and bundle use the system or bundled shared libraries of the distribution the package
was built on (glibc and the graphics stack are never bundled).

## Not used

Anything not listed above is not part of lvdsim. In particular the project contains no code, documentation text,
Tcl scripts or help strings of other commercial simulators; compatibility is limited to command names and
option syntax (see [CONTRIBUTING.md](CONTRIBUTING.md)).

## Keeping this file correct

Adding a dependency needs a note here and in `packaging/licenses/NOTICE-third-party.txt` before it is merged,
and a licence that is compatible with GPL-3.0-or-later.
