#!/bin/bash

set -e

WORKSPACE=.

# set directories
DIR_SWIFT=.
DIR_LFS=.

LFS_SRC_STORE=$WORKSPACE/src/store
LFS_SRC_REALSTORE=$WORKSPACE/src/real

LFS_DST_STORE=$WORKSPACE/dst/store
LFS_DST_REALSTORE=$WORKSPACE/dst/real

mkdir -p $LFS_SRC_STORE $LFS_SRC_REALSTORE $LFS_DST_STORE $LFS_DST_REALSTORE

# logging
LOGS_DIR=$WORKSPACE/logs/$(date +'%F-%H-%M')
mkdir -p $LOGS_DIR

fusermount -V
df -h

# build libswift
# make -C $DIR_SWIFT

# get and build LFS
#git clone git@github.com:vladum/lfs-libswift.git $DIR_LFS
#make -C $DIR_LFS

# start source LFS
$DIR_LFS/lfs $LFS_SRC_STORE -o realstore=$LFS_SRC_REALSTORE,big_writes &
LFS_SRC_PID=$!
wait $LFS_SRC_PID

# start destination LFS
$DIR_LFS/lfs $LFS_DST_STORE -o realstore=$LFS_DST_REALSTORE,big_writes &
LFS_DST_PID=$!
wait $LFS_DST_PID

# create data file and get precomputed metafiles
truncate -s 128GiB $LFS_SRC_STORE/aaaaaaaa_128gb_8192

hexdump -C -n 8192 $LFS_SRC_STORE/aaaaaaaa_128gb_8192

META_ARCHIVE=$WORKSPACE/meta.tar.gz2
META_URL=https://dl.dropboxusercontent.com/u/18515377/Tribler/aaaaaaaa_128gb_8192.tar.gz

wget --header="If-None-Match: \"$(cat $META_ARCHIVE.headers 2>/dev/null | grep etag | cut -d " " -f 4)\"" -S --no-check-certificate -O $META_ARCHIVE $META_URL 2>&1 | tee $META_ARCHIVE.headers

tar zxvf $META_ARCHIVE -C $LFS_SRC_STORE || true

HASH=$(cat $LFS_SRC_STORE/aaaaaaaa_128gb_8192.mbinmap | grep hash | cut -d " " -f 3)

hexdump -C -n 8192 $LFS_SRC_STORE/aaaaaaaa_128gb_8192

mv $LFS_SRC_STORE/aaaaaaaa_128gb_8192 $LFS_SRC_STORE/$HASH
mv $LFS_SRC_STORE/aaaaaaaa_128gb_8192.mbinmap $LFS_SRC_STORE/$HASH.mbinmap
mv $LFS_SRC_STORE/aaaaaaaa_128gb_8192.mhash $LFS_SRC_STORE/$HASH.mhash

ls -alh $LFS_SRC_STORE
ls -alh $LFS_SRC_REALSTORE

hexdump -C -n 8192 $LFS_SRC_STORE/$HASH

# start source swift
stap $DIR_LFS/stap/cpu_io_mem.stp -o $LOGS_DIR/swift.src.stap.out -c "taskset -c 0 $DIR_SWIFT/swift -e $LFS_SRC_STORE -l 1337 -c 10000 -z 8192 --progress -D$LOGS_DIR/swift.src.debug" >$LOGS_DIR/swift.src.log 2>&1 &
SWIFT_SRC_PID=$!

echo "Waiting for source..."
sleep 6s

# start destination swift
stap $DIR_LFS/stap/cpu_io_mem.stp -o $LOGS_DIR/swift.dst.stap.out -c "taskset -c 1 $DIR_SWIFT/swift -o $LFS_DST_STORE -t 127.0.0.1:1337 -h $HASH -z 8192 --progress -D$LOGS_DIR/swift.dst.debug" >$LOGS_DIR/swift.dst.log 2>&1 &
SWIFT_DST_PID=$!

echo "Time remaining 50s..."
sleep 10s
echo "Time remaining 40s..."
sleep 10s
echo "Time remaining 30s..."
sleep 10s
echo "Time remaining 20s..."
sleep 10s
echo "Time remaining 10s..."
sleep 10s

echo "---------------------------------------------------------------------------------"

ls -alh $LFS_SRC_STORE
ls -alh $LFS_SRC_REALSTORE
ls -alh $LFS_DST_STORE
ls -alh $LFS_DST_REALSTORE


# --------- EXPERIMENT END ----------
kill -9 $SWIFT_SRC_PID $SWIFT_DST_PID || true
fusermount -z -u $LFS_SRC_STORE
fusermount -z -u $LFS_DST_STORE
kill -9 $LFS_SRC_PID $LFS_DST_PID || true
pkill -9 swift || true

# remove temps
rm -rf $LFS_SRC_STORE
rm -rf $LFS_SRC_REALSTORE
rm -rf $LFS_DST_STORE
rm -rf $LFS_DST_REALSTORE
rm -rf ./src ./dst # TODO
