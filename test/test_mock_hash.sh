#!/bin/bash

usage() {
	echo "Usage: $0 <path_to_swift_executable> <path_to_lfs_executable>"
}

[[ $# -lt 2 ]] && usage 

SWIFT_EXEC=$1
LFS_EXEC=$2
SCRIPT_LOC="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

for SIZE in $(seq 100)
do
	DIR=~/lazydir_$SIZE

	mkdir $DIR > /dev/null
	$LFS_EXEC $DIR
	truncate $DIR/testfile --size ${SIZE}MiB
	$SCRIPT_LOC/test_mock_hash.expect "$SWIFT_EXEC -H -d $DIR -l 18000 -B -p -z 65536" "swift: Mainloop" > /dev/null
	cat $DIR/testfile.mbinmap | grep "root hash"
	
	sleep 1
	fusermount -u $DIR
	rmdir $DIR
done
