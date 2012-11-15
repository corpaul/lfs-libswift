lfs : lfs.o
	gcc -g -o lfs lfs.o `pkg-config fuse --libs`

lfs.o : lfs.c uthash.h
	gcc -g -Wall `pkg-config fuse --cflags` -c lfs.c

clean:
	rm -f lfs *.o
