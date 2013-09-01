#!/bin/bash

# Run this before this script --------------------------------------------------
    # # build libswift
    # make -C $DIR_SWIFT

    # # get and build LFS
    # if [ ! -d "$DIR_LFS" ]; then
    #     git clone git@github.com:vladum/lfs-libswift.git $DIR_LFS
    # else
    #     cd $DIR_LFS
    #     git pull origin master
    #     cd -
    # fi
    # make -C $DIR_LFS
# ------------------------------------------------------------------------------

# Machine-specific variables ---------------------------------------------------
# Override these on Jenkins before running the script.
[ -z $WORKSPACE ] && WORKSPACE=.
[ -z $STAP_BIN ] && STAP_BIN=stap
[ -z $STAP_RUN ] && STAP_RUN=staprun
[ -z $DIR_SWIFT ] && DIR_SWIFT=.
[ -z $DIR_LFS ] && DIR_LFS=.
[ -z $TIME ] && TIME=30
[ -z $FILE_SIZE ] && FILE_SIZE="128GiB"
# ------------------------------------------------------------------------------

echo "Running swift processes for $TIME seconds"
echo "Workspace: $WORKSPACE"

LFS_SRC_STORE=$WORKSPACE/src/store
LFS_SRC_REALSTORE=$WORKSPACE/src/real

LFS_DST_STORE=$WORKSPACE/dst/store
LFS_DST_REALSTORE=$WORKSPACE/dst/real

fusermount -u $LFS_SRC_STORE || true
fusermount -u $LFS_DST_STORE || true

mkdir -p $LFS_SRC_STORE $LFS_SRC_REALSTORE $LFS_DST_STORE $LFS_DST_REALSTORE

# logging
DATE=$(date +'%F-%H-%M')
LOGS_DIR=$WORKSPACE/logs/$DATE
PLOTS_DIR=$WORKSPACE/plots/$DATE
PLOTS_DIR_LAST=$WORKSPACE/plots/last
mkdir -p $LOGS_DIR $PLOTS_DIR $PLOTS_DIR_LAST

fusermount -V
df -h

# start source LFS
$DIR_LFS/lfs $LFS_SRC_STORE -o fsname=lfssrc,realstore=$LFS_SRC_REALSTORE,big_writes &
LFS_SRC_PID=$!
wait $LFS_SRC_PID

# start destination LFS
$DIR_LFS/lfs $LFS_DST_STORE -o fsname=lfsdst,realstore=$LFS_DST_REALSTORE,big_writes &
LFS_DST_PID=$!
wait $LFS_DST_PID

# create data file and get precomputed metafiles
truncate -s $FILE_SIZE $LFS_SRC_STORE/aaaaaaaa_128gb_8192

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

# compile SystemTap module
#$STAP_BIN -p4 -DMAXMAPENTRIES=10000 -t -r `uname -r` -vv -m cpu_io_mem_2 $DIR_LFS/stap/cpu_io_mem_2.stp >$LOGS_DIR/cpu_io_mem_2.compile.log 2>&1

# start source swift
#$STAP_RUN -R -o $LOGS_DIR/swift.src.stap.out -c "taskset -c 0 timeout 60s $DIR_SWIFT/swift -e $LFS_SRC_STORE -l 1337 -c 10000 -z 8192 --progress -D$LOGS_DIR/swift.src.debug" cpu_io_mem_2.ko >$LOGS_DIR/swift.src.log 2>&1 &

mkdir -p $LOGS_DIR/src
mkdir -p $LOGS_DIR/dst

$DIR_LFS/process_guard.py -c "taskset -c 0 $DIR_SWIFT/swift --uprate 307200 -e $LFS_SRC_STORE -l 1337 -c 10000 -z 8192 --progress" -t $TIME -m $LOGS_DIR/src -o $LOGS_DIR/src &
SWIFT_SRC_PID=$!

echo "Starting destination in 5s..."
sleep 5s

# start destination swift
#$STAP_RUN -R -o $LOGS_DIR/swift.dst.stap.out -c "taskset -c 1 timeout 50s $DIR_SWIFT/swift -o $LFS_DST_STORE -t 127.0.0.1:1337 -h $HASH -z 8192 --progress -D$LOGS_DIR/swift.dst.debug" cpu_io_mem_2.ko >$LOGS_DIR/swift.dst.log 2>&1 &
touch $LFS_DST_STORE/aaaaaaaa_128gb_8192
$DIR_LFS/process_guard.py -c "taskset -c 1 $DIR_SWIFT/swift --downrate 307200 -o $LFS_DST_STORE -f aaaaaaaa_128gb_8192 -t 127.0.0.1:1337 -h $HASH -z 8192 --progress -D$LOGS_DIR/swift.dst.debug" -t $(($TIME-5)) -m $LOGS_DIR/dst -o $LOGS_DIR/dst &
SWIFT_DST_PID=$!

echo "Waiting for swifts to finish (~${TIME}s)..."
wait $SWIFT_SRC_PID
wait $SWIFT_DST_PID

echo "---------------------------------------------------------------------------------"

# check LFS storage
ls -alh $LFS_SRC_STORE
ls -alh $LFS_SRC_REALSTORE
ls -alh $LFS_DST_STORE
ls -alh $LFS_DST_REALSTORE

sleep 10s

# --------- EXPERIMENT END ----------
#kill -9 $SWIFT_SRC_PID $SWIFT_DST_PID || true
fusermount -z -u $LFS_SRC_STORE
fusermount -z -u $LFS_DST_STORE
#kill -9 $LFS_SRC_PID $LFS_DST_PID || true
#pkill -9 swift || true

sleep 5s

# separate logs

# remove temps
rm -rf $LFS_SRC_STORE
rm -rf $LFS_SRC_REALSTORE
rm -rf $LFS_DST_STORE
rm -rf $LFS_DST_REALSTORE
# rm -rf ./src ./dst # TODO

# ------------- LOG PARSING -------------

$DIR_LFS/experiment/parse_logs.py $LOGS_DIR/src
$DIR_LFS/experiment/parse_logs.py $LOGS_DIR/dst

# ------------- PLOTTING -------------
gnuplot -e "logdir='$LOGS_DIR/src';peername='src';plotsdir='$PLOTS_DIR'" $DIR_LFS/experiment/resource_usage.gnuplot
gnuplot -e "logdir='$LOGS_DIR/dst';peername='dst';plotsdir='$PLOTS_DIR'" $DIR_LFS/experiment/resource_usage.gnuplot

gnuplot -e "logdir='$LOGS_DIR';plotsdir='$PLOTS_DIR'" $DIR_LFS/experiment/speed.gnuplot

rm $PLOTS_DIR_LAST/*
cp $PLOTS_DIR/* $PLOTS_DIR_LAST/
