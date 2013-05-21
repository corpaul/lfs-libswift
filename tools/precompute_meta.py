#!/usr/bin/python

from sys import argv, stderr
from os import walk, path
from subprocess import Popen, PIPE, STDOUT
from threading import Thread

def call_swift(swiftbin, filename, chunksize):
    cmd = [
        path.abspath(swiftbin),
        '-f', filename,
        '-z', chunksize,
        '-m'
    ]
    print 'spawning:', cmd
    process = Popen(cmd, stdout=PIPE, stderr=STDOUT)
    process.wait()

if __name__ == '__main__':
    if len(argv) != 3:
        print >>stderr, \
            'Usage:', argv[0], '<lfsstore> <swiftbinary>'
        exit(1)

    lfsstore = argv[1]
    swiftbin = argv[2]
    
    i = 0
    for _, _, files in walk(lfsstore):
        for f in files:
            if (not f.endswith('.mhash')) and (not f.endswith('.mbinmap')):
                if i % 100 == 0:
                    print 'Files read so far:', i
                
                filename = path.join(lfsstore, f)
                chunksize = f.split('_')[-1]
                call_swift(swiftbin, filename, chunksize)
                i += 1
