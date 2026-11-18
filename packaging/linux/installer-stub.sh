#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# lvdsim self-extracting installer (POSIX sh). The compressed tar of the AppDir follows the line
# "__PAYLOAD_BELOW__" at the end of this file. scripts/package-linux-appdir.sh fills in the four LVD_ values.
# Run "sh <file> --help".

LVD_VERSION="@VERSION@"
LVD_PAYLOAD_SIZE=@SIZE@
LVD_PAYLOAD_SHA256="@SHA256@"
LVD_COMPRESSION="@COMPRESSION@"

self=$0
MANIFEST_NAME=.lvdsim-install-manifest
MANIFEST_MAGIC="lvdsim-install-manifest 1"
TAB=$(printf '	')
INSTALLING=0
YES=0
QUIET=0

say() { [ "$QUIET" = 1 ] || printf '%s\n' "$*"; }
warn() { printf 'warning: %s\n' "$*" >&2; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

usage() {
    cat <<EOF
lvdsim $LVD_VERSION installer for Linux x86_64

Usage: sh $(basename "$self") [options]

Install (default):
  --prefix DIR     install into DIR (default: \$HOME/.local/opt/lvdsim, /opt/lvdsim when run as root)
  --yes, -y        do not ask for confirmation
  --no-symlinks    do not create the command symlinks (lvdsim-gui lvdsim-cli vlib vmap vcom vlog vsim)
  --no-desktop     do not create the .desktop entry and the icon
  --no-path        do not touch shell startup files (this is the default; accepted for scripts)
  --add-path       append a marked PATH block to ~/.profile and ~/.bashrc (only on request)

Other actions:
  --uninstall      remove what a previous install created (needs --prefix if it was not the default)
  --extract DIR    unpack the bundle into DIR and do nothing else
  --list           list the files of the bundle
  --check          verify the SHA-256 checksum of the payload and exit
  --license        print the licence text (GPL-3.0-or-later)
  --version        print the version
  --help           this text

Symlinks go to \$HOME/.local/bin (/usr/local/bin as root), the desktop entry to \$HOME/.local/share/applications and
the icon to \$HOME/.local/share/icons/hicolor/256x256/apps (/usr/share/... as root). Everything the installer creates is
listed in <prefix>/$MANIFEST_NAME, which --uninstall reads. An existing install at the prefix is replaced; a directory
that is not an lvdsim install is never overwritten.
EOF
}

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum | cut -d' ' -f1
    elif command -v shasum >/dev/null 2>&1; then shasum -a 256 | cut -d' ' -f1
    elif command -v openssl >/dev/null 2>&1; then openssl dgst -sha256 | sed 's/^.*= //'
    else die "no sha256sum, shasum or openssl found to verify the payload"; fi
}

payload() { tail -c "$LVD_PAYLOAD_SIZE" "$self"; }

decompress() {
    case "$LVD_COMPRESSION" in
        xz) command -v xz >/dev/null 2>&1 || die "this installer is compressed with xz; install the xz tool (xz-utils) first"
            xz -dc ;;
        gz) gzip -dc ;;
        *) die "unknown payload compression '$LVD_COMPRESSION'" ;;
    esac
}

verify() {
    [ -f "$self" ] || die "cannot read $self"
    size=$(wc -c <"$self" | tr -d ' ')
    [ "$size" -gt "$LVD_PAYLOAD_SIZE" ] || die "the installer file is truncated"
    mark=$(tail -c $((LVD_PAYLOAD_SIZE + 18)) "$self" | head -c 17)
    [ "$mark" = "__PAYLOAD_BELOW__" ] || die "payload marker not found: the installer file is damaged or truncated"
    have=$(payload | sha256)
    [ "$have" = "$LVD_PAYLOAD_SHA256" ] || die "checksum mismatch: the installer file is corrupt (expected $LVD_PAYLOAD_SHA256, got $have)"
}

licence_summary() {
    cat <<EOF
lvdsim $LVD_VERSION
Licence: GPL-3.0-or-later. It includes the NVC simulator (GPL-3.0-or-later), fstapi (MIT) and shared libraries
of Debian 13 (Qt 6 under LGPL-3.0, LLVM, Tcl, ICU and others, each under its own licence).
The notices are in <prefix>/licenses/ after installation; "--license" prints the GPL text.
EOF
}

abspath() { case "$1" in /*) printf '%s' "$1" ;; *) printf '%s/%s' "$PWD" "$1" ;; esac; }

# mkdir -p that records every directory it creates in the manifest (parents first).
mkdir_rec() {
    [ -d "$1" ] && return 0
    [ "$(dirname "$1")" = "$1" ] && return 0
    mkdir_rec "$(dirname "$1")" || return 1
    mkdir "$1" || return 1
    [ -n "$MANIFEST" ] && printf 'dir\t%s\n' "$1" >>"$MANIFEST"
    return 0
}

reverse_lines() { sed -n '1!G;h;$p'; }

do_uninstall() {
    up=$1
    um=$up/$MANIFEST_NAME
    [ -f "$um" ] || die "$up has no $MANIFEST_NAME: not an lvdsim install made by this installer"
    [ "$(head -n 1 "$um")" = "$MANIFEST_MAGIC" ] || die "$um is not a manifest of this installer"
    case "$up" in ""|/|"$HOME"|"$HOME"/) die "refusing to uninstall from '$up'" ;; esac
    grep "^link$TAB" "$um" | cut -f2- | while IFS= read -r p; do
        if [ -L "$p" ]; then
            t=$(readlink "$p")
            case "$t" in "$up"/*) rm -f "$p"; say "removed $p" ;; *) warn "$p no longer points into $up; left alone" ;; esac
        fi
    done
    grep "^file$TAB" "$um" | cut -f2- | while IFS= read -r p; do
        if [ -f "$p" ] || [ -L "$p" ]; then rm -f "$p"; say "removed $p"; fi
    done
    grep "^block$TAB" "$um" | cut -f2- | while IFS= read -r p; do
        if [ -f "$p" ] && grep -q '^# >>> lvdsim >>>$' "$p"; then
            tmp=$p.lvdsim-tmp.$$
            sed '/^# >>> lvdsim >>>$/,/^# <<< lvdsim <<<$/d' "$p" >"$tmp" && cat "$tmp" >"$p"
            rm -f "$tmp"
            say "removed the lvdsim PATH block from $p"
        fi
    done
    # the install directory: contents first, the manifest last
    for e in "$up"/* "$up"/.[!.]* "$up"/..?*; do
        [ -e "$e" ] || [ -L "$e" ] || continue
        [ "$e" = "$um" ] && continue
        rm -rf "$e"
    done
    dirs=$(grep "^dir$TAB" "$um" | cut -f2- | reverse_lines)
    rm -f "$um"
    oldifs=$IFS
    IFS='
'
    for p in $dirs; do
        if [ -d "$p" ] && rmdir "$p" 2>/dev/null; then say "removed directory $p"; fi
    done
    IFS=$oldifs
    # when the prefix pre-existed (empty), it stays; nothing else of ours remains
    return 0
}

on_exit() {
    if [ "$INSTALLING" = 1 ]; then
        warn "installation failed, rolling back"
        QUIET=1
        [ -f "$PREFIX/$MANIFEST_NAME" ] && do_uninstall "$PREFIX"
    fi
}

do_install() {
    MANIFEST=
    licence_summary
    say ""
    say "Install directory: $PREFIX"
    if [ "$YES" != 1 ] && [ -t 0 ]; then
        printf 'Continue? [Y/n] '
        read -r ans
        case "$ans" in ""|y|Y|yes|YES) ;; *) say "cancelled"; exit 1 ;; esac
    fi
    say "Verifying the payload checksum ..."
    verify
    case "$PREFIX" in ""|/|"$HOME"|"$HOME"/) die "refusing to install into '$PREFIX'" ;; esac
    case "$PREFIX" in *'	'*) die "the prefix must not contain a tab" ;; esac
    if [ -e "$PREFIX" ] || [ -L "$PREFIX" ]; then
        [ -d "$PREFIX" ] || die "$PREFIX exists and is not a directory"
        if [ -f "$PREFIX/$MANIFEST_NAME" ]; then
            say "Replacing the existing lvdsim install in $PREFIX"
            do_uninstall "$PREFIX"
        elif [ -n "$(ls -A "$PREFIX" 2>/dev/null)" ]; then
            die "$PREFIX exists, is not empty and is not an lvdsim install; choose another --prefix (nothing was changed)"
        fi
    fi
    # directories that do not exist yet (parents first): recorded so that --uninstall can remove them
    p=$PREFIX
    CREATED_PARENTS=
    while [ ! -d "$p" ] && [ "$p" != "/" ] && [ -n "$p" ]; do
        CREATED_PARENTS="$p $CREATED_PARENTS"
        p=$(dirname "$p")
    done
    INSTALLING=1
    MANIFEST=
    mkdir_rec "$PREFIX" || die "cannot create $PREFIX"
    MANIFEST=$PREFIX/$MANIFEST_NAME
    # the directories made before the manifest existed
    {
        printf '%s\nversion\t%s\nprefix\t%s\n' "$MANIFEST_MAGIC" "$LVD_VERSION" "$PREFIX"
    } >"$MANIFEST"
    for pre in $CREATED_PARENTS; do printf 'dir\t%s\n' "$pre" >>"$MANIFEST"; done
    say "Extracting ..."
    payload | decompress | tar -x -C "$PREFIX" --strip-components=1 || die "extraction failed"
    [ -x "$PREFIX/AppRun" ] || die "the payload has no AppRun"

    if [ "$SYMLINKS" = 1 ]; then
        mkdir_rec "$BINDIR" || die "cannot create $BINDIR"
        for n in lvdsim-gui lvdsim-cli vlib vmap vcom vlog vsim; do
            l=$BINDIR/$n
            if [ -L "$l" ] || [ -e "$l" ]; then
                warn "$l exists and was not created by lvdsim; skipped"
                continue
            fi
            ln -s "$PREFIX/AppRun" "$l" || die "cannot create $l"
            printf 'link\t%s\n' "$l" >>"$MANIFEST"
        done
    fi
    if [ "$DESKTOP" = 1 ]; then
        mkdir_rec "$APPSDIR" || die "cannot create $APPSDIR"
        mkdir_rec "$ICONDIR" || die "cannot create $ICONDIR"
        d=$APPSDIR/lvdsim.desktop
        if [ -e "$d" ] || [ -L "$d" ]; then
            warn "$d exists and was not created by lvdsim; skipped"
        else
            sed -e "s|^Exec=.*|Exec=\"$PREFIX/AppRun\" %F|" -e "s|^TryExec=.*|TryExec=$PREFIX/AppRun|" \
                "$PREFIX/lvdsim.desktop" >"$d" || die "cannot write $d"
            printf 'file\t%s\n' "$d" >>"$MANIFEST"
        fi
        i=$ICONDIR/lvdsim.png
        if [ -e "$i" ] || [ -L "$i" ]; then
            warn "$i exists and was not created by lvdsim; skipped"
        else
            cp "$PREFIX/lvdsim.png" "$i" || die "cannot write $i"
            printf 'file\t%s\n' "$i" >>"$MANIFEST"
        fi
    fi
    if [ "$ADDPATH" = 1 ]; then
        if [ "$SYMLINKS" = 1 ]; then pdir=$BINDIR; else pdir=$PREFIX/bin; fi
        for rc in "$HOME/.profile" "$HOME/.bashrc"; do
            if [ -f "$rc" ] && grep -q '^# >>> lvdsim >>>$' "$rc"; then continue; fi
            if [ ! -e "$rc" ]; then printf 'file\t%s\n' "$rc" >>"$MANIFEST"; fi
            {
                printf '# >>> lvdsim >>>\n'
                printf '# added by the lvdsim installer; "lvdsim installer --uninstall" removes this block\n'
                printf 'case ":$PATH:" in *":%s:"*) ;; *) PATH="%s:$PATH"; export PATH ;; esac\n' "$pdir" "$pdir"
                printf '# <<< lvdsim <<<\n'
            } >>"$rc"
            printf 'block\t%s\n' "$rc" >>"$MANIFEST"
            say "added a PATH block to $rc"
        done
    fi
    INSTALLING=0
    say ""
    say "lvdsim $LVD_VERSION installed in $PREFIX"
    say "  start the GUI:  $PREFIX/AppRun"
    if [ "$SYMLINKS" = 1 ]; then
        say "  commands:       $BINDIR/{lvdsim-gui,lvdsim-cli,vlib,vmap,vcom,vlog,vsim}"
        case ":$PATH:" in
            *":$BINDIR:"*) ;;
            *) say "  hint: $BINDIR is not on your PATH. Add it to your shell startup file, or reinstall with --add-path:"
               say "        export PATH=\"$BINDIR:\$PATH\"" ;;
        esac
    else
        say "  commands:       $PREFIX/bin (add it to PATH, or run $PREFIX/AppRun --run vsim ...)"
    fi
    say "  uninstall:      sh $(basename "$self") --uninstall --prefix \"$PREFIX\""
}

# ---- arguments ----
ACTION=install
PREFIX=
EXTRACT_DIR=
SYMLINKS=1
DESKTOP=1
ADDPATH=0
while [ $# -gt 0 ]; do
    case "$1" in
        --prefix) [ $# -ge 2 ] || die "--prefix needs a directory"; PREFIX=$2; shift ;;
        --prefix=*) PREFIX=${1#--prefix=} ;;
        --yes|-y) YES=1 ;;
        --no-symlinks) SYMLINKS=0 ;;
        --no-desktop) DESKTOP=0 ;;
        --no-path) ADDPATH=0 ;;
        --add-path) ADDPATH=1 ;;
        --uninstall) ACTION=uninstall ;;
        --extract) [ $# -ge 2 ] || die "--extract needs a directory"; ACTION=extract; EXTRACT_DIR=$2; shift ;;
        --extract=*) ACTION=extract; EXTRACT_DIR=${1#--extract=} ;;
        --list) ACTION=list ;;
        --check) ACTION=check ;;
        --license|--licence) ACTION=license ;;
        --version) echo "lvdsim installer $LVD_VERSION"; exit 0 ;;
        --help|-h) usage; exit 0 ;;
        *) die "unknown option '$1' (try --help)" ;;
    esac
    shift
done

if [ "$(id -u)" = 0 ]; then
    : "${PREFIX:=/opt/lvdsim}"
    BINDIR=/usr/local/bin
    APPSDIR=/usr/share/applications
    ICONDIR=/usr/share/icons/hicolor/256x256/apps
else
    [ -n "$HOME" ] || die "HOME is not set"
    : "${PREFIX:=$HOME/.local/opt/lvdsim}"
    BINDIR=$HOME/.local/bin
    DATA=${XDG_DATA_HOME:-$HOME/.local/share}
    APPSDIR=$DATA/applications
    ICONDIR=$DATA/icons/hicolor/256x256/apps
fi
PREFIX=$(abspath "$PREFIX")
PREFIX=${PREFIX%/}
CREATED_PARENTS=

case "$ACTION" in
    check) verify; echo "checksum OK ($LVD_PAYLOAD_SHA256)" ;;
    list) verify; payload | decompress | tar -t ;;
    license) verify; payload | decompress | tar -xO --wildcards '*/licenses/lvdsim-GPL-3.0.txt' 2>/dev/null || true ;;
    extract)
        verify
        EXTRACT_DIR=$(abspath "$EXTRACT_DIR")
        if [ -e "$EXTRACT_DIR" ]; then
            [ -d "$EXTRACT_DIR" ] && [ -z "$(ls -A "$EXTRACT_DIR")" ] || die "$EXTRACT_DIR exists and is not an empty directory"
        else
            mkdir -p "$EXTRACT_DIR" || die "cannot create $EXTRACT_DIR"
        fi
        payload | decompress | tar -x -C "$EXTRACT_DIR" --strip-components=1 || die "extraction failed"
        say "unpacked into $EXTRACT_DIR"
        ;;
    uninstall) do_uninstall "$PREFIX"; say "lvdsim uninstalled from $PREFIX" ;;
    install)
        trap on_exit EXIT
        do_install
        ;;
esac
exit 0
__PAYLOAD_BELOW__
