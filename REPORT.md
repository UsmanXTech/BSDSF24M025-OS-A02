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

## v1.3.0 — Horizontal display (-x)

The `-x` option prints names in horizontal columns: entries flow left-to-right
along each row instead of top-to-bottom down each column. `getopt()` now takes
`"lx"`; the mode is stored in an `enum display_mode`
(`MODE_DEFAULT` / `MODE_LONG` / `MODE_HORIZONTAL`) and assigned on each flag,
so the **last** flag wins (`-lx` → horizontal, `-xl` → long). `do_ls()`
collects names exactly as before and then dispatches to one of two printers:
`print_columns_down()` (v1.2.0 layout) or the new `print_columns_across()`.
The horizontal printer is a single pass: it tracks `pos`, the number of
characters used on the current line, and starts a new line when
`pos + col_width` would exceed the terminal width. The first item of a line
never triggers a wrap (it prints even if it is wider than the terminal), and
the final name gets no trailing padding.

### Q&A

**Q1. Compare vertical (down-then-across) and horizontal printing complexity —
which needs more pre-calculation and why?**

The vertical layout needs more pre-calculation. To print even the first row
of a vertical layout you must know which item sits in *every* column of that
row (`idx = c*nrows + r`), so you have to read all names first, find the
longest one, and compute `ncols` and `nrows` before printing a single
character — the index mapping is two-dimensional. The horizontal layout needs
no such grid: items are printed in order and a line break is emitted whenever
the running position `pos + col_width` would exceed the terminal width, so
the decision for each item depends only on the cursor's current position, not
on the whole set. The tradeoff: vertical gives you the classic column look
but demands the full grid up front; horizontal is a simpler streaming pass.

**Q2. How are display modes managed — how does the program choose the print
function?**

The mode is held in a single variable of type `enum display_mode`, initialized
to `MODE_DEFAULT`. While `getopt()` parses the flags, `-l` assigns
`MODE_LONG` and `-x` assigns `MODE_HORIZONTAL` — a plain assignment, so when
both flags appear the last one processed wins. `main()` uses the mode for the
top-level choice (`do_ls_long()` vs `do_ls()`), and `do_ls()` dispatches to
the column printer with one `if`: `MODE_HORIZONTAL` →
`print_columns_across()`, anything else → `print_columns_down()`. The reading
logic (opendir/readdir/strdup) is shared by both layouts; only the printing
differs, so adding a mode means adding one enum value, one flag letter, and
one printer function.

## v1.4.0 — Alphabetical sort

All listings are now sorted alphabetically with `qsort()` before any display.
In `do_ls()`, the `char **` name array is sorted with a `cmp_name`
comparator (casts `const void *` to `const char * const *`, then `strcmp`); in
`do_ls_long()`, the `struct file_entry` array is sorted with a `cmp_entry`
comparator (by `name`) after reading, before the two-pass print. Memory is
freed as before — nothing leaks. Sorting happens in every mode (default,
`-x`, `-l`) and matches GNU `ls` byte order (uppercase before lowercase).
During testing a v1.2.0 bug surfaced: `print_columns_down()` skipped padding
on every column whenever the grid's last column was incomplete, so names
printed concatenated; fixed by treating an item as last-on-line only when
the *next* column has no entry at that row (`(c+1)*nrows + r >= n`).

### Q&A

**Q1. Why must all entries be read into memory before sorting? What are the
drawbacks for millions of files?**

Sorting needs random access to the whole set: an element's final position
depends on comparisons against *all* other elements, and `readdir()` yields
entries one at a time in arbitrary (filesystem) order. There is no way to
emit "the first sorted entry" until every entry has been seen, so the program
must collect everything (name + `lstat` data) into a dynamic array first and
then sort it. Drawbacks at scale: memory grows linearly with entry count
(millions of entries → hundreds of MB just for names and structs), and
`qsort()`'s O(n log n) comparisons become expensive — GNU `ls` on huge
directories has the same cost, which is why tools like `find` stream unsorted
when order doesn't matter.

**Q2. What is the purpose and signature of the `qsort` comparison function —
why `const void *`?**

`qsort()` is generic: it sorts an array of *any* element type, so it cannot
know the element type at compile time. It hands the comparator two
`const void *` pointers, each pointing at one array element; the comparator
casts them to the real element type and returns negative/zero/positive
according to their order. `void *` is C's way of saying "pointer to unknown
type", and `const` promises the comparator won't modify the elements. For the
name array, elements are `char *`, so the parameters are really
`const char * const *` (pointer to a constant pointer to constant char):
`return strcmp(*sa, *sb);`. For the long listing, elements are
`struct file_entry`, so we cast to `const struct file_entry *` and compare
`ea->name` vs `eb->name`.

## v1.5.0 — Colorized output

Names are wrapped in ANSI escape sequences when stdout is a terminal:
directories blue (`\033[0;34m`), executables green (`\033[0;32m`), archives
(`.tar`/`.gz`/`.zip`) red (`\033[0;31m`), symlinks pink (`\033[0;35m`),
special files (char/block/fifo/socket) reverse video (`\033[7m`), and every
colored name ends with a reset (`\033[0m`). The entry's type and
executability come from `lstat()` mode bits (not the name), with precedence:
symlink > directory > archive > executable > special > plain.
`main()` sets a global flag from `isatty(STDOUT_FILENO)`, so piped/redirected
output stays plain text with zero escape bytes. The column printers resolve
each name to a full path for `lstat()` and apply padding to the *visible*
name only, keeping columns aligned despite the zero-width codes; the long
listing reuses the already-stated entry array.

### Q&A

**Q1. How do ANSI escape sequences work? Give the green sequence.**

A terminal interprets certain byte sequences as commands instead of printable
text. A sequence starts with the Escape character (ASCII 27, `\033` in C),
followed by `[`, then parameter bytes, and ends with a letter that names the
command. `m` (Select Graphic Rendition) changes text rendering: `0` = normal
attributes, `1` = bold, `30–37` = foreground colors, `7` = reverse video.
So `\033[0;32m` means: escape, start sequence, parameters "0" (normal) and
"32" (green foreground), command `m` — everything printed after it appears
green until another `m` sequence changes it. That's why every colored name is
followed by `\033[0m` (parameters "0", reset all attributes): without the
reset, the color would bleed into the next filename. Sequences are *zero
width* — they move no cursor — which is why column padding must be computed
from the name length alone, with codes wrapped around it.

**Q2. Which `st_mode` bits determine executability?**

The three execute bits in `st_mode`: `S_IXUSR` (owner execute), `S_IXGRP`
(group execute), and `S_IXOTH` (other execute) — bitmask `0111` octal in the
permission field. A file counts as executable for coloring if *any* of the
three is set: `st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)`. For a regular file
this usually means it can be run as a program or script; for a directory the
same bits mean "search permission" (you can enter it), which is why
directories are colored blue by the file *type* check (`S_ISDIR`) instead of
green. Note the check is deliberately OR, not AND — an `rwxr--r--` script
with only the owner bit set is still green, just like GNU `ls`.
