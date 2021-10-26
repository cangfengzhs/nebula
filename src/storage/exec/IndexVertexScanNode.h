/* Copyright (c) 2021 vesoft inc. All rights reserved.
 *
 * This source code is licensed under Apache 2.0 License,
 * attached with Common Clause Condition 1.0, found in the LICENSES directory.
 */
#pragma once

#include <gtest/gtest_prod.h>

#include <functional>
#include <memory>

#include "common/base/Base.h"
#include "storage/exec/IndexScanNode.h"
#include "storage/exec/StorageIterator.h"

namespace nebula {
namespace storage {

/**
 * IndexVertexScanNode
 *
 * reference: IndexScanNode
 */
class IndexVertexScanNode final : public IndexScanNode {
 public:
  IndexVertexScanNode(const IndexVertexScanNode& node);
  IndexVertexScanNode(RuntimeContext* context,
                      IndexID indexId,
                      const std::vector<cpp2::IndexColumnHint>& clolumnHint,
                      ::nebula::kvstore::KVStore* kvstore);
  ::nebula::cpp2::ErrorCode init(InitContext& ctx) override;
  std::unique_ptr<IndexNode> copy() override;

 private:
  nebula::cpp2::ErrorCode getBaseData(folly::StringPiece key,
                                      std::pair<std::string, std::string>& kv) override;
  Row decodeFromIndex(folly::StringPiece key) override;
  Map<std::string, Value> decodeFromBase(const std::string& key, const std::string& value) override;
  const std::vector<std::shared_ptr<const meta::NebulaSchemaProvider>>& getSchema() override {
    return tag_;
  }

  std::vector<std::shared_ptr<const nebula::meta::NebulaSchemaProvider>> tag_;

  // Convenient for testing
  std::function<StatusOr<std::shared_ptr<::nebula::meta::cpp2::IndexItem>>()> getIndex;
  std::function<std::vector<std::shared_ptr<const meta::NebulaSchemaProvider>>()> getTag;

  FRIEND_TEST(IndexScanTest, VertexIndexOnlyScan);
  FRIEND_TEST(IndexScanTest, VertexBase);
  FRIEND_TEST(IndexScanTest, Prefix1);
  FRIEND_TEST(IndexScanTest, Prefix2);
  FRIEND_TEST(IndexScanTest, Base);
  FRIEND_TEST(IndexScanTest, Vertex);
  friend class IndexScanTestHelper;
};
}  // namespace storage
}  // namespace nebula
