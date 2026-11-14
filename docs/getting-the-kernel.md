# Getting the NVC kernel

lvdsim runs the [NVC](https://github.com/nickg/nvc) simulator as a separate program. NVC is not part of this
repository: `kernel/nvc/` is ignored by git, so a fresh clone has no kernel. The kernel is the branch `lvdsim` of the
maintainer's fork of NVC, `https://github.com/giorgiodelmondo/nvc.git`: upstream NVC plus the changes lvdsim needs, which are
also prepared as pull requests for mainline (see [kernel/PATCHES.md](../kernel/PATCHES.md)). Nothing is patched at build
time: the clone of the branch is the kernel. The script `kernel/build/fetch-nvc.sh` makes the clone.

## Use

From the repository root, in Git Bash (Windows), WSL or any Linux shell. `git` is the only requirement.

```sh
kernel/build/fetch-nvc.sh                 # clones the fork, branch lvdsim, into kernel/nvc and prints the commit
kernel/build/fetch-nvc.sh --dest ~/nvc    # or somewhere else
kernel/build/fetch-nvc.sh --update        # an existing clone: fetch and fast-forward, only if its working tree is clean
```

Environment variables: `NVC_URL` (repository, default the fork above; a local path works), `NVC_BRANCH` (default `lvdsim`),
`NVC_REF` (a commit, tag or branch to check out instead of the tip of the branch).

The script never modifies an existing target without `--update`, never touches a working tree with uncommitted changes, and
deletes or moves nothing. It prints the branch and the commit that are checked out; compare it with the commit named in
`kernel/PATCHES.md` and in the `build-info.txt` of a release.

Then build the kernel:

- Windows: MSYS2 MINGW64, `kernel/build/build-nvc-msys2.sh` (packages and command line in the header of the
  script, and in [kernel-tests.md](kernel-tests.md)). The result is `kernel/nvc/build/bin/nvc.exe`.
- Linux: [building-linux.md](building-linux.md), section 2. The clone can be used directly when it is in the Linux file system:
  `cd kernel/nvc && ./autogen.sh && mkdir -p build && cd build && ../configure --enable-tcl && make -j"$(nproc)"`.

## Line endings

NVC's sources use LF. The script clones with `core.autocrlf=false` and `core.eol=lf` (stored in the clone's own
configuration), so the tree is LF on every host, Windows included; the autotools scripts need LF to run in MSYS2. A
plain `git clone` with a global `core.autocrlf=true` (the default of Git for Windows) may produce CRLF files, which break `./autogen.sh` and `configure` in MSYS2 (not checked on this branch: the existing checkout on the maintainer's machine has LF files). Do not build the kernel from `/mnt/c` or `/mnt/d` in WSL; clone the
branch into the Linux file system (`git clone -b lvdsim /mnt/d/D/lvdsim/kernel/nvc ~/build/nvc-src` works from a local
clone).

## What the script does not do

- **Harness fix for MinGW.** `test/run_regr.c` defines its own `strndup` under `__MINGW32__`, which clashes with
  the MSYS2 C library (`config.h` has `HAVE_STRNDUP`), so `bin/run_regr` does not build with MinGW as it is on the branch. It
  does not matter for building or running `nvc`; it only matters if you want to run NVC's own regression suite on Windows.
  [kernel-tests.md](kernel-tests.md) shows how to build the harness from a renamed copy of the file outside the tree, without
  editing the kernel; on Linux nothing is needed.
- It does not build anything, and it does not rebase the fork onto a newer upstream commit. Rebasing is done in the fork by
  its owner ([upstream-contribution.md](upstream-contribution.md)).

## Check

`git -C kernel/nvc log -1 --format=%H` prints the commit (at the time of writing `53d41eeae`, "Merge upstream/master into lvdsim", which contains upstream
up to `0b2837794`) and `git -C kernel/nvc status --short` must print nothing.
`kernel/nvc/build/bin/nvc.exe --version` (Windows, with `C:/msys64/mingw64/bin` on PATH) shows the commit hash too.
