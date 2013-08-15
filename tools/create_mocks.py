#!/usr/bin/python

from sys import argv, stderr
from os import walk, path
from random import randint, sample

def get_existing_files(lfsstore, size, chunksize):
    existing = []
    for _, _, files in walk(lfsstore):
        for f in files:
            [pattern, s, cs] = f.split('_')
            if s == size and cs == chunksize:
                existing += [pattern.lower()]
    return existing

def rand_pattern():
    p = hex(randint(0x0000000, 0xFFFFFFFF))[2:].lower()
    if len(p) < 8:
        p = '0' * (8 - len(p)) + p
    return p

def gen_random_patterns(existing, nfiles):
    print '\tGenerating sample of', nfiles, 'unique patterns.'
    
    selected = set()
    for e in existing:
        selected.add(e)
    
    patterns = []
    inforate = nfiles / 10 if nfiles / 10 > 0 else 1
    while nfiles:
        if nfiles % inforate == 0:
            print '\tPatterns remaining:', nfiles
        p = rand_pattern()
        while p in selected:
            p = rand_pattern()

        selected.add(p)
        patterns += [p]
        nfiles -= 1
        
    return patterns

SIZE_MULTIPLIERS = {
    'kb': 1024,
    'mb': 1024 * 1024,
    'gb': 1024 * 1024 * 1024,
    'tb': 1024 * 1024 * 1024 * 1024
}

def parse_size(size):
    return int(size[:-2]) * SIZE_MULTIPLIERS[size[-2:].lower()]

def compact_size(bytes):
    bytes = int(bytes)
    mmax = 0
    for (s, m) in SIZE_MULTIPLIERS.items():
        if (bytes / m) == int((bytes / m)) and (bytes / m) > 0 and mmax < m:
            mmax = m
            r = s
    return str((bytes / m / 1024)) + r

if __name__ == '__main__':
    if len(argv) != 5:
        print >>stderr, \
            'Usage:', argv[0], '<lfsstore> <nfiles> <size> <chunksize>'
        exit(1)

    lfsstore = argv[1]
    nfiles = int(argv[2])
    size = argv[3]
    chunksize = argv[4]

    e = get_existing_files(lfsstore, size, chunksize)
    
    # generate patterns
    print 'Generating patterns...'
    patterns = gen_random_patterns(e, nfiles)
    
    # create the new files
    print 'Creating files...'
    inforate = nfiles / 10 if nfiles / 10 > 0 else 1
    for (i, p) in enumerate(patterns):
        if i % inforate == 0:
            print '\tFiles created so far:', i
        filename = '_'.join([p, size, chunksize])
        f = open(path.join(lfsstore, filename), 'w')
        f.truncate(parse_size(size))
        f.close()
