#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# make-linux-run.sh: one command that builds lvdsim and produces the Linux release files in a NEW folder.
#
# Usage (Linux, or WSL Debian; from Windows use scripts\make-linux-run.bat):
#   scripts/make-linux-run.sh [options]
#
# Options:
#   --out DIR          output folder (default ~/build/lvdsim/release/<version>-<yyyymmdd-HHMMSS>); must not exist,
#                      nothing is ever deleted or overwritten
#   --version X.Y.Z    default: the version in project(lvdsim VERSION ...) of the top-level CMakeLists.txt
#   --nvc-build DIR    nvc build tree (bin/nvc and lib/). Default: the newest ~/build/nvc-linux* or ~/build/nvc-fork-linux* that has both.
#                      Its version line must contain the commit of kernel/nvc (the lvdsim branch of the NVC fork),
#                      otherwise it was not built from the current kernel and the script stops (rebuild: clone the
#                      branch as in docs/building-linux.md, section 2).
#   --force-nvc        skip that freshness check
#   --portable         also write lvdsim-<ver>-linux-x86_64-portable.tar.gz (bundle folder plus portable.txt)
#   --skip-build       reuse ~/build/lvdsim/linux-release (the GUI must have been built in it)
#   --compress xz|gz   payload compression of the .run (default xz)
#   --jobs N           parallel build jobs (default: all cores)
#   --write-out-path F write the output folder path into file F at the start (used by make-linux-run.bat)
#   -h, --help         this text
#
# Produces in the output folder:
#   lvdsim-<ver>-linux-x86_64.run             self-extracting installer (scripts/package-linux-appdir.sh builds it)
#   lvdsim-<ver>-linux-x86_64-portable.tar.gz (only with --portable)
#   SHA256SUMS      "<hash>  <file>"          build-info.txt
#   lvdsim-<ver>-linux-x86_64/                the bundle folder (the installer payload, no portable.txt); may be deleted afterwards
#
# Verified before it says OK: the .run --check and --help, the payload checksum recomputed from the file, the isolation scan
# (every ELF of the bundle resolves inside the bundle or on the host list: glibc, GL stack, udev, systemd), a smoke install into
# a throwaway prefix and HOME (--no-path) with vlib/vcom/vsim -c from the installed symlinks, a headless GUI start whose settings
# must land in HOME, --uninstall and a check that the HOME listing is back to its initial state; with --portable also an
# extract of the tarball, a smoke run and a headless GUI start whose settings must land in <dir>/data and not in HOME.
# Exit codes: 0 ok, 1 failure (message says which check), 2 bad usage.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

out=
version=
nvcBuild=
forceNvc=0
portable=0
skipBuild=0
compress=xz
jobs=$(nproc 2>/dev/null || echo 4)
outPathFile=

die() { echo "ERROR: $*" >&2; exit 1; }
usage() { sed -n '3,/^set -euo/p' "$0" | sed -e '/^set -euo/d' -e 's/^# \{0,1\}//'; }
need() { [ $# -ge 2 ] || { echo "option $1 needs a value" >&2; exit 2; }; }

while [ $# -gt 0 ]; do
    case "$1" in
        --out) need "$@"; out=$2; shift ;;
        --version) need "$@"; version=$2; shift ;;
        --nvc-build) need "$@"; nvcBuild=$2; shift ;;
        --force-nvc) forceNvc=1 ;;
        --portable) portable=1 ;;
        --skip-build) skipBuild=1 ;;
        --compress) need "$@"; compress=$2; shift ;;
        --jobs) need "$@"; jobs=$2; shift ;;
        --write-out-path) need "$@"; outPathFile=$2; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1 (see --help)" >&2; exit 2 ;;
    esac
    shift
done
case "$compress" in xz|gz) ;; *) echo "--compress must be xz or gz" >&2; exit 2 ;; esac
case "$jobs" in ''|*[!0-9]*) echo "--jobs needs a number" >&2; exit 2 ;; esac

# ---- version: single source of truth is CMakeLists.txt ----
if [ -z "$version" ]; then
    version=$(sed -n 's/^project(lvdsim VERSION \([0-9][0-9.]*\).*/\1/p' "$root/CMakeLists.txt")
    [ -n "$version" ] || die "cannot read the version: no project(lvdsim VERSION x.y.z ...) line in $root/CMakeLists.txt (or pass --version)"
fi
stamp=$(date +%Y%m%d-%H%M%S)
[ -n "$out" ] || out=$HOME/build/lvdsim/release/$version-$stamp
case "$out" in /*) ;; *) out=$PWD/$out ;; esac
[ -e "$out" ] && die "$out already exists. This script never deletes or overwrites: pass another --out."
name=lvdsim-$version-linux-x86_64
portName=$name-portable
runFile=$out/$name.run
tarFile=$out/$portName.tar.gz

# ---- nvc build: find it and check that it was built from the commit of kernel/nvc ----
if [ -z "$nvcBuild" ]; then
    for d in $(ls -dt "$HOME"/build/nvc-linux* "$HOME"/build/nvc-fork-linux* 2>/dev/null); do
        if [ -x "$d/bin/nvc" ] && [ -d "$d/lib" ]; then nvcBuild=$d; break; fi
    done
    [ -n "$nvcBuild" ] || die "no nvc build found (~/build/nvc-linux*/bin/nvc or ~/build/nvc-fork-linux*/bin/nvc, with lib/). Build one: clone the branch as in docs/building-linux.md section 2, or pass --nvc-build DIR."
fi
[ -x "$nvcBuild/bin/nvc" ] && [ -d "$nvcBuild/lib" ] || die "$nvcBuild has no bin/nvc and lib/"
# The commit of the kernel checkout (git may refuse a checkout on a Windows drive as "dubious ownership": allow it for this call).
nvcCommit=$(git -c safe.directory='*' -C "$root/kernel/nvc" rev-parse HEAD 2>/dev/null || true)
nvcDirty=$(git -c safe.directory='*' -C "$root/kernel/nvc" status --porcelain 2>/dev/null | wc -l)
nvcVersionLine=$("$nvcBuild/bin/nvc" --version 2>/dev/null | head -1 || true)
if [ -z "$nvcCommit" ]; then
    echo "   warning: kernel/nvc is not a git checkout here; cannot verify that $nvcBuild was built from it"
elif [ "$forceNvc" = 0 ]; then
    case "$nvcVersionLine" in
        *"${nvcCommit:0:7}"*) ;;
        *) die "$nvcBuild/bin/nvc reports '$nvcVersionLine', which does not contain the commit ${nvcCommit:0:9} of kernel/nvc,
       so it was not built from the current kernel. Rebuild it from the branch lvdsim (docs/building-linux.md section 2, new build
       directory, for example ~/build/nvc-linux2), or pass --force-nvc to skip this check." ;;
    esac
fi
echo "== lvdsim $version -> $out"
[ -z "$outPathFile" ] || echo "$out" >"$outPathFile"
echo "   nvc build: $nvcBuild ($nvcVersionLine; kernel/nvc commit ${nvcCommit:0:9})"
for t in patchelf strip ldd objdump dpkg tar sha256sum; do command -v $t >/dev/null || die "missing tool: $t"; done
[ "$compress" = xz ] && ! command -v xz >/dev/null && die "xz not found (use --compress gz)"

mkdir -p "$out"
work=$(mktemp -d "${TMPDIR:-/tmp}/lvdsim-rel.XXXXXX")   # created here, removed at the end
trap 'rm -rf "$work"' EXIT

# ---- build + bundle + installer: scripts/package-linux-appdir.sh ----
echo "== build and bundle (scripts/package-linux-appdir.sh)"
args=
[ "$skipBuild" = 1 ] && args=--skip-build
CMAKE_BUILD_PARALLEL_LEVEL=$jobs NVC_BUILD=$nvcBuild OUT_DIR=$out COMPRESS=$compress bash "$root/scripts/package-linux-appdir.sh" $args
bundle=$out/$name
[ -d "$bundle" ] || die "bundle folder $bundle was not created"
[ -f "$runFile" ] || die "installer $runFile was not created"
[ -e "$bundle/portable.txt" ] && die "portable.txt in the installer payload"

# ---- portable tarball ----
if [ "$portable" = 1 ]; then
    echo "== portable tarball"
    mkdir -p "$work/p/$portName"
    cat >"$work/p/$portName/portable.txt" <<EOF
lvdsim portable mode marker. While this file sits next to AppRun the program keeps its settings in
data/lvdsim-gui.ini and compiled libraries in data/libraries (nothing in ~/.config or ~/.local/share). Delete it to turn that off.
EOF
    cat >"$work/p/$portName/README-portable.txt" <<EOF
lvdsim $version - portable edition (Linux x86_64)

No installation. Unpack this archive anywhere (a USB stick works) and run ./AppRun for the GUI, or "./AppRun --run vsim ..."
(also vlib, vcom, vlog, lvdsim-cli). See README.txt for the requirements (glibc and the host graphics stack).

Portable mode is switched on by portable.txt next to AppRun (delete it for the normal per-user settings in ~/.config).
Everything the program saves stays in the data/ folder of this directory:
  data/lvdsim-gui.ini   window layout and options
  data/libraries        default output folder of Tools > Compile libraries
Do not run it from a read-only location: the GUI then falls back to the normal per-user settings and prints a note.
Temporary files go to /tmp. "lvdsim-gui --settings FILE" still wins; LVDSIM_PORTABLE=1 turns portable mode on without the marker.
Licence: GPL-3.0-or-later, see licenses/.
EOF
    ( cd "$out" && tar --sort=name --owner=0 --group=0 --numeric-owner --mtime=@0 --transform "s,^$name,$portName," -cf "$work/portable.tar" "$name" )
    ( cd "$work/p" && tar --owner=0 --group=0 --numeric-owner --mtime=@0 -rf "$work/portable.tar" "$portName/portable.txt" "$portName/README-portable.txt" )
    gzip -9n <"$work/portable.tar" >"$tarFile"
fi

# ---- checksums, build-info ----
echo "== checksums and build-info"
files=("$name.run")
[ "$portable" = 1 ] && files+=("$portName.tar.gz")
( cd "$out" && sha256sum "${files[@]}" >SHA256SUMS )
gitCommit="none (no git commit available)"
if g=$(git -C "$root" rev-parse HEAD 2>/dev/null) && [ -n "$g" ]; then gitCommit=$g; fi
nvcRemote=$(git -c safe.directory='*' -C "$root/kernel/nvc" remote get-url origin 2>/dev/null || echo unknown)
qtVer=$(qmake6 -query QT_VERSION 2>/dev/null || echo unknown)
gcc=$(g++ --version 2>/dev/null | head -1 || echo unknown)
minGlibc=$(sed -n '1p' "$bundle/licenses/MIN-GLIBC.txt" 2>/dev/null || echo unknown)
{
    echo "lvdsim $version (linux x86_64)"
    echo "built:        $(date '+%Y-%m-%d %H:%M:%S %z')"
    echo "git commit:   $gitCommit"
    echo "nvc commit:   ${nvcCommit:-unknown} (kernel/nvc, branch lvdsim, $nvcRemote)"
    echo "nvc tree:     $nvcDirty uncommitted change(s) in kernel/nvc; build tree $nvcBuild"
    echo "nvc version:  $("$nvcBuild/bin/nvc" --version 2>/dev/null | head -1)"
    echo "Qt:           $qtVer (Debian package, bundled)"
    echo "compiler:     $gcc"
    echo "$minGlibc"
    echo "built on:     $(. /etc/os-release; echo "$PRETTY_NAME")"
    echo "compression:  $compress"
    echo "files:        ${files[*]}"
} >"$out/build-info.txt"

# ================= verification =================
echo "== verify"
sh "$runFile" --check >/dev/null || die "$name.run --check failed"
sh "$runFile" --help >"$work/help.txt" || die "$name.run --help failed"
grep -q -- '--uninstall' "$work/help.txt" || die "--help output has no --uninstall"
echo "  --check and --help ok"

# payload checksum recomputed from the file, independent of the stub's own code
want=$(sed -n 's/^LVD_PAYLOAD_SHA256="\(.*\)"/\1/p' "$runFile" | head -1)
psize=$(sed -n 's/^LVD_PAYLOAD_SIZE=\([0-9]*\)/\1/p' "$runFile" | head -1)
have=$(tail -c "$psize" "$runFile" | sha256sum | cut -d' ' -f1)
[ -n "$want" ] && [ "$want" = "$have" ] || die "payload checksum: stub says '$want', file has '$have'"
echo "  payload checksum $have ($psize bytes)"

# isolation scan: bundled ELF files resolve inside the bundle or are on the host list
hostRe='^(linux-vdso\.so.*|ld-linux.*|libc\.so.*|libm\.so.*|libdl\.so.*|libpthread\.so.*|librt\.so.*|libutil\.so.*|libresolv\.so.*|libanl\.so.*|libnsl\.so.*|libmvec\.so.*|libGL\.so.*|libGLX\.so.*|libGLdispatch\.so.*|libEGL\.so.*|libOpenGL\.so.*|libGLESv2\.so.*|libgbm\.so.*|libdrm.*\.so.*|libvulkan\.so.*|libudev\.so.*|libsystemd\.so.*)$'
nelf=0; bad=0
while IFS= read -r f; do
    head -c 4 "$f" | grep -q '^.ELF' || continue
    nelf=$((nelf + 1))
    while IFS= read -r line; do
        case "$line" in
            *"not found"*) echo "  unresolved in ${f#"$bundle"/}: $line" >&2; bad=$((bad + 1)) ;;
            *"=>"*)
                n=$(echo "${line%%=>*}" | tr -d ' \t'); p=${line#*=>}; p=$(echo "${p%% (*}" | tr -d ' \t')
                [ -n "$p" ] || continue
                case "$p" in "$bundle"/*) continue ;; esac
                if ! echo "$n" | grep -Eq "$hostRe"; then echo "  ${f#"$bundle"/} uses $n from outside the bundle ($p)" >&2; bad=$((bad + 1)); fi ;;
        esac
    done < <(env -i PATH=/usr/bin:/bin LD_LIBRARY_PATH= ldd "$f" 2>/dev/null)
done < <(find "$bundle" -type f \( -name '*.so' -o -name '*.so.*' -o -perm -u+x \))
[ "$bad" = 0 ] || die "isolation scan: $bad problem(s) (listed above)"
[ "$nelf" -gt 50 ] || die "isolation scan looked at only $nelf ELF files"
echo "  isolation scan: $nelf ELF files, all resolve inside the bundle or on the host list"

# tiny design for the smoke runs
cat >"$work/smoke.vhd" <<'EOF'
entity smoke is end entity;
architecture a of smoke is
  signal s : bit := '0';
begin
  process begin
    wait for 1 ns; s <= '1';
    wait for 1 ns;
    report "lvdsim-smoke-ok";
    wait;
  end process;
end architecture;
EOF
runFlow() {   # <bin dir with the tool names> <HOME> <dir to work in>
    mkdir -p "$3"
    cp "$work/smoke.vhd" "$3/"
    ( cd "$3" && env -i HOME="$2" PATH="$1:/usr/bin:/bin" TMPDIR="$3" vlib work >/dev/null &&
      env -i HOME="$2" PATH="$1:/usr/bin:/bin" TMPDIR="$3" vcom -2008 smoke.vhd >/dev/null &&
      env -i HOME="$2" PATH="$1:/usr/bin:/bin" TMPDIR="$3" timeout 120 vsim -c -do 'run 10 ns; quit' work.smoke >"$3/vsim.out" 2>&1 ) \
        || { cat "$3/vsim.out" 2>/dev/null >&2; return 1; }
    grep -q lvdsim-smoke-ok "$3/vsim.out"
}
guiStart() {   # <launcher> <HOME> <dir with the compiled work library>: headless GUI start that saves its settings on close
    ( cd "$3" && env -i HOME="$2" PATH=/usr/bin:/bin TMPDIR="$3" QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR="$3"         timeout 120 "$1" work.smoke -do 'run 10 ns' --screenshot "$3/gui.png" >"$3/gui.out" 2>&1 ) || { cat "$3/gui.out" >&2; return 1; }
    [ -s "$3/gui.png" ]
}

# smoke install into a throwaway prefix and HOME
th=$work/home; tp=$work/prefix
mkdir -p "$th"
listing() { ( cd "$1" && find . | LC_ALL=C sort ); }
listing "$th" >"$work/home.before"
env -i HOME="$th" PATH=/usr/bin:/bin sh "$runFile" --prefix "$tp" --yes --no-path >"$work/install.out" 2>&1 \
    || { cat "$work/install.out" >&2; die "smoke install failed"; }
for t in vlib vmap vcom vlog vsim lvdsim-cli lvdsim-gui; do
    [ -L "$th/.local/bin/$t" ] || die "smoke install: symlink $t missing"
done
[ -e "$tp/portable.txt" ] && die "installed tree has portable.txt"
runFlow "$th/.local/bin" "$th" "$work/run-installed" || die "smoke install: vlib/vcom/vsim -c flow failed"
echo "  smoke install: symlinks ok, vlib/vcom/vsim -c ran from the installed tree"
if guiStart "$th/.local/bin/lvdsim-gui" "$th" "$work/run-installed"; then
    [ -e "$tp/data/lvdsim-gui.ini" ] && die "installed GUI wrote data/ inside the prefix (portable mode must be off)"
    if [ -d "$th/.config" ]; then echo "  installed GUI: settings in HOME/.config (normal mode)"
    else echo "WARNING: installed GUI left no settings in HOME (offscreen start did not save?)"; fi
else
    die "installed lvdsim-gui offscreen start failed"
fi
env -i HOME="$th" PATH=/usr/bin:/bin sh "$runFile" --uninstall --prefix "$tp" >"$work/uninstall.out" 2>&1 \
    || { cat "$work/uninstall.out" >&2; die "uninstall failed"; }
# the GUI session may have created ~/.config/lvdsim: that is the user's data and is not the installer's. Compare the rest.
rm -rf "$th/.config" "$th/.cache"   # created by the GUI run in the throwaway HOME above
listing "$th" >"$work/home.after"
cmp -s "$work/home.before" "$work/home.after" || { diff "$work/home.before" "$work/home.after" >&2 || true; die "HOME listing differs after uninstall"; }
[ -e "$tp/AppRun" ] && die "prefix not cleaned by uninstall"
echo "  uninstall: HOME listing identical to the initial one, prefix removed"

# portable tarball
if [ "$portable" = 1 ]; then
    pdir=$work/portable-x; mkdir -p "$pdir"
    ( cd "$pdir" && tar -xzf "$tarFile" )
    top=$pdir/$portName
    [ -f "$top/portable.txt" ] || die "portable tarball lacks $portName/portable.txt"
    [ -x "$top/AppRun" ] && [ -x "$top/bin/vsim" ] && [ -f "$top/nvc/bin/nvc" ] && [ -f "$top/README-portable.txt" ] || die "portable tarball is incomplete"
    ph=$work/home-portable; mkdir -p "$ph"
    listing "$ph" >"$work/ph.before"
    mkdir -p "$work/bin-portable"
    for t in vlib vmap vcom vlog vsim; do ln -s "$top/AppRun" "$work/bin-portable/$t"; done
    runFlow "$work/bin-portable" "$ph" "$work/run-portable" || die "portable: vlib/vcom/vsim -c flow failed"
    guiStart "$top/AppRun" "$ph" "$work/run-portable" || die "portable: offscreen GUI start failed"
    [ -f "$top/data/lvdsim-gui.ini" ] || die "portable: settings file $portName/data/lvdsim-gui.ini was not written"
    listing "$ph" >"$work/ph.after"
    cmp -s "$work/ph.before" "$work/ph.after" || { diff "$work/ph.before" "$work/ph.after" >&2 || true; die "portable GUI run changed HOME"; }
    echo "  portable tarball: flow ok, GUI settings in $portName/data, HOME untouched"
fi

echo "== done"
echo "Output folder: $out"
( cd "$out" && ls -l --time-style=long-iso "${files[@]}" SHA256SUMS build-info.txt | awk '{print "  " $5 "\t" $8}' && sed 's/^/  sha256 /' SHA256SUMS )
echo OK
