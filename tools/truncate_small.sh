#!/bin/bash

#truncate -s 1TiB ./haha/aaaaaaa1_1024gb_8192
#truncate -s 1TiB ./haha/aaaaaaa2_1024gb_8192
#truncate -s 512GiB ./haha/bbbbbbb1_512gb_8192
#truncate -s 512GiB ./haha/bbbbbbb2_512gb_8192
truncate -s 1GiB ./haha/ccccccc1_1gb_8192
truncate -s 1GiB ./haha/ccccccc2_1gb_4096
truncate -s 2GiB ./haha/ddddddd1_2gb_8192
truncate -s 2GiB ./haha/ddddddd2_2gb_4096

