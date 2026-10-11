# REPORT.md — BSDSF24M025-OS-A02 Analysis Report

## Feature-1 (v1.0.0): Basic Setup

Repository created with the required structure (`src/`, `bin/`, `obj/`, `man/`),
the instructor's starter code (`src/ls-v1.0.0.c`), a hand-written `Makefile`,
`README.md`, and this report. The starter lists a directory's entries one per
line using `opendir()` / `readdir()` / `closedir()`, skipping hidden files
(names starting with `.`).

## Feature-2 (v1.1.0): Long Listing (-l)

`getopt()` parses the `-l` option. `do_ls_long()` works in two passes: first it
reads every entry with `readdir()` + `lstat()` into a dynamic array (grown with
`realloc()`), recording the maximum widths of the link-count, owner, group, and
size columns; then it prints the aligned table. Owner/group names come from
`getpwuid()` / `getgrgid()`. Permissions are built bit-by-bit from `st_mode`
with the `S_IS*` type macros and the `S_IRUSR`/`S_ISUID`/… bit masks; setuid,
setgid, and sticky replace the `x` position with `s`/`S` or `t`/`T`.
Timestamps use `strftime()`: `"Mon DD HH:MM"` for files newer than ~6 months,
`"Mon DD  YYYY"` otherwise. Symlinks are shown as `name -> target` via
`readlink()`, and the `total` line sums `st_blocks / 2` (1K blocks, as in GNU
`ls`). `#define _DEFAULT_SOURCE` at the top re-exposes the POSIX declarations
(`getopt`, `lstat`, `strdup`, `readlink`, `S_ISVTX`) that strict `-std=c11`
hides.

### Q&A

**Q1. What is the crucial difference between `stat()` and `lstat()`? When is
`lstat()` more appropriate for `ls`?**

`stat()` *follows* symbolic links: if the path names a symlink, it returns the
metadata of the **target** file (size, permissions, timestamps of whatever the
link points to). `lstat()` does **not** follow symlinks: it returns the metadata
of the **link itself** (a small file whose size is the length of the target
path, whose type bit is `S_IFLNK`). For `ls`, `lstat()` is correct because the
user asks "what is *this entry*", not "what does it point to" — with `stat()`,
a symlink would be misreported as the target's type/size, and a broken
(dangling) symlink would make `stat()` fail outright and vanish from the
listing. Our `-l` uses `lstat()` and then calls `readlink()` separately to
print `name -> target`.

**Q2. How do you extract the file type and permissions from `st_mode` using
bitwise operators and macros?**

The file type lives in the high bits of `st_mode`. The portable way is the
`S_IS*()` macros, which test those bits: `S_ISDIR(m)` for directories,
`S_ISLNK(m)` for symlinks, `S_ISREG(m)` for regular files, plus `S_ISCHR`,
`S_ISBLK`, `S_ISFIFO`, `S_ISSOCK`. The permission bits are the low 12 bits,
tested with bitwise AND: `mode & S_IRUSR` (owner read), `S_IWUSR`,
`S_IXUSR`, `S_IRGRP`…`S_IXOTH`. The three special bits are `S_ISUID`
(set-user-ID), `S_ISGID` (set-group-ID), and `S_ISVTX` (sticky). For example,
`if (mode & S_ISUID) perm[3] = (mode & S_IXUSR) ? 's' : 'S';` renders the
setuid flag in the owner's execute slot — lowercase `s` when execute is also
set, uppercase `S` when it isn't.

## v1.2.0 — Column display, down-then-across

The default (non `-l`) path now prints names in columns instead of one per
line: all names are read into a dynamic array first, the terminal width is
queried with `ioctl()`, and entries are printed top-to-bottom, then
left-to-right, like GNU `ls`.

### Q&A

**Q1. Explain the down-then-across logic — why is a single loop insufficient?**

Down-then-across means columns fill vertically: the first column holds the
first entries, the next column the following ones. The position of an item is
therefore a 2-D function of its index: `idx = column * nrows + row`. A single
sequential loop can't produce this layout because you can't print row 0
without already knowing what goes in *every* column of row 0 — i.e. you need
items 0, nrows, 2*nrows, … in hand before printing the first line. That forces
a two-phase design: (1) read everything into memory and compute `ncols` from
the longest name and terminal width, and (2) nested loops `for r { for c { } }`
using the index mapping `idx = c*nrows + r`. GNU `ls` works exactly this way.

**Q2. What is the purpose of `ioctl()` here? What are the limitations of a
fixed 80-column fallback?**

`ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)` asks the kernel for the actual size of
the terminal window backing stdout; `ws.ws_col` is its width in characters.
We need it to compute how many columns fit (`ncols = term_width /
(longest+2)`), so the layout adapts to wide/narrow terminals. The call fails
when stdout is not a terminal (piped to a file or another command) and a
fallback of 80 columns is used. Limitations: 80 is just a historical guess —
a narrower real terminal will wrap mid-line and shatter the column alignment,
a wider one wastes space; also, redirecting to a pipe means the "screen width"
is meaningless, which is why GNU `ls` switches to one-per-line when stdout is
not a tty (a refinement we haven't added yet).
