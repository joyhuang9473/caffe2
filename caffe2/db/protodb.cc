#include <unordered_set>

#include "caffe2/core/db.h"
#include "caffe2/utils/proto_utils.h"
#include "caffe2/core/logging.h"

namespace caffe2 {
namespace db {

class ProtoDBCursor : public Cursor {
 public:
  explicit ProtoDBCursor(const TensorProtos* proto)
    : proto_(proto), iter_(0) {}
  ~ProtoDBCursor() {}
  void SeekToFirst() override { iter_ = 0; }
  void Next() override { ++iter_; }
  string key() override { return proto_->protos(iter_).name(); }
  string value() override { return proto_->protos(iter_).SerializeAsString(); }
  bool Valid() override { return iter_ < proto_->protos_size(); }

 private:
  const TensorProtos* proto_;
  int iter_;
};

class ProtoDBTransaction : public Transaction {
 public:
  explicit ProtoDBTransaction(TensorProtos* proto)
      : proto_(proto), existing_names_() {
    for (const auto& tensor : proto_->protos()) {
      existing_names_.insert(tensor.name());
    }
  }
  ~ProtoDBTransaction() { Commit(); }
  void Put(const string& key, const string& value) override {
    if (existing_names_.count(key)) {
      CAFFE_LOG_FATAL << "An item with key " << key << " already exists.";
    }
    auto* tensor = proto_->add_protos();
    CAFFE_CHECK(tensor->ParseFromString(value));
    CAFFE_CHECK_EQ(tensor->name(), key)
        << "Passed in key " << key << " does not equal to the tensor name "
        << tensor->name();
  }
  // Commit does nothing. The protocol buffer will be written at destruction
  // of ProtoDB.
  void Commit() override {}

 private:
  TensorProtos* proto_;
  std::unordered_set<string> existing_names_;

  DISABLE_COPY_AND_ASSIGN(ProtoDBTransaction);
};

class ProtoDB : public DB {
 public:
  ProtoDB(const string& source, Mode mode)
      : DB(source, mode), proto_(), source_(source) {
    if (mode == READ || mode == WRITE) {
      // Read the current protobuffer.
      CAFFE_CHECK(ReadProtoFromFile(source, &proto_));
    }
    CAFFE_LOG_INFO << "Opened protodb " << source;
  }
  ~ProtoDB() { Close(); }

  void Close() override {
    if (mode_ == NEW || mode_ == WRITE) {
      WriteProtoToBinaryFile(proto_, source_);
    }
  }

  Cursor* NewCursor() override {
    return new ProtoDBCursor(&proto_);
  }
  Transaction* NewTransaction() override {
    return new ProtoDBTransaction(&proto_);
  }

 private:
  TensorProtos proto_;
  string source_;
};

REGISTER_CAFFE2_DB(ProtoDB, ProtoDB);
// For lazy-minded, one can also call with lower-case name.
REGISTER_CAFFE2_DB(protodb, ProtoDB);

}  // namespace db
}  // namespace caffe2
