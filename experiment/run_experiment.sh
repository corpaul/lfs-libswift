#!/bin/bash

set +e
set +v 
# Machine-specific variables ---------------------------------------------------
# Edit these when running, for example, on Jenkins.
WORKSPACE=.
STAP_BIN=stap
DIR_SWIFT=.
DIR_LFS=.
LOCAL=true
# ------------------------------------------------------------------------------

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
make -C $DIR_SWIFT

# get and build LFS
if [ ! -d "$DIR_LFS" ]; then
    git clone git@github.com:vladum/lfs-libswift.git $DIR_LFS
fi
if [ ! $LOCAL ]; then
    cd $DIR_LFS
    git pull origin master
    cd -
fi

# start source LFS
$DIR_LFS/lfs $LFS_SRC_STORE -o fsname=lfssrc,realstore=$LFS_SRC_REALSTORE,big_writes &
LFS_SRC_PID=$!
wait $LFS_SRC_PID

# start destination LFS
$DIR_LFS/lfs $LFS_DST_STORE -o fsname=lfsdst,realstore=$LFS_DST_REALSTORE,big_writes &
LFS_DST_PID=$!
wait $LFS_DST_PID

# create data file and get precomputed metafiles
truncate -s 128GiB $LFS_SRC_STORE/aaaaaaaa_128gb_8192

hexdump -C -n 8192 $LFS_SRC_STORE/aaaaaaaa_128gb_8192

META_ARCHIVE=$WORKSPACE/meta.tar.gz2
META_URL=https://dl.dropboxusercontent.com/u/18515377/Tribler/aaaaaaaa_128gb_8192.tar.gz

ETAG=`awk '/.*etag:.*/ { gsub(/[ \t\n\r]+$/, "", $2); print $2 }' $META_ARCHIVE.headers | tail -1`
wget --header="If-None-Match: $ETAG" -S --no-check-certificate -O $META_ARCHIVE $META_URL 2>&1 | tee $META_ARCHIVE.headers
mkdir ${META_ARCHIVE}_dir || true
tar zxvf $META_ARCHIVE -C ${META_ARCHIVE}_dir || true
echo "Copying meta files. Please wait."
cp ${META_ARCHIVE}_dir/* $LFS_SRC_STORE || true

HASH=$(cat $LFS_SRC_STORE/aaaaaaaa_128gb_8192.mbinmap | grep hash | cut -d " " -f 3)

hexdump -C -n 8192 $LFS_SRC_STORE/aaaaaaaa_128gb_8192

mv $LFS_SRC_STORE/aaaaaaaa_128gb_8192 $LFS_SRC_STORE/$HASH
mv $LFS_SRC_STORE/aaaaaaaa_128gb_8192.mbinmap $LFS_SRC_STORE/$HASH.mbinmap
mv $LFS_SRC_STORE/aaaaaaaa_128gb_8192.mhash $LFS_SRC_STORE/$HASH.mhash

ls -alh $LFS_SRC_STORE
ls -alh $LFS_SRC_REALSTORE

hexdump -C -n 8192 $LFS_SRC_STORE/$HASH

# start source swift
$STAP_BIN -v $DIR_LFS/stap/cpu_io_mem_2.stp -o $LOGS_DIR/swift.src.stap.out -c "taskset -c 0 $DIR_SWIFT/swift -w 60s -e $LFS_SRC_STORE -l 1337 -c 10000 -z 8192 --progress -D$LOGS_DIR/swift.src.debug" >$LOGS_DIR/swift.src.log 2>&1 &
SWIFT_SRC_PID=$!

echo "Starting destination in 6s..."
sleep 6s

# start destination swift
$STAP_BIN -v $DIR_LFS/stap/cpu_io_mem_2.stp -o $LOGS_DIR/swift.dst.stap.out -c "taskset -c 1 $DIR_SWIFT/swift -w 50s -o $LFS_DST_STORE -t 127.0.0.1:1337 -h $HASH -z 8192 --progress -D$LOGS_DIR/swift.dst.debug" >$LOGS_DIR/swift.dst.log 2>&1 &
SWIFT_DST_PID=$!

echo "Waiting for swifts to finish (~60s)..."

sleep 40s

#wait $SWIFT_SRC_PID
#wait $SWIFT_DST_PID

echo "---------------------------------------------------------------------------------"

ls -alh $LFS_SRC_STORE
ls -alh $LFS_SRC_REALSTORE
ls -alh $LFS_DST_STORE
ls -alh $LFS_DST_REALSTORE

#sleep 10s

# --------- EXPERIMENT END ----------
#kill -9 $SWIFT_SRC_PID $SWIFT_DST_PID || true
fusermount -z -u $LFS_SRC_STORE
fusermount -z -u $LFS_DST_STORE
#kill -9 $LFS_SRC_PID $LFS_DST_PID || true
pkill -9 swift || true

sleep 5s

# remove temps
rm -rf $LFS_SRC_STORE
rm -rf $LFS_SRC_REALSTORE
rm -rf $LFS_DST_STORE
rm -rf $LFS_DST_REALSTORE
rm -rf ./src ./dst # TODO
