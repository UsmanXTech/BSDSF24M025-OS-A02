#define _DEFAULT_SOURCE

/*
 * Programming Assignment 02: ls v1.1.0 — long listing (-l)
 *
 * Extends v1.0.0 with the -l option:
 *   -lstat() (not stat()) so symlinks are listed as links, not followed
 *   two-pass: collect entries into a dynamic array (realloc) measuring
 *   max column widths, then print aligned
 *
 * Usage:
 *       $ ./ls
 *       $ ./ls -l
 *       $ ./ls -l /home /etc
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

void do_ls(const char *dir);
void do_ls_long(const char *dir);

/* One collected entry for the two-pass long listing */
struct file_entry {
    char *name;        /* strdup'd entry name              */
    struct stat st;    /* lstat() result for the entry     */
};

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
    int long_flag = 0;
    int opt;

    while ((opt = getopt(argc, argv, "l")) != -1) {
        if (opt == 'l') {
            long_flag = 1;
        } else {
            fprintf(stderr, "Usage: %s [-l] [dir ...]\n", argv[0]);
            return 2;
        }
    }

    if (optind == argc) {
        if (long_flag)
            do_ls_long(".");
        else
            do_ls(".");
    } else {
        for (int i = optind; i < argc; i++) {
            printf("Directory listing of %s : \n", argv[i]);
            if (long_flag)
                do_ls_long(argv[i]);
            else
                do_ls(argv[i]);
            puts("");
        }
    }
    return 0;
}

/* v1.0.0: one name per line, dotfiles skipped */
void do_ls(const char *dir)
{
    struct dirent *entry;
    DIR *dp = opendir(dir);
    if (dp == NULL)
    {
        fprintf(stderr, "Cannot open directory : %s\n", dir);
        return;
    }
    errno = 0;
    while ((entry = readdir(dp)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;
        printf("%s\n", entry->d_name);
    }

    if (errno != 0)
    {
        perror("readdir failed");
    }

    closedir(dp);
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

    printf("total %ld\n", total);

    for (size_t i = 0; i < n; i++) {
        char mstr[11];
        mode_string(arr[i].st.st_mode, mstr);

        struct passwd *pw = getpwuid(arr[i].st.st_uid);
        struct group *gr = getgrgid(arr[i].st.st_gid);

        char tstr[32];
        time_string(arr[i].st.st_mtime, tstr, sizeof tstr);

        printf("%s %*lu %-*s %-*s %*lld %s %s",
               mstr,
               (int)w_nlink, (unsigned long)arr[i].st.st_nlink,
               (int)w_user, pw ? pw->pw_name : "?",
               (int)w_group, gr ? gr->gr_name : "?",
               (int)w_size, (long long)arr[i].st.st_size,
               tstr,
               arr[i].name);

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
