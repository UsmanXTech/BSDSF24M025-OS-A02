# BSDSF24M025-OS-A02 — Programming Assignment 02

Re-engineering the Unix `ls` utility in C, built feature by feature (v1.0.0 → v1.6.0).

## Prerequisites

- `gcc` (C11 or newer)
- `make`
- A Unix-like system (Linux/macOS) — uses `ioctl()`, `lstat()`, `readlink()`

Check:

```sh
gcc --version
make --version
```

## Installation

```sh
# 1. Clone the repository
git clone https://github.com/UsmanXTech/BSDSF24M025-OS-A02.git
cd BSDSF24M025-OS-A02

# 2. Build
make            # compiles src/ls-v1.0.0.c -> bin/ls

# 3. Run
./bin/ls
```

Or download the pre-built binary from any
[GitHub Release](https://github.com/UsmanXTech/BSDSF24M025-OS-A02/releases),
then:

```sh
chmod +x ls
./ls
```

To rebuild from scratch:

```sh
make clean && make
```

## Usage

```sh
./bin/ls [options] [dir...]
```

| Option | Meaning |
|--------|---------|
| *(none)* | Column listing, down-then-across |
| `-l` | Long listing format (permissions, owner, size, time) |
| `-x` | Horizontal listing (across, then wrap) |
| `-R` | Recursive listing of subdirectories |

Options combine, e.g. `./bin/ls -lR /etc`. If both `-l` and `-x` are given,
the last one wins.

Examples:

```sh
./bin/ls                 # list current directory in columns
./bin/ls -l /tmp         # long listing of /tmp
./bin/ls -x ~            # horizontal listing of home
./bin/ls -R src          # recursive listing of src/
./bin/ls -lR .           # recursive long listing
```

## Project structure

```
.
├── src/
│   └── ls-v1.0.0.c     # all source code (single file, extended per version)
├── bin/
│   └── ls              # compiled binary (built by make)
├── obj/
│   └── ls-v1.0.0.o     # object file (built by make)
├── man/                # man page directory (reserved)
├── Makefile            # build rules
├── README.md           # this file
└── REPORT.md           # analysis answers for each feature (viva prep)
```

## Versions

| Version | Feature | Marks |
|---------|---------|-------|
| v1.0.0 | Basic directory listing | 5 |
| v1.1.0 | Long listing format (`-l`) | 15 |
| v1.2.0 | Column display, down-then-across | 15 |
| v1.3.0 | Horizontal display (`-x`) | 15 |
| v1.4.0 | Alphabetical sorting (`qsort`) | 15 |
| v1.5.0 | Colorized output (ANSI, tty only) | 10 |
| v1.6.0 | Recursive listing (`-R`) | 20 |

Each version lives on its own branch (`feature-<name>-vX.Y.Z`), is tagged
(`v1.0.0` … `v1.6.0`), and has a GitHub Release with the compiled binary.

See `REPORT.md` for the analysis questions and answers for each feature —
written as viva preparation.
