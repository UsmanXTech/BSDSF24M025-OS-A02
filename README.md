# BSDSF24M025-OS-A02 — Programming Assignment 02

Re-engineering the Unix `ls` utility in C, built feature by feature.

## Build

```sh
make          # builds bin/ls
make clean    # removes bin/ and obj/
```

## Usage

```sh
./bin/ls [dir...]        # v1.0.0: basic listing
./bin/ls -l [dir...]     # v1.1.0: long listing format
./bin/ls -x [dir...]     # v1.3.0: horizontal display
./bin/ls -R [dir...]     # v1.6.0: recursive listing
```

## Versions

| Version | Feature |
|---------|---------|
| v1.0.0 | Basic directory listing |
| v1.1.0 | Long listing format (`-l`) |
| v1.2.0 | Column display, down-then-across |
| v1.3.0 | Horizontal display (`-x`) |
| v1.4.0 | Alphabetical sorting (`qsort`) |
| v1.5.0 | Colorized output (ANSI) |
| v1.6.0 | Recursive listing (`-R`) |

See `REPORT.md` for the analysis answers (viva preparation) for each feature.
Each version is tagged (`v1.0.0` … `v1.6.0`) and has a GitHub Release with the compiled binary.
