#!/usr/bin/env python

from base64 import b64decode, b64encode
from zlib import compress, decompress

class MetaDB:
    def __init__(self, filename):
        self.filename = filename
        self.db = {}

    def load(self):
        try:
            with open(self.filename, "r") as f:
                self._read_entries(f)
        except IOError:
            print "[!] metadb file not found or bad."

    def persist(self):
        with open(self.filename, "w") as f:
            self._write_entries(f)

    def _write_entries(self, fd):
        for (key, value) in self.db.items():
            (mbinmap, mhash) = value
            [pattern, size, chunk] = key.split("_")
            line = "%s %s %s %s %s\n" % (
                pattern, size, chunk, 
                self._encode(mbinmap), self._encode(mhash)
            )
            fd.write(line)

    def _read_entries(self, fd):
        for line in fd.readlines():
            [pattern, size, chunk, mbinmap, mhash] = line.split(" ")
            key = "%s_%s_%s" % (pattern, size, chunk)
            self.db[key] = (self._decode(mbinmap), self._decode(mhash))

    def add(self, key, mbinmap, mhash):
        self.db[key] = (mbinmap, mhash)

    def _decode(self, s):
        return decompress(b64decode(s))

    def _encode(self, s):
        return b64encode(compress(s))

    def __repr__(self):
        r = ""
        for (key, value) in self.db.items():
            r += "%s %s %s\n" % (key, value[0], value[1])
        return r

def restore_metafiles(metadb, dir):
    # TODO: Restore meta files from DB.
    pass

SWIFTBINARY = "./swift"

if __name__ == "__main__":
    import sys
    import os

    from precompute_meta import call_swift, content_files
    from create_mocks import parse_size

    if len(sys.argv) != 3:
        print >>sys.stderr, "Usage: %s <files_dir> <metadb_file>" % sys.argv[0]
        exit(1)
    if not os.path.exists(SWIFTBINARY):
        print >>sys.stderr, "[-] Cannot find swift binary at:", SWIFTBINARY
        exit(1)

    files_dir = os.path.abspath(sys.argv[1])
    metadb_file = os.path.abspath(sys.argv[2])
    print "[*] Computing hashes for files in dir:", files_dir
    print "[*] Saving libswift metadata in metadb file:", metadb_file

    mdb = MetaDB(metadb_file)
    mdb.load()

    for (f, abspath) in content_files(files_dir):
        try:
            print "[+] Computing roothash for file:", abspath
            
            [pattern, size, chunk] = f.split("_")
            # TODO: Move this size normalization to a method.
            key = "%s_%s_%s" % (pattern, parse_size(size), chunk)

            if key in mdb.db:
                print "[!] Roothash already in DB. Moving on."
                continue

            chunksize = f.split("_")[-1]
            call_swift(SWIFTBINARY, abspath, chunksize)

            # read metadata files
            with open(abspath + ".mbinmap", "rb") as f:
                mbinmap = f.read()
            with open(abspath + ".mhash", "rb") as f:
                mhash = f.read()

            # save in db
            mdb.add(key, mbinmap, mhash)
        except Exception as e:
            # ultra-super-resilient system we have here
            import traceback
            traceback.print_exc()
            print >>sys.stderr, "[!] libswift failed, but moving on"
            pass

    mdb.persist()
