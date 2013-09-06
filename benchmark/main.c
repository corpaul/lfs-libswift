#define _LARGEFILE_SOURCE

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>

int prepare_file(const char *pathname, off_t size) {
	int r;

	r = open(pathname, O_CREAT | O_WRONLY | O_TRUNC);
	if (r == -1) {
		return -1;
	}

	r = truncate(pathname, size);
	if (r != 0) {
		return -1;
	}

	return 0;
}

int read_bm(int fd, int fd2, uint64_t chunk_size) {
	struct stat st;
	
	// get file size
	fstat(fd, &st);

	fprintf(stderr, "%lu bytes reads in %lu bytes file\n", chunk_size, st.st_size);

	uint64_t i, j;
	uint64_t cn = st.st_size / chunk_size;
	struct timeval t1, t2;
    double duration;
    char buf[65536];

    for (j = 0; j < 1000; j++) {
    	// LFS
		gettimeofday(&t1, NULL);
		for (i = 0; i < cn; i++) {
			read(fd, buf, chunk_size);
		}
		gettimeofday(&t2, NULL);
		duration = (t2.tv_sec - t1.tv_sec) * 1000.0;
		duration += (t2.tv_usec - t1.tv_usec) / 1000.0;
		printf("%lf ", duration);

		// normal FS
		gettimeofday(&t1, NULL);
		for (i = 0; i < cn; i++) {
			read(fd2, buf, chunk_size);
		}
		gettimeofday(&t2, NULL);
		duration = (t2.tv_sec - t1.tv_sec) * 1000.0;
		duration += (t2.tv_usec - t1.tv_usec) / 1000.0;
		printf("%lf\n", duration);

		lseek(fd, 0, SEEK_SET);
		lseek(fd2, 0, SEEK_SET);
	}
}

int main(int argc, char *argv[]) {
	char path[256], path2[256], buffer[65536];
	int r;

	path[0] = '\0';
	strcat(path, argv[1]);
	strcat(path, "/");
	strcat(path, argv[2]);

	path2[0] = '\0';
	strcat(path2, argv[1]);
	strcat(path2, "/");
	strcat(path2, argv[4]);

	// uint64_t s = 128 * 1024 * 1024 * 1024L;
	// r = prepare_file(path, s);
	// printf("asdasdasd\n");
	// if (r != 0) {
	// 	exit(1);
	// }


	// sprintf(buffer, "ls -alh %s", argv[1]);
	// printf("Directory: %s\n", buffer);

	// system(buffer);

	int fd = open(path, O_RDONLY);
	int fd2 = open(path2, O_RDONLY);
	read_bm(fd, fd2, atoi(argv[3]));
	close(fd);
	close(fd2);

	return 0;
}