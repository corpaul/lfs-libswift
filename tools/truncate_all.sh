#!/bin/bash

#truncate -s 1TiB ./haha/aaaaaaa1_1024gb_8192
#truncate -s 1TiB ./haha/aaaaaaa2_1024gb_8192
#truncate -s 512GiB ./haha/bbbbbbb1_512gb_8192
#truncate -s 512GiB ./haha/bbbbbbb2_512gb_8192
truncate -s 256GiB ./haha/ccccccc1_256gb_8192
truncate -s 256GiB ./haha/ccccccc2_256gb_4096
truncate -s 128GiB ./haha/ddddddd1_128gb_8192
truncate -s 128GiB ./haha/ddddddd2_128gb_4096

