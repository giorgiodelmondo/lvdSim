# Security policy

## Supported versions

| Version | Supported |
| --- | --- |
| 0.1.x | yes |
| older, or unreleased development snapshots | no |

Security fixes go into the latest 0.1.x release. The NVC kernel is a fork of upstream NVC; a problem that is in
NVC itself (not in our patches) should also be reported to the NVC project, and we will pass on fixes.

## Reporting a vulnerability

Please **do not open a public issue**. Use GitHub's private vulnerability reporting: on the repository page open
the *Security* tab and choose *Report a vulnerability*. Include the version and package type, the operating
system, what you found, and the steps to reproduce it (a small design or script that triggers it is ideal).
We have not set up an email address for security reports.

## What to expect

- We aim to acknowledge a report within about a week. This is a small volunteer project, so it may take longer;
  if you hear nothing after two weeks, add a comment to the report.
- We will tell you whether we consider it a vulnerability, work on a fix, and agree a date for disclosure with
  you. We will credit you in the release notes if you wish.
- Please give us reasonable time to fix the problem before you publish it.

## What is in scope, and what is not

lvdsim is a simulator front end. Some behaviour that would be a vulnerability in other software is how it works:

- **The simulator runs the HDL and Tcl you give it, and it is not sandboxed.** Simulating a design or sourcing
  a `.do` script runs code with your user's rights: Tcl can read and write files and start programs, and
  VHDL and Verilog designs can read and write files. Do not run designs or scripts you do not trust, or run them
  in a virtual machine or container. A design that does something harmful by using these features is not a
  vulnerability.
- **The library compile scripts** (`scripts/libs`, Tools > Compile libraries) execute shell and Tcl on your
  machine and call `nvc` on files from the vendor installation you point them to. They trust the folder you
  give them.
- **The installers** (Windows Inno Setup installer and zip, Linux `.run` self-extracting installer) unpack
  files into a folder you choose. The installers may add that folder to `PATH` when you allow it. Packages are
  not signed with a code-signing certificate (none is configured in the repository).

In scope: memory errors or crashes in `libwave` when reading an FST or live-stream file from an untrusted
source, in the command line parsing and the transcript and protocol parsers of the tools and the GUI, an
installer or script that writes outside the chosen folder or runs commands from data it should only read, and
problems in our kernel changes (the `lvdsim` branch).
