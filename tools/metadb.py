#!/usr/bin/env python

from base64 import b64decode, b64encode
from zlib import compress, decompress
from struct import pack, unpack

SWIFTBINARY = "./swift"

class MetaDB:
    def __init__(self, filename):
        self.filename = filename
        self.db = {}

    def load(self):
        try:
            with open(self.filename, "r") as f:
                for line in f.readlines():
                    [pattern, size, chunk, mbinmap, mhash] = line.split(" ")
                    key = "%s_%s_%s" % (pattern, size, chunk)
                    self.db[key] = (self._decode(mbinmap), self._decode(mhash))
        except IOError:
            print "[!] Metadb file not found or cannot be opened. Loading empty DB"

    def persist(self):
        with open(self.filename, "w") as f:
            for (key, value) in self.db.items():
                (mbinmap, mhash) = value
                [pattern, size, chunk] = key.split("_")
                line = "%s %s %s %s %s\n" % (
                    pattern, size, chunk, 
                    self._encode(mbinmap), self._encode(mhash)
                )
                f.write(line)

    def add(self, key, path_mbinmap, path_mhash, abspath):
        if not os.path.exists(path_mbinmap) or not os.path.exists(path_mhash):
            # libswift didn't previously run
            chunksize = key.split("_")[-1]
            call_swift(SWIFTBINARY, abspath, chunksize, debug=True)
        else:
            print "[-] Metadata files already present"

        # read metadata files
        with open(abspath + ".mbinmap", "rb") as f:
            mbinmap = f.read()
        with open(abspath + ".mhash", "rb") as f:
            mhash = f.read()

        self.db[key] = (self._encode(mbinmap), self._encode(mhash))

    # TODO(vladum): Retrieval methods.

    def _decode(self, s):
        return decompress(b64decode(s))

    def _encode(self, s):
        return b64encode(compress(s))

    def __repr__(self):
        r = ""
        for (key, value) in self.db.items():
            r += "%s %s %s\n" % (key, value[0], value[1])
        return r

class BinaryMetaDB(MetaDB):
    def load(self):
        try:
            with open(self.filename, "rb") as f:
                for pattern in iter(lambda: f.read(8), ""):
                    size = unpack("<Q", f.read(8))[0]
                    chunk = unpack("<I", f.read(4))[0]
                    mbinmap_size = unpack("<Q", f.read(8))[0]
                    mhash_size = unpack("<Q", f.read(8))[0]
                    mbinmap = f.read(mbinmap_size)
                    mhash = f.read(mhash_size)

                    e = [size, chunk, mbinmap_size, mhash_size, mbinmap, mhash]
                    if not all(e):
                        # entry is corrupted
                        print "[!] Entry corrupted:", e
                        continue

                    key = "%s_%s_%s" % (pattern, size, chunk)
                    self.db[key] = (mbinmap, mhash)
        except IOError:
            print "[!] Metadb file not found or cannot be opened. Loading empty DB"

    def persist(self):
        with open(self.filename, "wb") as f:
            for (key, value) in self.db.items():
                (mbinmap, mhash) = value
                [pattern, size, chunk] = key.split("_")

                f.write(pattern) # 8 chars (8 bytes)
                f.write(pack("<Q", int(size))) # size in bytes (8 bytes)
                f.write(pack("<I", int(chunk))) # chunck size (4 bytes)
                f.write(pack("<Q", len(mbinmap))) # mbinmap size (8 bytes)
                f.write(pack("<Q", len(mhash))) # mhash size (8 bytes)
                f.write(mbinmap)
                f.write(mhash)

    def _encode(self, s):
        pass

    def _decode(self, s):
        pass

class NoStateCompressedMetaDB(BinaryMetaDB):
    def load(self):
        pass

    def persist(self):
        pass

def restore_metafiles(metadb_file, files_dir):
    from create_mocks import compact_size

    print "[*] Restoring to dir:", files_dir
    print "[*] Reading from libswift metadata file:", metadb_file

    mdb = BinaryMetaDB(metadb_file)
    mdb.load()

    for (key, value) in mdb.db.items():
        [pattern, size, chunk] = key.split("_")
        size = compact_size(size)

        dst_file_prefix = os.path.join(files_dir,
            "%s_%s_%s" % (pattern, size, chunk))
        mbinmap_file = dst_file_prefix + ".mbinmap"
        mhash_file = dst_file_prefix + ".mhash"

        (mbinmap, mhash) = value

        print "[+] Writing", mbinmap_file
        with open(mbinmap_file, "wb") as f:
            f.write(mbinmap)

        print "[+] Writing", mhash_file
        with open(mhash_file, "wb") as f:
            f.write(mhash)

if __name__ == "__main__":
    import sys
    import os

    from precompute_meta import call_swift, content_files
    from create_mocks import parse_size

    if len(sys.argv) != 3:
        if len(sys.argv) == 4 and sys.argv[1] == "restore":
            # restore
            files_dir = os.path.abspath(sys.argv[2])
            metadb_file = os.path.abspath(sys.argv[3])
            restore_metafiles(metadb_file, files_dir)
            exit(0)
        else:
            print >>sys.stderr, "Usage: %s [restore] <files_dir> <metadb_file>" % sys.argv[0]
            exit(1)
    if not os.path.exists(SWIFTBINARY):
        print >>sys.stderr, "[-] Cannot find swift binary at:", SWIFTBINARY
        exit(1)

    files_dir = os.path.abspath(sys.argv[1])
    metadb_file = os.path.abspath(sys.argv[2])
    print "[*] Computing hashes for files in dir:", files_dir
    print "[*] Saving libswift metadata in metadb file:", metadb_file

    mdb = BinaryMetaDB(metadb_file)
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

            path_mbinmap = abspath + ".mbinmap"
            path_mhash = abspath + ".mhash"

            # save in db
            mdb.add(key, path_mbinmap, path_mhash, abspath)
        except Exception as e:
            # ultra-super-resilient system we have here
            import traceback
            traceback.print_exc()
            print >>sys.stderr, "[!] libswift failed, but moving on"
            pass

    mdb.persist()
