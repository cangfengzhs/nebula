/* Copyright (c) 2022 vesoft inc. All rights reserved.
 *
 * This source code is licensed under Apache 2.0 License.
 */

#ifndef COMMON_PLUGIN_FULLTEXT_ELASTICSEARCH_ESADAPTER_H_
#define COMMON_PLUGIN_FULLTEXT_ELASTICSEARCH_ESADAPTER_H_

#include "common/base/StatusOr.h"
#include "common/plugin/fulltext/elasticsearch/ESClient.h"
#include "folly/container/F14Map.h"
#include "folly/dynamic.h"
namespace nebula::plugin {

struct ESQueryResult {
  struct Item {
    std::string text;
    std::string vid;       // for vertex
    std::string src, dst;  // for edge
    int64_t rank;          // for edge
  };
  std::vector<Item> items;
};

class ESBulk {
 public:
  void put(const std::string& indexName,
           const std::string& vid,
           const std::string& src,
           const std::string& dst,
           int64_t rank,
           const std::string& text);
  void delete_(const std::string& indexName,
               const std::string& vid,
               const std::string& src,
               const std::string& dst,
               int64_t rank);

  bool empty();

 private:
  folly::F14FastMap<std::string, std::vector<folly::dynamic>> documents_;
  friend class ESAdapter;
};

class ESAdapter {
 public:
  explicit ESAdapter(std::vector<ESClient>&& clients);
  ESAdapter() = default;
  void setClients(std::vector<ESClient>&& clients);
  Status createIndex(const std::string& name);
  Status dropIndex(const std::string& name);
  Status clearIndex(const std::string& name);
  StatusOr<bool> isIndexExist(const std::string& name);

  Status bulk(const ESBulk& bulk);
  StatusOr<ESQueryResult> prefix(const std::string& index, const std::string& pattern);
  StatusOr<ESQueryResult> fuzzy(const std::string& index, const std::string& pattern);
  StatusOr<ESQueryResult> regexp(const std::string& index, const std::string& pattern);
  StatusOr<ESQueryResult> wildcard(const std::string& index, const std::string& pattern);
  StatusOr<ESQueryResult> query(const std::string& index, const folly::dynamic& query);

 private:
  static std::string genDocID(const std::string& vid,
                              const std::string& src,
                              const std::string& dst,
                              int64_t rank);

  ESClient& randomClient();
  std::vector<ESClient> clients_;

  friend class ESBulk;
};

}  // namespace nebula::plugin

#endif
