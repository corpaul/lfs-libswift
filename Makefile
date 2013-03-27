lfs : lfs.o
	gcc -O3 -o lfs lfs.o -lbsd `pkg-config fuse --libs`

lfs.o : lfs.c uthash.h
	gcc -O3 -Wall -lbsd `pkg-config fuse --cflags` -c lfs.c

clean:
	rm -f lfs *.o
