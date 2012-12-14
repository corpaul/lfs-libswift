#!/bin/bash

usage() {
	echo "Usage: $0 <path_to_mount_dir>"
}

[[ $# -lt 1 ]] && usage

SCRIPT_LOC="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
MOUNT_DIR=$1

cp $SCRIPT_LOC/testfile_100GiB.m* $MOUNT_DIR/
