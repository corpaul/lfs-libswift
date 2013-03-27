/*
 * Lazy File System
 *
 * This only has virtual files of a specific size. The files are not actually
 * stored anywhere, but some predetermined bytes are return at each read. Writes
 * are just send to a black hole. We use this to simulated huge files (e.g.,
 * 1TB) without actually storing the data in an underlying storage system.
 *
 * This file system does not support directories, symlinks or other non-required
 * operations. You get only read-only files and the option to change their size.
 */

#ifdef linux
/* For pread()/pwrite() */
#define _XOPEN_SOURCE 500
#endif

#define FUSE_USE_VERSION 26
#include <fuse.h>

#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <libgen.h>
#include <bsd/string.h>

#include "uthash.h"

#define MAXPATHLEN 65565
#define MAXMETAPATHLEN 65565

struct file_size {
	char path[MAXPATHLEN];
	off_t size;
	char *mhash;
	char *mbinmap;
	int realfd;
	UT_hash_handle hh;
};

struct l_state {
	struct file_size *file_to_size; /* hash table holding sizes */
	unsigned char realmeta;
	char metapath[MAXMETAPATHLEN];
};

#define L_DATA ((struct l_state *) fuse_get_context()->private_data)

static inline char *gnu_basename(char *path)
{
	char *base = strrchr(path, '/');
	return base ? base+1 : path;
}

/*
 * Returns a file's attributes.
 *
 * This should be fast as it is called all the time. Because we do not have an
 * underlying storage, we will keep all this information in memory at a location
 * pointed by the private_data field of fuse_context. We will use a hashtable
 * that maps a file path to the corresponding size. Size is the only attribute
 * this file system cares about.
 */
int l_getattr(const char *path, struct stat *stbuf)
{
	struct timespec zero = {.tv_sec = 0, .tv_nsec = 0};

	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;

		return 0;
	}

	/* We only actually care about the size. */
	struct file_size *file_size;

	/* DEBUG */
	//struct file_size *files = L_DATA->file_to_size;
	//struct file_size *f, *tmp;
	//printf("iterating files:\n");
	//HASH_ITER(hh, files, f, tmp) {
		//printf("%s %ld %ld\n", f->path, f->size, &(f->size));
	//}

	HASH_FIND_STR(L_DATA->file_to_size, path, file_size);
	if (file_size == NULL) {
		return -ENOENT;
	} else {
		if ((L_DATA->realmeta == 1) &&
			(strcmp(path + strlen(path) - 6, ".mhash") == 0 ||
			strcmp(path + strlen(path) - 8, ".mbinmap") == 0)) {
			// Delegating to real filesystem for meta files.
			char *pathcopy = strdup(path);
			char *bn = gnu_basename(pathcopy);
			char realpath[MAXPATHLEN];
			snprintf(realpath, MAXPATHLEN, "%s/%s", L_DATA->metapath, bn);
			int r = stat(realpath, stbuf);
			fprintf(stderr, "%d\n", r);
			free(pathcopy);

			return r;
		}

		stbuf->st_dev = 0;
		stbuf->st_ino = 0;
		stbuf->st_mode = S_IFREG | S_IRWXU | S_IRWXG | S_IRWXO;
		stbuf->st_nlink = 1;
		stbuf->st_uid = getuid();
		stbuf->st_gid = getgid();
		stbuf->st_rdev = 0;
		stbuf->st_blksize = 0;
		stbuf->st_atime = 0;//zero;
		stbuf->st_mtime = 0;//zero;
		stbuf->st_ctime = 0;//zero;
		stbuf->st_size = file_size->size;
		stbuf->st_blocks = stbuf->st_size / 512;
	}

	/* This is always successful. */
	return 0;
}

int l_readlink(const char *path, char *link, size_t size)
{
	return -ENOSYS;
}

int l_mknod(const char *path, mode_t mode, dev_t dev)
{
	return -ENOSYS;
}

int l_mkdir(const char *path, mode_t mode)
{
	return -ENOSYS;
}

/*
 * Removes the file.
 *
 * We just delete the entry for this file from the hashtable.
 */
int l_unlink(const char *path)
{
	/* TODO(vladum): Delete entry from hashtable. */
	return 0;
}

int l_rmdir(const char *path)
{
	return -ENOSYS;
}

int l_symlink(const char *oldname, const char *newname)
{
	return -ENOSYS;
}

int l_rename(const char *oldname, const char *newname)
{
	return -ENOSYS;
}

int l_link(const char *oldname, const char *newname)
{
	return -ENOSYS;
}

int l_chmod(const char *path, mode_t mode)
{
	return -ENOSYS;
}

int l_chown(const char *path, uid_t owner, gid_t group)
{
	return -ENOSYS;
}

/*
 * Change file size.
 *
 * Returns 0 on success or -ENOENT if the file does not exists.
 */
int l_truncate(const char *path, off_t length)
{
	if ((L_DATA->realmeta == 1) &&
		(strcmp(path + strlen(path) - 6, ".mhash") == 0 ||
		strcmp(path + strlen(path) - 8, ".mbinmap") == 0)) {
		// Delegating to real filesystem for meta files.
		char *pathcopy = strdup(path);
		char *bn = gnu_basename(pathcopy);
		char realpath[MAXPATHLEN];
		snprintf(realpath, MAXPATHLEN, "%s/%s", L_DATA->metapath, bn);
		int r = truncate(realpath, length);
		free(pathcopy);

		return r;
	}

	//printf("in l_truncate: length=%ld\n", length);
	/* TODO(vladum): Check for max size? */
	struct file_size *file_size;
	HASH_FIND_STR(L_DATA->file_to_size, path, file_size);
	if (file_size == NULL) {
		/* File not found. */
		return -ENOENT;
	} else {
		file_size->size = length;

		if (strcmp(path + strlen(path) - 6, ".mhash") == 0) {
			/* Allocate space for mhash file. */
			file_size->mhash = (char *)realloc(file_size->mhash, file_size->size);
		} else if (strcmp(path + strlen(path) - 8, ".mbinmap") == 0) {
			/* Allocate space for mhash file. */
			file_size->mbinmap = (char *)realloc(file_size->mbinmap, file_size->size);
		}

		//printf("len = %ld, size = %ld pointer = %ld\n", length, file_size->size, &(file_size->size));
		return 0;
	}
}

/*
 * Opens a file.
 *
 * O_CREAT and O_EXCL are guaranteed to not be passed to this. The file will
 * exist when this is called. O_TRUNC might be present when atomic_o_trunc is
 * specified on a kernel version of 2.6.24 or later.
 *
 * Returns 0 when successful or an error otherwise.
 */
int l_open(const char *path, struct fuse_file_info *fi)
{
	struct file_size *file_size;

	HASH_FIND_STR(L_DATA->file_to_size, path, file_size);
	if (file_size == NULL) {
		/* File does not exist. */
		return -ENOENT;
	} else {
		if ((L_DATA->realmeta == 1) &&
			(strcmp(path + strlen(path) - 6, ".mhash") == 0 ||
			strcmp(path + strlen(path) - 8, ".mbinmap") == 0)) {
			// Delegating to real filesystem for meta files.
			// Nothing I guess (except access rights)...
			char *pathcopy = strdup(path);
			char *bn = gnu_basename(pathcopy);
			char realpath[MAXPATHLEN];
			snprintf(realpath, MAXPATHLEN, "%s/%s", L_DATA->metapath, bn);
			fprintf(stderr, "opening real file %s\n", realpath);
			file_size->realfd = open(realpath, fi->flags);
			free(pathcopy);
			return 0;
		}
		/* If O_TRUNC set the size to 0. */
		if (fi->flags & O_TRUNC)
			file_size->size = 0;
		printf("returning 0...\n");
		/* This is it. We don't care about access rights. */
		return 0;
	}
}

/*
 * Reads file.
 *
 * This is the only thing serious in this file system. It returns an incresing
 * number each 8 bytes. This means you have 2^64 numbers so the maximum filesize
 * is 8 bytes * 2^64 which is 128 YB or 134217728 TB.
 *
 * TODO(vladum): Handle direct_io.
 */
int l_read(const char *path, char *buf, size_t size, off_t offset,
	struct fuse_file_info *fi)
{
	printf ("l_read begin: %s %ld b from %ld\n", path, size, offset);

	struct file_size *file_size;
	HASH_FIND_STR(L_DATA->file_to_size, path, file_size);
	if (file_size == NULL) {
		/* File does not exist. */
		return -ENOENT;
	}

	if ((L_DATA->realmeta == 1) &&
		(strcmp(path + strlen(path) - 6, ".mhash") == 0 ||
		strcmp(path + strlen(path) - 8, ".mbinmap") == 0)) {
		// Delegating to real filesystem for meta files.
		lseek(file_size->realfd, offset, SEEK_SET);
		return read(file_size->realfd, buf, size);
	}

	if (strcmp(path + strlen(path) - 6, ".mhash") == 0) {
		if (file_size->mhash == NULL)
			return 0;

		/* Read hashes from memory. */
		int i;

		for (i = 0; i < size; i++)
			buf[i] = file_size->mhash[offset + i];

		printf ("l_read end: %s %d b read\n", path, i);
		return size;
	} else if (strcmp(path + strlen(path) - 8, ".mbinmap") == 0) {
		if (file_size->mbinmap == NULL)
			return 0;

		/* Read bins from memory. */
		int i;

		for (i = 0; i < size; i++)
			buf[i] = file_size->mbinmap[offset + i];

		printf ("l_read end: %s %d b read\n", path, i);
		return size;
	} else {
		int i, j;
		for (i = 0; i < size; i++) {
			if (offset + i >= file_size->size) {
				/* Fill remaining buffer with 0s. */
				for (j = i; j < size; j++)
					buf[j] = 0x00;

				/* Return bytes read so far. */
				return i;
			} else {
				buf[i] = 0xFE;
			}
		}
	}

	/* EOF was not encountered. */
	return size;
}

int l_write(const char *path, const char *buf, size_t size, off_t offset,
	struct fuse_file_info *fi)
{
	printf ("l_write begin: %s %ld b at %ld\n", path, size, offset);

	if ((L_DATA->realmeta == 1) &&
		(strcmp(path + strlen(path) - 6, ".mhash") == 0 ||
		strcmp(path + strlen(path) - 8, ".mbinmap") == 0)) {
		struct file_size *file_size;
		HASH_FIND_STR(L_DATA->file_to_size, path, file_size);
		if (file_size == NULL) {
			/* File does not exist. */
			return -ENOENT;
		}

		// Delegating to real filesystem for meta files.
		fprintf(stderr, "writing to real fd %d\n", file_size->realfd);
		//int x = lseek(file_size->realfd, offset, SEEK_SET);
		//fprintf(stderr, "lseek: %d %s\n", x, strerror(errno));

		int r = write(file_size->realfd, buf, size);
		fprintf(stderr, "wrote %d bytes to real file\n", r);
		return r;
	}

	/* LFS only accepts writes to the .mhash file. */
	if (strcmp(path + strlen(path) - 6, ".mhash") == 0) {
		struct file_size *file_size;
		HASH_FIND_STR(L_DATA->file_to_size, path, file_size);
		if (file_size == NULL) {
			/* File does not exist. */
			return -ENOENT;
		}

		if (file_size->size < offset + size) {
			/* File has to be resized to accomodate new data. */
			file_size->mhash = (char *)realloc(file_size->mhash, offset + size);
			file_size->size = offset + size;
		}

		int i;
		for (i = 0; i < size; i++) {
			file_size->mhash[offset + i] = buf[i];
		}

		printf ("l_write end: %s %d b written\n", path, i);
		return size;
	} else if (strcmp(path + strlen(path) - 8, ".mbinmap") == 0) {
		struct file_size *file_size;
		HASH_FIND_STR(L_DATA->file_to_size, path, file_size);
		if (file_size == NULL) {
			/* File does not exist. */
			return -ENOENT;
		}

		if (file_size->size < offset + size) {
			/* File has to be resized to accomodate new data. */
			file_size->mbinmap = (char *)realloc(file_size->mbinmap,
			                                     offset + size);
			file_size->size = offset + size;
		}

		int i;
		for (i = 0; i < size; i++) {
			file_size->mbinmap[offset + i] = buf[i];
		}

		printf ("l_write end: %s %d b written\n", path, i);
		return size;
	} else {
		return -ENOSYS;
	}
}

int l_statfs(const char *path, struct statvfs *s)
{
	return -ENOSYS;
}

int l_flush(const char *path, struct fuse_file_info *fi)
{
	/* Nothing to flush, so this always succeeds. */
	return 0;
}

int l_release(const char *path, struct fuse_file_info *fi)
{
	struct file_size *file_size;
	HASH_FIND_STR(L_DATA->file_to_size, path, file_size);
	if (file_size == NULL) {
		/* File does not exist. */
		return -ENOENT;
	}
	if ((L_DATA->realmeta == 1) &&
		(strcmp(path + strlen(path) - 6, ".mhash") == 0 ||
		strcmp(path + strlen(path) - 8, ".mbinmap") == 0)) {
		// Delegating to real filesystem for meta files.
		fprintf(stderr, "closing real fd %d\n", file_size->realfd);
		return close(file_size->realfd);
	}

	/* Nothing to do for release() and the return value is ignored. */
	return 0;
}

int l_fsync(const char *path, int isdatasync, struct fuse_file_info *fi)
{
	return -ENOSYS;
}

int l_setxattr(const char *path, const char *name, const char *value,
	size_t size, int flags)
{
	return -ENOSYS;
}

int l_getxattr(const char *path, const char *name, char *value, size_t size)
{
	return -ENOSYS;
}

int l_listxattr(const char *path, char *list, size_t size)
{
	return -ENOSYS;
}

int l_removexattr(const char *path, const char *list)
{
	return -ENOSYS;
}

int l_opendir(const char *path, struct fuse_file_info *fi)
{
	//fprintf(stderr, "in l_opendir: path=%s\n", path);

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

	struct file_size *files = L_DATA->file_to_size, *f, *tmp;
	HASH_ITER(hh, files, f, tmp) {
		filler(buf, f->path + 1, NULL, 0);
	}

	return 0;
}

int l_releasedir(const char *path, struct fuse_file_info *fi)
{
	return -ENOSYS;
}

int l_fsyncdir(const char *path, int isdatasync, struct fuse_file_info *fi)
{
	return -ENOSYS;
}

/*
 * Initializes the Lazy File System.
 */
void *l_init(struct fuse_conn_info *conn)
{
	fprintf(stderr, "l_init\n");
	/* Just return the hashtable. */
	return L_DATA;
}

/*
 * Cleans up the Lazy File System.
 */
void l_destroy(void *userdata)
{
	struct file_size *files = ((struct l_state *)userdata)->file_to_size;
	struct file_size *f, *tmp;

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
 *
 * We add a new entry in the hashtable.
 */
int l_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	struct file_size *file_size;

	fprintf(stderr, "l_create: file is %s\n", path);

	HASH_FIND_STR(L_DATA->file_to_size, path, file_size);
	if (file_size == NULL) {
		file_size = (struct file_size *)malloc(sizeof(*file_size));
		file_size->size = 0;
		strcpy(file_size->path, path);

		if ((L_DATA->realmeta == 1) &&
			(strcmp(path + strlen(path) - 6, ".mhash") == 0 ||
			strcmp(path + strlen(path) - 8, ".mbinmap") == 0)) {
			// Delegating to real filesystem for meta files.
			char *pathcopy = strdup(path);
			char *bn = gnu_basename(pathcopy);
			char realpath[MAXPATHLEN];
			snprintf(realpath, MAXPATHLEN, "%s/%s", L_DATA->metapath, bn);
			fprintf(stderr, "creating real file %s\n", realpath);
			free(pathcopy);
			int fd;
			/* TODO(vladum): Remove | O_RDWR hask. */
			if ((fd = open(realpath, fi->flags | O_RDWR, mode) != -1)) {
				HASH_ADD_STR(L_DATA->file_to_size, path, file_size);
				file_size->realfd = fd;
				//write(fd, "test\n", 5);
				//fprintf(stderr, "write ok in fd %d\n", fd);
				return fi->fh;
			} else {
				return -1;
			}
		}

		HASH_ADD_STR(L_DATA->file_to_size, path, file_size);
	} else {
		if ((fi->flags & O_CREAT) && (fi->flags & O_EXCL)) {
			/* File already exists. */
			return -EEXIST;
		}
	}

	/* Reset size. */
	file_size->size = 0;
	file_size->mhash = NULL;
	file_size->mbinmap = NULL;

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
	return -ENOSYS;
}

int l_utimens(const char *path, const struct timespec ts[2])
{
	return -ENOSYS;
}

int l_bmap(const char *path, size_t blocksize, uint64_t *blockno)
{
	return -ENOSYS;
}

struct fuse_operations l_ops = {
	.getattr     = l_getattr,
	.readlink    = l_readlink,
	.getdir      = NULL,          /* deprecated */
	.mknod       = l_mknod,
	.mkdir       = l_mkdir,
	.unlink      = l_unlink,
	.rmdir       = l_rmdir,
	.symlink     = l_symlink,
	.rename      = l_rename,
	.link        = l_link,
	.chmod       = l_chmod,
	.chown       = l_chown,
	.truncate    = l_truncate,
	.utime       = NULL,          /* deprecated */
	.open        = l_open,
	.read        = l_read,
	.write       = l_write,
	.statfs      = l_statfs,
	.flush       = l_flush,
	.release     = l_release,
	.fsync       = l_fsync,
	.setxattr    = l_setxattr,
	.getxattr    = l_getxattr,
	.listxattr   = l_listxattr,
	.removexattr = l_removexattr,
	.opendir     = l_opendir,
	.readdir     = l_readdir,
	.releasedir  = l_releasedir,
	.fsyncdir    = l_fsyncdir,
	.init        = l_init,
	.destroy     = l_destroy,
	.access      = l_access,
	.create      = l_create,
	.ftruncate   = l_ftruncate,
	.fgetattr    = l_fgetattr,
	.lock        = l_lock,
	.utimens     = l_utimens,
	.bmap        = l_bmap

	/* TODO(vladum): Add the new functions? */
};

int main(int argc, char *argv[])
{
	if ((getuid() == 0) || (geteuid() == 0)) {
		//fprintf(stderr,
		//	"Cannot mount LFS as root because it is not secure.\n");
		return 1;
	}

	/* Check command line. */
	if (argc < 2) {
		//fprintf(stderr,
		//	"Usage:\n\tlfs [FUSE and mount options] mountpoint\n");
		return 1;
	}

	struct l_state *l_data;
	l_data = malloc(sizeof(struct l_state));
	if (l_data == NULL) {
		//fprintf(stderr, "Cannot allocate LFS state structure.\n");
		return 1;
	}

	if (argc > 2) {
		// We are using a real fs location for meta files.
		l_data->realmeta = 1;
		// TODO(vladum): Check if provided string is actually a directory.
		strlcpy(l_data->metapath, argv[argc - 1], sizeof(l_data->metapath));
		argv[argc - 1] = NULL;
		argc -= 1;
	} else {
		l_data->realmeta = 0;
	}

	/* FUSE */
	int fuse_stat = fuse_main(argc, argv, &l_ops, l_data);

	return fuse_stat;
}
