/* Copyright (c) 2022 vesoft inc. All rights reserved.
 *
 * This source code is licensed under Apache 2.0 License.
 */

#include "common/plugin/fulltext/elasticsearch/ESClient.h"

#include "common/http/HttpClient.h"
namespace nebula::plugin {

ESClient::ESClient(const std::string& protocol,
                   const std::string& address,
                   const std::string& user,
                   const std::string& password)
    : protocol_(protocol), address_(address), user_(user), password_(password) {
  // TODO(hs.zhang): enable protocol
  // TODO(hs.zhang): enable user&password
}

StatusOr<folly::dynamic> ESClient::createIndex(const std::string& name,
                                               const folly::dynamic& body) {
  std::string url = fmt::format("http://{}/{}", address_, name);
  auto resp = HttpClient::put(url, {"Content-Type: application/json"}, folly::toJson(body));
  if (resp.curlCode != 0) {
    return Status::Error(fmt::format("curl error({}):{}", resp.curlCode, resp.curlMessage));
  }
  auto ret = folly::parseJson(resp.body);
  return ret;
}

StatusOr<folly::dynamic> ESClient::dropIndex(const std::string& name) {
  std::string url = fmt::format("http://{}/{}", address_, name);
  auto resp = HttpClient::delete_(url, {"Content-Type: application/json"});
  if (resp.curlCode != 0) {
    return Status::Error(fmt::format("curl error({}):{}", resp.curlCode, resp.curlMessage));
  }
  return folly::parseJson(resp.body);
}

StatusOr<folly::dynamic> ESClient::clearIndex(const std::string& name) {
  std::string url = fmt::format("http://{}/{}/_delete_by_query?refresh", address_, name);
  std::string body = R"(
    {
      "query":{
        "match_all":{}
      }
    }
  )";
  auto resp = HttpClient::post(url, {"Content-Type: application/json"}, body);
  if (resp.curlCode != 0) {
    return Status::Error(fmt::format("curl error({}):{}", resp.curlCode, resp.curlMessage));
  }
  return folly::parseJson(resp.body);
}

StatusOr<folly::dynamic> ESClient::getIndex(const std::string& name) {
  std::string url = fmt::format("http://{}/{}", address_, name);
  auto resp = HttpClient::get(url, {"Content-Type: application/json"});
  if (resp.curlCode != 0) {
    return Status::Error(fmt::format("curl error({}):{}", resp.curlCode, resp.curlMessage));
  }
  return folly::parseJson(resp.body);
}

StatusOr<folly::dynamic> ESClient::deleteByQuery(const std::string& index,
                                                 const folly::dynamic& query) {
  std::string url = fmt::format("http://{}/{}/_delete_by_query", address_, index);
  auto resp = HttpClient::post(url, {"Content-Type: application/json"}, folly::toJson(query));
  if (resp.curlCode != 0) {
    return Status::Error(fmt::format("curl error({}):{}", resp.curlCode, resp.curlMessage));
  }
  return folly::parseJson(resp.body);
}

StatusOr<folly::dynamic> ESClient::updateByQuery(const std::string& index,
                                                 const folly::dynamic& query) {
  std::string url = fmt::format("http://{}/{}/_update_by_query", address_, index);
  auto resp = HttpClient::post(url, {"Content-Type: application/json"}, folly::toJson(query));
  if (resp.curlCode != 0) {
    return Status::Error(fmt::format("curl error({}):{}", resp.curlCode, resp.curlMessage));
  }
  return folly::parseJson(resp.body);
}

// StatusOr<folly::dynamic> ESClient::put(const std::string& index, const folly::dynamic& document)
// {
//   std::string url = fmt::format("http://{}/{}/_doc", address_, index);
//   auto resp = HttpClient::post(url, {"Content-Type: application/json"}, folly::toJson(document));
//   if (resp.curlCode != 0) {
//     return Status::Error(fmt::format("curl error({}):{}", resp.curlCode, resp.curlMessage));
//   }
//   return folly::parseJson(resp.body);
// }

StatusOr<folly::dynamic> ESClient::search(const std::string& index, const folly::dynamic& query) {
  std::string url = fmt::format("http://{}/{}/_search", address_, index);
  auto resp = HttpClient::post(url, {"Content-Type: application/json"}, folly::toJson(query));
  if (resp.curlCode != 0) {
    return Status::Error(fmt::format("curl error({}):{}", resp.curlCode, resp.curlMessage));
  }
  return folly::parseJson(resp.body);
}

StatusOr<folly::dynamic> ESClient::bulk(const std::vector<folly::dynamic>& bulk) {
  std::string url = fmt::format("http://{}/_bulk", address_);
  std::string body;
  for (auto& obj : bulk) {
    body += folly::toJson(obj);
    body += "\n";
  }
  auto resp = HttpClient::post(url, {"Content-Type: application/x-ndjson"}, body);
  if (resp.curlCode != 0) {
    return Status::Error(fmt::format("curl error({}):{}", resp.curlCode, resp.curlMessage));
  }
  return folly::parseJson(resp.body);
}

}  // namespace nebula::plugin
