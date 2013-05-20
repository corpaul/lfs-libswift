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
 * Usage: ./lfs [fuse options] <mountpoint> <realstore>
 */

#define FUSE_USE_VERSION 26 /* new API */
#include <fuse.h>

#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <libgen.h>
#include <bsd/string.h>
#include <attr/xattr.h>

#include "uthash.h"

#define MAXPATHLEN 65565
#define MAXMETAPATHLEN 65565

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
    char metadir[MAXMETAPATHLEN];
    unsigned nfiles;
};

#define L_DATA ((struct l_state *) fuse_get_context()->private_data)

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

/*
 * Predefined attributes - we don't care about most of these.
 */
int l_getattr(const char *path, struct stat *stbuf)
{
    struct l_file *file;

    /* find it */
    HASH_FIND_STR(L_DATA->files, path, file);
    if (file == NULL) {
        return -ENOENT;
    }

    if (is_meta_file(path)) {
        /* delegate to real fs */
        char *pathcopy = strdup(path);
        char *bn = gnu_basename(pathcopy);
        char realpath[MAXPATHLEN];
        snprintf(realpath, MAXPATHLEN, "%s/%s", L_DATA->metadir, bn);
        int r = stat(realpath, stbuf);
        free(pathcopy);

        return r;
    } else if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;

        return 0;
    } else {
        time_t now = time(NULL);

        stbuf->st_dev = 0;
        stbuf->st_ino = 0;
        stbuf->st_mode = S_IFREG | S_IRWXU | S_IRWXG | S_IRWXO;
        stbuf->st_nlink = 1;
        stbuf->st_uid = getuid();
        stbuf->st_gid = getgid();
        stbuf->st_rdev = 0;
        stbuf->st_blksize = 0;
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

    /* find it */
    HASH_FIND_STR(L_DATA->files, path, file);
    if (file == NULL) {
        return -ENOENT;
    }

    HASH_DEL(L_DATA->files, file);
    free(file);

    return 0;
}

/*
 * Changes file size.
 */
int l_truncate(const char *path, off_t length)
{
    struct l_file *file;
    
    /* find it */
    HASH_FIND_STR(L_DATA->files, path, file);
    if (file == NULL) {
        return -ENOENT;
    }
    
    if (is_meta_file(path)) {
        /* delegate to real fs */
        char *pathcopy = strdup(path);
        char *bn = gnu_basename(pathcopy);
        char realpath[MAXPATHLEN];
        snprintf(realpath, MAXPATHLEN, "%s/%s", L_DATA->metadir, bn);
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

    /* find it */
    HASH_FIND_STR(L_DATA->files, path, file);
    if (file == NULL) {
        return -ENOENT;
    }
    
    if (is_meta_file(path)) {
        /* delegate to real fs */
        char *pathcopy = strdup(path);
        char *bn = gnu_basename(pathcopy);
        char realpath[MAXPATHLEN];
        snprintf(realpath, MAXPATHLEN, "%s/%s", L_DATA->metadir, bn);

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
    HASH_FIND_STR(L_DATA->files, path, file);
    if (file == NULL) {
        return -ENOENT;
    }

    if (is_meta_file(path)) {
        /* delegate to real fs */
        lseek(file->realfd, offset, SEEK_SET);
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
                buf[i] = file->fd;
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
    
    /* find it */
    HASH_FIND_STR(L_DATA->files, path, file);
    if (file == NULL) {
        return -ENOENT;
    }
    
    if (is_meta_file(path)) {
        /* delegate to real fs */
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
    HASH_FIND_STR(L_DATA->files, path, file);
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
    return -ENOATTR;
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

    struct l_file *files = L_DATA->files, *f, *tmp;
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

    /* find it */
    HASH_FIND_STR(L_DATA->files, path, file);
    if (file != NULL) {
        if ((fi->flags & O_CREAT) && (fi->flags & O_EXCL)) {
            /* File already exists. */
            return -EEXIST;
        }
    }

    if (file == NULL) {
        file = (struct l_file *)malloc(sizeof(*file));
        file->size = 0;
        strcpy(file->path, path);

        if (is_meta_file(path)) {
            /* delegate to real fs */
            char *pathcopy = strdup(path);
            char *bn = gnu_basename(pathcopy);
            char realpath[MAXPATHLEN];
            snprintf(realpath, MAXPATHLEN, "%s/%s", L_DATA->metadir, bn);
            free(pathcopy);
            int fd;
            if ((fd = open(realpath, O_CREAT | O_RDWR | O_TRUNC, mode)) != -1) {
                HASH_ADD_STR(L_DATA->files, path, file);
                file->realfd = fd;
                return fi->fh;
            } else {
                return -errno;
            }
        } else {
            HASH_ADD_STR(L_DATA->files, path, file);

            file->fd = ++(L_DATA->nfiles);
        }
    }

    /* Reset size. */
    file->size = 0;

    return fi->fh;
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
    return L_DATA;
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
    /* TODO(vladum): Add the new functions? */
};

int main(int argc, char *argv[])
{
    if ((getuid() == 0) || (geteuid() == 0)) {
        return 1;
    }

    /* Check command line. */
    if (argc < 3) {
        fprintf(stderr,
            "Usage: %s [FUSE and mount options] <mountpoint> <realstore>\n",
            argv[0]);
        return 1;
    }

    struct l_state *l_data;
    l_data = malloc(sizeof(struct l_state));
    if (l_data == NULL) {
        return 1;
    }

    // TODO(vladum): Check if provided string is actually a directory.
    strlcpy(l_data->metadir, argv[argc - 1], sizeof(l_data->metadir));
    argv[argc - 1] = NULL;
    argc -= 1;

    /* FUSE */
    int fuse_stat = fuse_main(argc, argv, &l_ops, l_data);

    return fuse_stat;
}
