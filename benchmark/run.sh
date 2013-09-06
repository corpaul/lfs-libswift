#!/bin/bash

# Usage: ./run.sh [size] [dir] [pattern]
#
# You can also export SIZE, DIR, PATTERN before running

[ -z $SIZE ] && SIZE=${1:-$((1024*1024))}
[ -z $DIR ] && DIR=${2:-"./test"}
[ -z $DIR_REAL ] && DIR_REAL=${2:-"./real"}
[ -z $PATTERN ] && PATTERN=${3:-"aaaaaaaa"}
[ -z $LFS ] && LFS=./lfs

echo "File size: $SIZE bytes"
echo "Directory: $DIR"
echo "Pattern: $PATTERN"

rm ./times/*
mkdir -p ./times

# LFS with kernel cache
mkdir -p $DIR
mkdir -p ${DIR}_real
mkdir -p $DIR_REAL
taskset -c 0 $LFS -o realstore=${DIR}_real $DIR
truncate -s $SIZE $DIR/${PATTERN}_test
ls -alh $DIR
cp $DIR/${PATTERN}_test $DIR_REAL
for cs in 32 40 64 128 256 512 1024 2048 3072 4096 8192 16384 32768 65536; do
	taskset -c 1 ./main $DIR ${PATTERN}_test $cs $DIR_REAL 1>./times/nokcache.$cs
done
sleep 1s
fusermount -u $DIR
rm ${DIR}_real/*
rm $DIR_REAL/*
rmdir $DIR ${DIR}_real $DIR_REAL

# LFS kernel cache
mkdir -p $DIR
mkdir -p ${DIR}_real
mkdir -p $DIR_REAL
taskset -c 0 $LFS -o realstore=${DIR}_real,kernel_cache $DIR
truncate -s $SIZE $DIR/${PATTERN}_test
ls -alh $DIR
cp $DIR/${PATTERN}_test $DIR_REAL
for cs in 32 40 64 128 256 512 1024 2048 3072 4096 8192 16384 32768 65536; do
	taskset -c 1 ./main $DIR ${PATTERN}_test $cs $DIR_REAL 1>./times/kcache.$cs
done
sleep 1s
fusermount -u $DIR
rm ${DIR}_real/*
rm $DIR_REAL/*
rmdir $DIR ${DIR}_real $DIR_REAL

./stats.py > reads.stats
cat reads.stats | head -4 > reads.stats.1
cat reads.stats | tail -9 > reads.stats.2

#rm ./times/*
#rmdir ./times
