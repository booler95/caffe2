#include "caffe2/core/db.h"
#include "caffe2/core/logging.h"
#include "caffe2/core/flags.h"
#include "rocksdb/db.h"
#include "rocksdb/utilities/leveldb_options.h"

CAFFE2_DEFINE_int(caffe2_rocksdb_block_size, 65536,
                  "The caffe2 rocksdb block size when writing a rocksdb.");

namespace caffe2 {
namespace db {

class RocksDBCursor : public Cursor {
 public:
  explicit RocksDBCursor(rocksdb::DB* db)
      : iter_(db->NewIterator(rocksdb::ReadOptions())) {
    SeekToFirst();
  }
  ~RocksDBCursor() {}
  void Seek(const string& key) override { iter_->Seek(key); }
  bool SupportsSeek() override { return true; }
  void SeekToFirst() override { iter_->SeekToFirst(); }
  void Next() override { iter_->Next(); }
  string key() override { return iter_->key().ToString(); }
  string value() override { return iter_->value().ToString(); }
  bool Valid() override { return iter_->Valid(); }

 private:
  std::unique_ptr<rocksdb::Iterator> iter_;
};

class RocksDBTransaction : public Transaction {
 public:
  explicit RocksDBTransaction(rocksdb::DB* db) : db_(db) {
    CHECK_NOTNULL(db_);
    batch_.reset(new rocksdb::WriteBatch());
  }
  ~RocksDBTransaction() { Commit(); }
  void Put(const string& key, const string& value) override {
    batch_->Put(key, value);
  }
  void Commit() override {
    rocksdb::Status status = db_->Write(rocksdb::WriteOptions(), batch_.get());
    batch_.reset(new rocksdb::WriteBatch());
    CHECK(status.ok()) << "Failed to write batch to rocksdb "
                       << std::endl << status.ToString();
  }

 private:
  rocksdb::DB* db_;
  std::unique_ptr<rocksdb::WriteBatch> batch_;

  DISABLE_COPY_AND_ASSIGN(RocksDBTransaction);
};

class RocksDB : public DB {
 public:
  RocksDB(const string& source, Mode mode) : DB(source, mode) {
    rocksdb::LevelDBOptions options;
    options.block_size = FLAGS_caffe2_rocksdb_block_size;
    options.write_buffer_size = 268435456;
    options.max_open_files = 100;
    options.error_if_exists = mode == NEW;
    options.create_if_missing = mode != READ;
    rocksdb::Options rocksdb_options = rocksdb::ConvertOptions(options);

    rocksdb::DB* db_temp;
    rocksdb::Status status = rocksdb::DB::Open(
      rocksdb_options, source, &db_temp);
    CAFFE_ENFORCE(
        status.ok(),
        "Failed to open rocksdb ",
        source,
        "\n",
        status.ToString());
    db_.reset(db_temp);
    VLOG(1) << "Opened rocksdb " << source;
  }

  void Close() override { db_.reset(); }
  unique_ptr<Cursor> NewCursor() override {
    return std::make_unique<RocksDBCursor>(db_.get());
  }
  unique_ptr<Transaction> NewTransaction() override {
    return std::make_unique<RocksDBTransaction>(db_.get());
  }

 private:
  std::unique_ptr<rocksdb::DB> db_;
};

REGISTER_CAFFE2_DB(RocksDB, RocksDB);
// For lazy-minded, one can also call with lower-case name.
REGISTER_CAFFE2_DB(rocksdb, RocksDB);

}  // namespace db
}  // namespace caffe2
