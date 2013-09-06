#!/usr/bin/env python

import os
from numpy import mean, std
from scipy.stats import cmedian 

DIR = "./times"

#print "Chunk Size, LFS Mean, LFS StdDev, LFS Median, ext4 Mean, ext4 StdDev, ext4 Median, LFS (kcache) Mean, LFS (kcache) StdDev, LFS (kcache) Median"

files = sorted(os.listdir(DIR), key=lambda x: int(x.split(".")[1]))
files = [x for x in files if x.startswith("nokcache")]
for filename in files:
	fn = os.path.join(DIR, filename)
	fn2 = os.path.join(DIR, filename[2:])

	a = [map(float, x.split(" ")) for x in open(fn).read().split("\n")[:-1]]
	b = [map(float, x.split(" ")) for x in open(fn2).read().split("\n")[:-1]]
	
	chunk = filename[9:] if int(filename[9:]) < 1024 else str(int(filename[9:])/1024) + "K"
	print ','.join(map(str, [chunk, mean(a[0]), std(a[0]), cmedian(a[0]), mean(a[1]), std(a[1]), cmedian(a[1]), mean(b[0]), std(b[0]), cmedian(b[0])]))
