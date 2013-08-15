#!/usr/bin/python

from sys import argv, stderr
from os import walk, path
from subprocess import Popen, PIPE, STDOUT
from threading import Thread

def call_swift(swiftbin, filename, chunksize, debug=False):
    cmd = [
        path.abspath(swiftbin),
        '-f', filename,
        '-z', chunksize,
        '-m'
    ]
    if debug:
        cmd += ['--debug']
    print '[ ] Spawning:', " ".join(cmd)
    process = Popen(cmd, stdout=PIPE, stderr=STDOUT)
    process.wait()

def content_files(dir):
    for _, _, files in walk(dir):
        for f in files:
            if (not f.endswith('.mhash')) and (not f.endswith('.mbinmap')):
                if content_files.i % 100 == 0:
                    print '[*] Files discovered so far:', content_files.i
                
                abspath = path.join(dir, f)
                content_files.i += 1
                yield (f, abspath) 
content_files.i = 0

if __name__ == '__main__':
    if len(argv) != 3:
        print >>stderr, \
            'Usage:', argv[0], '<lfsstore> <swiftbinary>'
        exit(1)

    lfsstore = argv[1]
    swiftbin = argv[2]
    
    for (f, abspath) in content_files(lfsstore):
        chunksize = f.split('_')[-1]
        call_swift(swiftbin, abspath, chunksize)
