/*
 * Lazy File System
 *
 * Single directory support. Files ending in .mhash and .mbinmap are stored on
 * a real filesystem (operations are forwarded). Other files are not stored
 * anywhere and reads just return a preset value (a pattern), while writes do
 * nothing (except changing the size). Name these normal files as:
 * deadbeef_size_chunksize (where deadbeef is the pattern). The pattern, the
 * size and the chunksize uniquelly identify a libswift roothash and other
 * metadata (which can be precomputed).
 * 
 * Usage: ./lfs -o [fuse options],realstore=PATH <mountpoint>
 */

#define FUSE_USE_VERSION 26 /* new API */
#include <fuse.h>

#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <libgen.h>
#include <string.h>
#include <stddef.h>
#include <stdarg.h>

#include "uthash.h"

#define MAXPATHLEN 50
#define MAXREALPATHLEN 256
#define MAXMETAPATHLEN 32

#define VERSION "0.1 beta"

struct l_file { 
    char path[MAXPATHLEN];
    off_t size;
    int fd;
    int realfd; /* if stored on real fs */
    char pattern[4];
    UT_hash_handle hh;
};

struct l_state {
    struct l_file *files; /* hash table holding l_file structs */
    char *metadir;
    unsigned nfiles;
    void *log_file; /* first a string, then a FILE* */
};

/* Global var holding FS configuration. */
struct l_state l_data;

#define l_log(...) do { \
                    if (l_data.log_file) { \
                     fprintf(l_data.log_file, __VA_ARGS__); \
                     fflush(l_data.log_file); \
                    } \
                   } while (0)

static inline char *gnu_basename(char *path)
{
    char *base = strrchr(path, '/');
    return base ? base+1 : path;
}

static inline unsigned int is_meta_file(const char *path)
{
    size_t l = strlen(path);
    unsigned int r = 0;
    
    if (l >= 6) {
        r |= (strcmp(path + l - 6, ".mhash") == 0);
    }
    
    if (l >= 8) {
        r |= (strcmp(path + l - 8, ".mbinmap") == 0);
    }

    return r;
}

static inline int parse_pattern(const char *s, char *array)
{
    if (strlen(s) < 8) {
        return -1;
    }

    int i;
    for (i = 0; i < 8; i += 2) {
        if (sscanf(&s[i], "%2hhx", &array[i / 2]) != 1) {
            return -1;
        }
    }
    
    return 0;
}

/*
 * Predefined attributes - we don't care about most of these.
 */
int l_getattr(const char *path, struct stat *stbuf)
{
    struct l_file *file;

    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;

        return 0;
    }

    /* find it */
    HASH_FIND_STR(l_data.files, path, file);
    if (file == NULL) {
        return -ENOENT;
    }

    if (is_meta_file(path)) {
        /* delegate to real fs */
        char *pathcopy = strdup(path);
        char *bn = gnu_basename(pathcopy);
        char realpath[MAXREALPATHLEN];
        snprintf(realpath, MAXREALPATHLEN, "%s/%s", l_data.metadir, bn);
        int r = stat(realpath, stbuf);
        free(pathcopy);

        return r;
    } else {
        time_t now = time(NULL);

        stbuf->st_dev = 0;
        stbuf->st_ino = 0;
        stbuf->st_mode = S_IFREG | S_IRWXU | S_IRWXG | S_IRWXO;
        stbuf->st_nlink = 1;
        stbuf->st_uid = getuid();
        stbuf->st_gid = getgid();
        stbuf->st_rdev = 0;
        stbuf->st_blksize = 512;
        stbuf->st_atime = now;
        stbuf->st_mtime = now;
        stbuf->st_ctime = now;
        stbuf->st_size = file->size;
        stbuf->st_blocks = stbuf->st_size / 512;
        
        return 0;
    }
}

/*
 * Removes the file.
 */
int l_unlink(const char *path)
{
    struct l_file *file;
    int r = 0;

    /* find it */
    HASH_FIND_STR(l_data.files, path, file);
    if (file == NULL) {
        return -ENOENT;
    }

    /* remove meta files from real storage */
    if (is_meta_file(path)) {
        char *pathcopy = strdup(path);
        char *bn = gnu_basename(pathcopy);
        char realpath[MAXREALPATHLEN];
        snprintf(realpath, MAXREALPATHLEN, "%s/%s", l_data.metadir, bn);
        r = unlink(realpath);
        free(pathcopy);
    }

    HASH_DEL(l_data.files, file);
    free(file);

    return r; /* r is always 0 when file is not meta */
}

/*
 * Changes file size.
 */
int l_truncate(const char *path, off_t length)
{
    struct l_file *file;
    
    /* find it */
    HASH_FIND_STR(l_data.files, path, file);
    if (file == NULL) {
        return -ENOENT;
    }
    
    if (is_meta_file(path)) {
        /* delegate to real fs */
        char *pathcopy = strdup(path);
        char *bn = gnu_basename(pathcopy);
        char realpath[MAXREALPATHLEN];
        snprintf(realpath, MAXREALPATHLEN, "%s/%s", l_data.metadir, bn);
        int r = truncate(realpath, length);
        free(pathcopy);

        return r;
    } else {
        file->size = length;

        return 0;
    }
}

/*
 * Opens a file.
 *
 * O_CREAT and O_EXCL are guaranteed to not be passed to this. The file will
 * exist when this is called. O_TRUNC might be present when atomic_o_trunc is
 * specified on a kernel version of 2.6.24 or later.
 */
int l_open(const char *path, struct fuse_file_info *fi)
{
    struct l_file *file;

    l_log("opening file %s ", path);

    /* find it */
    HASH_FIND_STR(l_data.files, path, file);
    if (file == NULL) {
        return -ENOENT;
    }

    l_log("(file exists)\n");
    
    if (is_meta_file(path)) {
        /* delegate to real fs */
        char *pathcopy = strdup(path);
        char *bn = gnu_basename(pathcopy);
        char realpath[MAXREALPATHLEN];
        snprintf(realpath, MAXREALPATHLEN, "%s/%s", l_data.metadir, bn);

        int fd;
        if ((fd = open(realpath, O_RDWR)) == -1) {
            return -errno;
        }
        file->realfd = fd;
        free(pathcopy);

        return 0;
    } else {
        if (fi->flags & O_TRUNC)
            file->size = 0;

        /* This is it. We don't care about access rights. */
        return 0;
    }
}

/*
 * Reads file.
 *
 * Iteratively returns a byte from a 4-byte preset pattern.
 *
 * TODO(vladum): Handle direct_io.
 */
int l_read(const char *path, char *buf, size_t size, off_t offset,
    struct fuse_file_info *fi)
{
    struct l_file *file;
    
    /* find it */
    HASH_FIND_STR(l_data.files, path, file);
    if (file == NULL) {
        return -ENOENT;
    }

    if (is_meta_file(path)) {
        /* delegate to real fs */
        if (offset != lseek(file->realfd, 0, SEEK_CUR)) {
            lseek(file->realfd, offset, SEEK_SET);
        }
        return read(file->realfd, buf, size);
    } else {
        int i, j;
        for (i = 0; i < size; i++) {
            if (offset + i >= file->size) {
                /* Fill remaining buffer with 0s. */
                for (j = i; j < size; j++)
                    buf[j] = 0x00;

                /* Return bytes read so far. */
                return i;
            } else {
                buf[i] = file->pattern[(offset + i) % 4];
            }
        }
    }

    /* EOF was not encountered. */
    return size;
}

/*
 * Write.
 * 
 * All writes, except the ones on .mhash and .mbinmap, are ignored (only the
 * size is changed).
 */
int l_write(const char *path, const char *buf, size_t size, off_t offset,
    struct fuse_file_info *fi)
{
    struct l_file *file;
    //l_log("%ld bytes at %ld ", size, offset);
    /* find it */
    HASH_FIND_STR(l_data.files, path, file);
    if (file == NULL) {
        return -ENOENT;
    }
    
    //l_log("(file was found)\n");

    if (is_meta_file(path)) {
        /* delegate to real fs */
        if (offset != lseek(file->realfd, 0, SEEK_CUR)) {
            lseek(file->realfd, offset, SEEK_SET);
        }
        int r = write(file->realfd, buf, size);

        return r;
    } else {
        if (file->size < offset + size) {
            file->size = offset + size;
        }

        return size;
    }
}

int l_flush(const char *path, struct fuse_file_info *fi)
{
    /* Nothing to flush, so this always succeeds. */
    return 0;
}

/*
 * Release.
 */
int l_release(const char *path, struct fuse_file_info *fi)
{
    struct l_file *file;

    /* find it */
    HASH_FIND_STR(l_data.files, path, file);
    if (file == NULL) {
        return -ENOENT;
    }
    
    if (is_meta_file(path)) {
        /* delegate to real fs */
        return close(file->realfd);
    }
     
    /* Nothing to do for release() and the return value is ignored. */
    return 0;
}

int l_getxattr(const char *path, const char *name, char *value, size_t size)
{
    return -ENODATA;
}

int l_opendir(const char *path, struct fuse_file_info *fi)
{
    /* We only have one dir - the root. */
    if (strcmp(path, "/") == 0)
        return 0;
    else
        return -ENOENT;
}

int l_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
    struct fuse_file_info *fi)
{
    if (strcmp(path, "/") != 0)
        return -ENOENT;

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    struct l_file *files = l_data.files, *f, *tmp;
    HASH_ITER(hh, files, f, tmp) {
        filler(buf, f->path + 1, NULL, 0);
    }

    return 0;
}

void l_destroy(void *userdata)
{
    struct l_file *files = ((struct l_state *)userdata)->files;
    struct l_file *f, *tmp;

    /* Remove all files in the hashtable. */
    HASH_ITER(hh, files, f, tmp) {
        if (f->realfd != -1) {
            close(f->realfd);
        }
        HASH_DEL(files, f);
        free(f);
    }
}

int l_access(const char *path, int mode)
{
    /* We trust everybody. */
    return 0;
}

/*
 * Creates a file.
 */
int l_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    struct l_file *file;

    l_log("creating file %s ", path);

    /* find it */
    HASH_FIND_STR(l_data.files, path, file);
    if (file != NULL) {
        if ((fi->flags & O_CREAT) && (fi->flags & O_EXCL)) {
            /* File already exists. */
            return -EEXIST;
        }
    }

    l_log("(file didn't exist)\n");
    
    if (file == NULL) {
        file = (struct l_file *)malloc(sizeof(*file));
        file->size = 0;
        strcpy(file->path, path);

        if (is_meta_file(path)) {
            /* delegate to real fs */
            char *pathcopy = strdup(path);
            char *bn = gnu_basename(pathcopy);
            char realpath[MAXREALPATHLEN];
            snprintf(realpath, MAXREALPATHLEN, "%s/%s", l_data.metadir, bn);
            free(pathcopy);
            int fd;
            l_log("realpath: %s\n", realpath);
            if ((fd = open(realpath, O_CREAT | O_RDWR | O_TRUNC, mode)) != -1) {
                HASH_ADD_STR(l_data.files, path, file);
                file->realfd = fd;
                return fi->fh;
            } else {
                l_log("%s\n", strerror(errno));
                return -errno;
            }
        } else {
            HASH_ADD_STR(l_data.files, path, file);

            file->fd = ++(l_data.nfiles);
            char pattern[9];
            strncpy(pattern, path + 1, 8);
            pattern[8] = 0;
            /* TODO(vladum): Check error code. */
            parse_pattern(pattern, file->pattern);
        }
    }

    /* Reset size. */
    file->size = 0;

    return fi->fh;
}

int l_rename(const char *old, const char *new)
{
    struct l_file *file;

    l_log("rename old: %s new: %s", old, new);

    /* find new one */
    HASH_FIND_STR(l_data.files, new, file);
    if (file != NULL) {
        return -EEXIST;
    }

    /* find old one */
    HASH_FIND_STR(l_data.files, old, file);
    if (file == NULL) {
        return -ENOENT;
    }


    if ((is_meta_file(old) && !is_meta_file(new)) ||
        (is_meta_file(new) && !is_meta_file(old))) {
        /* do not rename metafiles to non-meta and reversed */
        return -EINVAL;
    }

    if (is_meta_file(old) && is_meta_file(new)) {
        /* delegate to real fs */
        char *oldcopy = strdup(old);
        char *newcopy = strdup(new);
        char *bnold = gnu_basename(oldcopy);
        char *bnnew = gnu_basename(newcopy);
        char realold[MAXREALPATHLEN], realnew[MAXREALPATHLEN];
        snprintf(realold, MAXREALPATHLEN, "%s/%s", l_data.metadir, bnold);
        snprintf(realnew, MAXREALPATHLEN, "%s/%s", l_data.metadir, bnnew);
        free(oldcopy);
        free(newcopy);

        l_log("realpaths old: %s new: %s\n", realold, realnew);

        int r = rename(realold, realnew);
        if (r == -1) {
            l_log("%s\n", strerror(errno));
            return -errno;
        }
    }

    /* change path in files list */
    struct l_file *newfile;
    newfile = (struct l_file *)malloc(sizeof(*newfile));
    strcpy(newfile->path, new);
    HASH_ADD_STR(l_data.files, path, newfile);

    newfile->fd = file->fd;
    newfile->size = file->size;
    newfile->realfd = file->realfd;
    strncpy(newfile->pattern, file->pattern, 4);

    HASH_DEL(l_data.files, file);
    free(file);

    return 0;
}

int l_ftruncate(const char *path, off_t length, struct fuse_file_info *fi)
{
    /* Same as truncate for this filesystem. */
    return l_truncate(path, length);
}

int l_fgetattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
    /* Same as getattr for this filesystem. */
    return l_getattr(path, stbuf);
}

int l_lock(const char *path, struct fuse_file_info *fi, int cmd,
    struct flock *locks)
{
    return -EINVAL;
}

void *l_init(struct fuse_conn_info *conn)
{
    return NULL;
}

struct fuse_operations l_ops = {
    .getattr     = l_getattr,
    .unlink      = l_unlink,
    .truncate    = l_truncate,
    .open        = l_open,
    .read        = l_read,
    .write       = l_write,
    .flush       = l_flush,
    .release     = l_release,
    .getxattr    = l_getxattr,
    .opendir     = l_opendir,
    .readdir     = l_readdir,
    .destroy     = l_destroy,
    .access      = l_access,
    .create      = l_create,
    .ftruncate   = l_ftruncate,
    .fgetattr    = l_fgetattr,
    .lock        = l_lock,
    .init        = l_init,
    .rename      = l_rename,
    /* TODO(vladum): Add the new functions? */
};

enum {
     KEY_HELP,
     KEY_VERSION,
};

static struct fuse_opt l_opts[] = {
    { "realstore=%s", offsetof(struct l_state, metadir), 0 },
    { "logfile=%s", offsetof(struct l_state, log_file), 0 },
    FUSE_OPT_KEY("-V",             KEY_VERSION),
    FUSE_OPT_KEY("--version",      KEY_VERSION),
    FUSE_OPT_KEY("-h",             KEY_HELP),
    FUSE_OPT_KEY("--help",         KEY_HELP),
    FUSE_OPT_END
};

static int l_opt_proc(void *data, const char *arg, int k, struct fuse_args *oa)
{
    switch (k) {
        case KEY_HELP:
            fprintf(stderr,
                "usage: %s mountpoint [options]\n"
                "\n"
                "general options:\n"
                "    -o opt,[opt...]  mount options\n"
                "    -h   --help      print help\n"
                "    -V   --version   print version\n"
                "\n"
                "LFS options:\n"
                "    -o realstore=PATH      real dir for libswift meta files\n"
                "    -o logfile=PATH        optional log file\n"
                "\n", oa->argv[0]);
            fuse_opt_add_arg(oa, "-ho");
            fuse_main(oa->argc, oa->argv, &l_ops, NULL);
            exit(1);

        case KEY_VERSION:
             fprintf(stderr, "LFS version %s\n", VERSION);
             fuse_opt_add_arg(oa, "--version");
             fuse_main(oa->argc, oa->argv, &l_ops, NULL);
             exit(0);
    }
    return 1;
}

static struct fuse *l_setup(struct fuse_args *args,
                            const struct fuse_operations *op, size_t op_size,
                            char **mountpoint, int *multithreaded)
{
    struct fuse_chan *ch;
    struct fuse *fuse;
    int foreground;
    int res;

    res = fuse_parse_cmdline(args, mountpoint, multithreaded, &foreground);
    if (res == -1)
        return NULL;

    ch = fuse_mount(*mountpoint, args);
    if (!ch) {
        fuse_opt_free_args(args);
        goto err_free;
    }

    fuse = fuse_new(ch, args, op, op_size, &l_data);
    fuse_opt_free_args(args);
    if (fuse == NULL)
        goto err_unmount;

    res = fuse_daemonize(foreground);
    if (res == -1)
        goto err_unmount;

    res = fuse_set_signal_handlers(fuse_get_session(fuse));
    if (res == -1)
        goto err_unmount;

    return fuse;

err_unmount:
    fuse_unmount(*mountpoint, ch);
    if (fuse)
        fuse_destroy(fuse);
err_free:
    free(*mountpoint);
    return NULL;
}

static int l_main(struct fuse_args *args)
{
    char *mountpoint;
    int multithreaded;
    struct fuse *fuse;
    int res;

    fuse = l_setup(args, &l_ops, sizeof(l_ops), &mountpoint, &multithreaded);
    if (fuse == NULL) {
        return 1;
    }

    printf ("Mountpoint: %s\n", mountpoint);

    if (multithreaded) {
        res = fuse_loop_mt(fuse);
    } else {
        res = fuse_loop(fuse);
    }

    fuse_teardown(fuse, mountpoint);
    if (res == -1) {
        return 1;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    if ((getuid() == 0) || (geteuid() == 0)) {
        fprintf(stderr, "Please DO NOT run this as root!\n");
        return 1;
    }

    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    fuse_opt_parse(&args, &l_data, l_opts, l_opt_proc);

    /* Get and open log file. */
    if (l_data.log_file != NULL) {
        printf("Logging to file: %s\n", (char *)l_data.log_file);
        l_data.log_file = fopen(l_data.log_file, "w");
        l_log("log started\n");
    }

    /* Get and resolve libswift meta files dir. */
    if (l_data.metadir == NULL) {
        fprintf(stderr, "Path for libswift meta files not specified. "
                        "Please use -o realstore=PATH option.\n");
        exit(1);
    }
    l_data.metadir = realpath(l_data.metadir, NULL);
    if (l_data.metadir == NULL) {
        perror("Failed to resolve realstore path.");
        exit(1);
    }
    printf("Libswift metadir: %s\n", l_data.metadir);

    /* FUSE */
    return l_main(&args);
}
