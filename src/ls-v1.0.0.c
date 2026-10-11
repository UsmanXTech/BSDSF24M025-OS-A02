#define _DEFAULT_SOURCE

/*
 * Programming Assignment 02: ls v1.4.0 — alphabetical sort
 *
 * Extends v1.1.0 with the -l option:
 *   -lstat() (not stat()) so symlinks are listed as links, not followed
 *   two-pass: collect entries into a dynamic array (realloc) measuring
 *   max column widths, then print aligned
 * v1.2.0: default (non -l) path prints names in columns, top-to-bottom
 *   then left-to-right, sized from the terminal width via ioctl().
 * v1.3.0: -x prints names in horizontal columns (left-to-right across each
 *   row) instead; -l still selects the long listing. When both -l and -x
 *   are given, the LAST flag wins.
 * v1.4.0: all listings are sorted alphabetically with qsort() before any
 *   display (default, -x, and -l).
 * v1.5.0: colorized output — directories blue, executables green, archives
 *   red, symlinks pink, special files reverse video; only when stdout is
 *   a terminal (isatty()), with a reset code after every colored name.
 * v1.6.0: -R descends into subdirectories recursively, printing a
 *   "dir:" header before each directory's listing; combines with -l and -x.
 *
 * Usage:
 *       $ ./ls
 *       $ ./ls -l
 *       $ ./ls -x
 *       $ ./ls -R
 *       $ ./ls -lR /home /etc
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>

/*
 * v1.5.0 — ANSI color codes. Color is only emitted when stdout is a
 * terminal; see g_color_enabled (set in main() from isatty()). The reset
 * code is printed after every colored name so colors never bleed into
 * the next field.
 */
#define COL_RESET   "\033[0m"
#define COL_DIR     "\033[0;34m"
#define COL_EXEC    "\033[0;32m"
#define COL_ARCHIVE "\033[0;31m"
#define COL_SYMLINK "\033[0;35m"
#define COL_SPECIAL "\033[7m"

/* 1 when stdout is a tty and colors may be emitted, else 0. */
static int g_color_enabled = 0;

/* Display modes: the last of -l / -x on the command line wins. */
enum display_mode { MODE_DEFAULT, MODE_LONG, MODE_HORIZONTAL };

/* 1 when -R was given: descend into subdirectories (independent of mode). */
static int g_recursive = 0;

void do_ls(const char *dir, enum display_mode mode);
void do_ls_long(const char *dir);
void do_ls_recursive(const char *dir, enum display_mode mode);

/*
 * term_width: terminal width via ioctl(STDOUT_FILENO, TIOCGWINSZ).
 * Returns 80 when stdout is not a terminal or the call fails.
 */
static int term_width(void)
{
    int width = 80;
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col != 0)
        width = ws.ws_col;
    return width;
}

/* One collected entry for the two-pass long listing */
struct file_entry {
    char *name;        /* strdup'd entry name              */
    struct stat st;    /* lstat() result for the entry     */
};

/*
 * cmp_name: qsort comparator for an array of (char *) names.
 * qsort passes pointers to the array elements; each element is a
 * (char *), so we cast to pointer-to-(const char *) and strcmp.
 */
static int cmp_name(const void *a, const void *b)
{
    const char * const *sa = a;
    const char * const *sb = b;
    return strcmp(*sa, *sb);
}

/*
 * cmp_entry: qsort comparator for an array of struct file_entry,
 * ordering entries by name for the sorted long listing.
 */
static int cmp_entry(const void *a, const void *b)
{
    const struct file_entry *ea = a;
    const struct file_entry *eb = b;
    return strcmp(ea->name, eb->name);
}

/*
 * v1.5.0 — color helpers.
 *
 * has_archive_ext: true when the name ends in a known archive suffix
 * (.tar, .gz, .zip).
 */
static int has_archive_ext(const char *name)
{
    static const char *exts[] = { ".tar", ".gz", ".zip" };
    size_t nlen = strlen(name);
    for (size_t i = 0; i < sizeof exts / sizeof *exts; i++) {
        size_t elen = strlen(exts[i]);
        if (nlen > elen && strcmp(name + nlen - elen, exts[i]) == 0)
            return 1;
    }
    return 0;
}

/*
 * color_code: pick the ANSI color for an entry from its lstat() mode and
 * name. Precedence: symlink > directory > archive > executable > special
 * file > plain (no color). Returns the code string, or "" when no color
 * applies. Callers print COL_RESET after the name whenever this is
 * non-empty.
 */
static const char *color_code(const struct stat *st, const char *name)
{
    if (S_ISLNK(st->st_mode))
        return COL_SYMLINK;
    if (S_ISDIR(st->st_mode))
        return COL_DIR;
    if (has_archive_ext(name))
        return COL_ARCHIVE;
    if (st->st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))
        return COL_EXEC;
    if (S_ISCHR(st->st_mode) || S_ISBLK(st->st_mode) ||
        S_ISFIFO(st->st_mode) || S_ISSOCK(st->st_mode))
        return COL_SPECIAL;
    return "";
}

/*
 * color_code_path: lstat() dir/name and return its color code. The column
 * printers only have plain names, so they resolve the full path here to
 * get the mode bits. Returns "" when the entry can't be stated.
 */
static const char *color_code_path(const char *dir, const char *name)
{
    char path[4096];
    struct stat st;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (lstat(path, &st) != 0)
        return "";
    return color_code(&st, name);
}

/*
 * print_name_colored: print name wrapped in its color code and COL_RESET.
 * When colors are disabled or the entry has no color, prints the name
 * alone. Padding is applied to the name only (codes are zero-width), so
 * columns stay aligned. pad = 0 means no padding.
 */
static void print_name_colored(const char *code, const char *name, int pad)
{
    if (!g_color_enabled || code[0] == '\0') {
        if (pad > 0)
            printf("%-*s", pad, name);
        else
            printf("%s", name);
    } else if (pad > 0) {
        printf("%s%-*s%s", code, pad, name, COL_RESET);
    } else {
        printf("%s%s%s", code, name, COL_RESET);
    }
}

/*
 * mode_string: build the 10-char permission string from st_mode.
 *   [0]  file type: d (dir), l (symlink), - (regular), c (char),
 *        b (block), p (fifo), s (socket)
 *   [1-9] rwx triples; setuid/setgid/sticky replace the x position:
 *        s/S (setuid/setgid), t/T (sticky) — lowercase when x is also set.
 */
static void mode_string(mode_t mode, char out[11])
{
    if (S_ISDIR(mode))      out[0] = 'd';
    else if (S_ISLNK(mode)) out[0] = 'l';
    else if (S_ISCHR(mode)) out[0] = 'c';
    else if (S_ISBLK(mode)) out[0] = 'b';
    else if (S_ISFIFO(mode))out[0] = 'p';
    else if (S_ISSOCK(mode))out[0] = 's';
    else                   out[0] = '-';

    out[1] = (mode & S_IRUSR) ? 'r' : '-';
    out[2] = (mode & S_IWUSR) ? 'w' : '-';
    if (mode & S_ISUID)
        out[3] = (mode & S_IXUSR) ? 's' : 'S';
    else
        out[3] = (mode & S_IXUSR) ? 'x' : '-';

    out[4] = (mode & S_IRGRP) ? 'r' : '-';
    out[5] = (mode & S_IWGRP) ? 'w' : '-';
    if (mode & S_ISGID)
        out[6] = (mode & S_IXGRP) ? 's' : 'S';
    else
        out[6] = (mode & S_IXGRP) ? 'x' : '-';

    out[7] = (mode & S_IROTH) ? 'r' : '-';
    out[8] = (mode & S_IWOTH) ? 'w' : '-';
    if (mode & S_ISVTX)
        out[9] = (mode & S_IXOTH) ? 't' : 'T';
    else
        out[9] = (mode & S_IXOTH) ? 'x' : '-';

    out[10] = '\0';
}

/*
 * time_string: GNU-ls style timestamp.
 *   Recent files (within ~6 months): "Mon DD HH:MM"
 *   Older files:                     "Mon DD  YYYY" (two spaces)
 */
static void time_string(time_t mtime, char *out, size_t n)
{
    time_t now = time(NULL);
    struct tm *lt = localtime(&mtime);
    const char *fmt;

    if (now - mtime > 182L * 24 * 3600 || mtime > now + 3600)
        fmt = "%b %e  %Y";   /* older than ~6 months, or in the future */
    else
        fmt = "%b %e %H:%M"; /* recent */
    strftime(out, n, fmt, lt);
}

int main(int argc, char *argv[])
{
    enum display_mode mode = MODE_DEFAULT;
    int opt;

    /* v1.5.0: only emit colors when stdout is a real terminal — piping
     * ls to cat/grep must produce clean, code-free text. */
    g_color_enabled = isatty(STDOUT_FILENO);

    while ((opt = getopt(argc, argv, "lxR")) != -1) {
        if (opt == 'l')
            mode = MODE_LONG;        /* last flag wins */
        else if (opt == 'x')
            mode = MODE_HORIZONTAL;  /* last flag wins */
        else if (opt == 'R')
            g_recursive = 1;         /* separate flag, combines with any mode */
        else {
            fprintf(stderr, "Usage: %s [-l] [-x] [-R] [dir ...]\n", argv[0]);
            return 2;
        }
    }

    if (optind == argc) {
        if (g_recursive)
            do_ls_recursive(".", mode);
        else if (mode == MODE_LONG)
            do_ls_long(".");
        else
            do_ls(".", mode);
    } else {
        for (int i = optind; i < argc; i++) {
            if (g_recursive) {
                /* the recursive function prints its own "dir:" headers */
                if (i > optind)
                    putchar('\n');   /* separator between operands, like GNU */
                do_ls_recursive(argv[i], mode);
            } else {
                printf("Directory listing of %s : \n", argv[i]);
                if (mode == MODE_LONG)
                    do_ls_long(argv[i]);
                else
                    do_ls(argv[i], mode);
                puts("");
            }
        }
    }
    return 0;
}

/*
 * do_ls_recursive: v1.6.0 — list dir with the existing logic, then descend
 * into every subdirectory (sorted, so output order is stable).
 *
 *   1. Print the "dir:" header, then the listing itself — do_ls() /
 *      do_ls_long() are reused unchanged, so -l and -x combine naturally.
 *   2. Re-scan the directory, collect subdirectories into a dynamic
 *      array (lstat(), NOT stat(), so a symlink pointing at a directory
 *      is a link, not a descent — no symlink cycles), skip "." and ".."
 *      plus hidden entries (the listings skip them too), sort, recurse.
 */
void do_ls_recursive(const char *dir, enum display_mode mode)
{
    printf("%s:\n", dir);
    if (mode == MODE_LONG)
        do_ls_long(dir);
    else
        do_ls(dir, mode);

    DIR *dp = opendir(dir);
    if (dp == NULL)
        return;   /* do_ls/do_ls_long already reported the error */

    char **subdirs = NULL;
    size_t cap = 0, n = 0;

    errno = 0;
    struct dirent *entry;
    while ((entry = readdir(dp)) != NULL) {
        /* base case: never descend into "." or "..", or we would
         * recurse forever — they always resolve back to an ancestor. */
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;
        if (entry->d_name[0] == '.')
            continue;   /* hidden entries are not listed, so not recursed */

        char path[4096];
        snprintf(path, sizeof path, "%s/%s", dir, entry->d_name);
        struct stat st;
        if (lstat(path, &st) != 0)
            continue;
        if (!S_ISDIR(st.st_mode))
            continue;

        if (n == cap) {
            cap = cap ? cap * 2 : 16;
            char **tmp = realloc(subdirs, cap * sizeof *tmp);
            if (tmp == NULL) {
                perror("realloc");
                break;
            }
            subdirs = tmp;
        }
        subdirs[n] = strdup(path);
        if (subdirs[n] == NULL) {
            perror("strdup");
            break;
        }
        n++;
    }
    if (errno != 0)
        perror("readdir failed");
    closedir(dp);

    qsort(subdirs, n, sizeof *subdirs, cmp_name);

    for (size_t i = 0; i < n; i++) {
        putchar('\n');
        do_ls_recursive(subdirs[i], mode);
        free(subdirs[i]);
    }
    free(subdirs);
}

/*
 * do_ls: read every visible name into a dynamic array (strdup + realloc),
 * tracking the longest name, then dispatch to the chosen printer:
 *   MODE_DEFAULT      -> print_columns_down()    (top-to-bottom, like GNU ls)
 *   MODE_HORIZONTAL   -> print_columns_across()  (left-to-right, like ls -x)
 */
/*
 * print_columns_down: v1.2.0 layout — columns fill top-to-bottom first,
 * then left-to-right. Row r of column c holds entry c*nrows + r.
 * v1.5.0: names are printed through print_name_colored() (needs dir to
 * lstat each entry); padding still applies to the visible name only.
 */
static void print_columns_down(char **names, size_t n, size_t maxlen,
                               const char *dir)
{
    int width = term_width();

    size_t col_width = maxlen + 2;
    size_t ncols = (size_t)width / col_width;
    if (ncols == 0)
        ncols = 1;
    size_t nrows = (n + ncols - 1) / ncols;

    for (size_t r = 0; r < nrows; r++) {
        for (size_t c = 0; c < ncols; c++) {
            size_t idx = c * nrows + r;
            if (idx >= n)
                break;   /* idx only grows with c: no more entries in this row */
            size_t next_idx = (c + 1) * nrows + r;
            const char *code = color_code_path(dir, names[idx]);
            if (next_idx >= n)
                print_name_colored(code, names[idx], 0); /* last on line: no padding */
            else
                print_name_colored(code, names[idx], (int)col_width);
        }
        putchar('\n');
    }
}

/*
 * print_columns_across: v1.3.0 layout — entries flow left-to-right along
 * each row (like GNU ls -x). Single pass: keep track of the current cursor
 * position; when the next padded item would overrun the terminal width,
 * start a new line. The line-start item never wraps, even if it alone is
 * wider than the terminal. The final name gets no trailing padding.
 * v1.5.0: colors applied the same way as print_columns_down().
 */
static void print_columns_across(char **names, size_t n, size_t maxlen,
                                 const char *dir)
{
    int width = term_width();
    size_t col_width = maxlen + 2;
    size_t pos = 0;   /* characters consumed on the current line */

    for (size_t i = 0; i < n; i++) {
        if (pos > 0 && pos + col_width > (size_t)width) {
            putchar('\n');
            pos = 0;
        }
        const char *code = color_code_path(dir, names[i]);
        if (i == n - 1)
            print_name_colored(code, names[i], 0);      /* last item: no padding */
        else
            print_name_colored(code, names[i], (int)col_width);
        pos += col_width;
    }
    putchar('\n');
}

void do_ls(const char *dir, enum display_mode mode)
{
    DIR *dp = opendir(dir);
    if (dp == NULL)
    {
        fprintf(stderr, "Cannot open directory : %s\n", dir);
        return;
    }

    char **names = NULL;
    size_t cap = 0, n = 0;
    size_t maxlen = 0;

    errno = 0;
    struct dirent *entry;
    while ((entry = readdir(dp)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 32;
            char **tmp = realloc(names, cap * sizeof *tmp);
            if (tmp == NULL) {
                perror("realloc");
                break;
            }
            names = tmp;
        }
        names[n] = strdup(entry->d_name);
        if (names[n] == NULL) {
            perror("strdup");
            break;
        }
        size_t len = strlen(names[n]);
        if (len > maxlen)
            maxlen = len;
        n++;
    }

    if (errno != 0)
    {
        perror("readdir failed");
    }

    closedir(dp);

    if (n == 0) {
        free(names);
        return;
    }

    /* v1.4.0: sort alphabetically before any display (default and -x) */
    qsort(names, n, sizeof *names, cmp_name);

    if (mode == MODE_HORIZONTAL)
        print_columns_across(names, n, maxlen, dir);
    else
        print_columns_down(names, n, maxlen, dir);

    for (size_t i = 0; i < n; i++)
        free(names[i]);
    free(names);
}

/*
 * do_ls_long: two-pass long listing.
 *   Pass 1 — readdir() + lstat() every entry into a dynamic array,
 *            tracking max widths of nlink/size/owner/group columns.
 *   Pass 2 — print the aligned table, symlink targets via readlink().
 */
void do_ls_long(const char *dir)
{
    DIR *dp = opendir(dir);
    if (dp == NULL) {
        fprintf(stderr, "Cannot open directory : %s\n", dir);
        return;
    }

    struct file_entry *arr = NULL;
    size_t cap = 0, n = 0;
    size_t w_nlink = 0, w_size = 0, w_user = 0, w_group = 0;
    long total = 0;   /* 1K blocks, like GNU ls */

    errno = 0;
    struct dirent *entry;
    while ((entry = readdir(dp)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;

        if (n == cap) {
            cap = cap ? cap * 2 : 32;
            struct file_entry *tmp = realloc(arr, cap * sizeof *tmp);
            if (tmp == NULL) {
                perror("realloc");
                break;
            }
            arr = tmp;
        }

        char path[4096];
        snprintf(path, sizeof path, "%s/%s", dir, entry->d_name);
        if (lstat(path, &arr[n].st) != 0) {
            fprintf(stderr, "lstat failed for %s: %s\n", path, strerror(errno));
            continue;
        }
        arr[n].name = strdup(entry->d_name);
        if (arr[n].name == NULL) {
            perror("strdup");
            break;
        }

        total += arr[n].st.st_blocks / 2;   /* 512-byte units -> 1K blocks */

        /* measure column widths for pass 2 */
        char buf[64];
        int len = snprintf(buf, sizeof buf, "%lu", (unsigned long)arr[n].st.st_nlink);
        if ((size_t)len > w_nlink) w_nlink = (size_t)len;
        len = snprintf(buf, sizeof buf, "%lld", (long long)arr[n].st.st_size);
        if ((size_t)len > w_size) w_size = (size_t)len;
        struct passwd *pw = getpwuid(arr[n].st.st_uid);
        size_t ulen = pw ? strlen(pw->pw_name) : 1;
        if (ulen > w_user) w_user = ulen;
        struct group *gr = getgrgid(arr[n].st.st_gid);
        size_t glen = gr ? strlen(gr->gr_name) : 1;
        if (glen > w_group) w_group = glen;

        n++;
    }
    if (errno != 0)
        perror("readdir failed");
    closedir(dp);

    /* v1.4.0: sort the collected entries by name so -l is sorted too */
    qsort(arr, n, sizeof *arr, cmp_entry);

    printf("total %ld\n", total);

    for (size_t i = 0; i < n; i++) {
        char mstr[11];
        mode_string(arr[i].st.st_mode, mstr);

        struct passwd *pw = getpwuid(arr[i].st.st_uid);
        struct group *gr = getgrgid(arr[i].st.st_gid);

        char tstr[32];
        time_string(arr[i].st.st_mtime, tstr, sizeof tstr);

        printf("%s %*lu %-*s %-*s %*lld %s ",
               mstr,
               (int)w_nlink, (unsigned long)arr[i].st.st_nlink,
               (int)w_user, pw ? pw->pw_name : "?",
               (int)w_group, gr ? gr->gr_name : "?",
               (int)w_size, (long long)arr[i].st.st_size,
               tstr);

        /* v1.5.0: colorize the name; reset before " -> target" so the
         * target path stays uncolored. */
        print_name_colored(color_code(&arr[i].st, arr[i].name),
                           arr[i].name, 0);

        if (S_ISLNK(arr[i].st.st_mode)) {
            char path[4096], target[1024];
            snprintf(path, sizeof path, "%s/%s", dir, arr[i].name);
            ssize_t tlen = readlink(path, target, sizeof target - 1);
            if (tlen >= 0) {
                target[tlen] = '\0';
                printf(" -> %s", target);
            }
        }
        putchar('\n');
    }

    for (size_t i = 0; i < n; i++)
        free(arr[i].name);
    free(arr);
}
