# Simple KV demo: path -> inode

This small demo shows a minimal file-backed key-value store where the key is a file path
and the value is a simple Inode struct. It's intended as a lightweight example you can
replace later with LevelDB/RocksDB or integrate with the project's on-disk inode format.

Files added
- `KVStore.h` - header with `mds::Inode` and `mds::KVStore` declarations
- `KVStore.cpp` - simple file-backed implementation (one file per key using std::hash)
- `demo_kv.cpp` - small program that writes/reads/deletes a sample inode


Build (fallback file-backed, no RocksDB)

cd /mnt/md0/Projects/ZBStorage/src/mds/metadataserver
g++ -std=c++17 -I/mnt/md0/Projects/ZBStorage/src KVStore.cpp demo_kv.cpp ../inode/inode.cpp ../inode/InodeTimestamp.cpp -o demo_kv

Run (fallback)

./demo_kv

Build with RocksDB backend (recommended)

Prerequisite: install RocksDB development headers and library. On Debian/Ubuntu you can try:

sudo apt-get update
sudo apt-get install -y librocksdb-dev

Then compile with USE_ROCKSDB and link against rocksdb:

cd /mnt/md0/Projects/ZBStorage/src/mds/metadataserver
g++ -std=c++17 -DUSE_ROCKSDB -I/mnt/md0/Projects/ZBStorage/src KVStore.cpp demo_kv.cpp ../inode/inode.cpp ../inode/InodeTimestamp.cpp -lrocksdb -o demo_kv_rocksdb

Run (RocksDB)

./demo_kv_rocksdb

Notes
- The code uses `::Inode::serialize()` / `::Inode::deserialize()` for value encoding, so stored values are binary-compatible with the project's inode format.
- If RocksDB is not available, the code falls back to the simple file-per-key implementation; to force RocksDB you must compile with `-DUSE_ROCKSDB` and have RocksDB installed.


Notes
- This demo writes files under `/tmp/zbstorage_kv` by default. You can change `storage_dir`
  in `demo_kv.cpp` or pass configuration in a future enhancement.
- The `Inode` layout here is a plain POD binary blob for simplicity. To interoperate with
  existing on-disk inode formats, adapt the serialization/deserialization accordingly.
