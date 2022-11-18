/* Copyright (c) 2020 vesoft inc. All rights reserved.
 *
 * This source code is licensed under Apache 2.0 License.
 */

#include "kvstore/plugins/elasticsearch/ESListener.h"

#include "common/plugin/fulltext/elasticsearch/ESAdapter.h"
#include "common/utils/NebulaKeyUtils.h"

DECLARE_uint32(ft_request_retry_times);
DECLARE_int32(ft_bulk_batch_size);

namespace nebula {
namespace kvstore {
void ESListener::init() {
  auto vRet = schemaMan_->getSpaceVidLen(spaceId_);
  if (!vRet.ok()) {
    LOG(FATAL) << "vid length error";
  }
  vIdLen_ = vRet.value();

  auto cRet = schemaMan_->getServiceClients(meta::cpp2::ExternalServiceType::ELASTICSEARCH);
  if (!cRet.ok() || cRet.value().empty()) {
    LOG(FATAL) << "elasticsearch clients error";
  }
  std::vector<nebula::plugin::ESClient> esClients;
  for (const auto& c : cRet.value()) {
    auto host = c.host;
    std::string user, password;
    if (c.user_ref().has_value()) {
      user = *c.user_ref();
      password = *c.pwd_ref();
    }
    std::string protocol = c.conn_type_ref().has_value() ? *c.get_conn_type() : "http";
    esClients.emplace_back(protocol, host.toString(), user, password);
  }
  esAdapter_.setClients(std::move(esClients));
  auto sRet = schemaMan_->toGraphSpaceName(spaceId_);
  if (!sRet.ok()) {
    LOG(FATAL) << "space name error";
  }
  spaceName_ = std::make_unique<std::string>(sRet.value());
}

bool ESListener::apply(const BatchHolder& batch) {
  nebula::plugin::ESBulk bulk;
  auto callback = [&bulk](BatchLogType type,
                          const std::string& index,
                          const std::string& vid,
                          const std::string& src,
                          const std::string& dst,
                          int64_t rank,
                          const std::string& text) {
    if (type == BatchLogType::OP_BATCH_PUT) {
      bulk.put(index, vid, src, dst, rank, text);
    } else if (type == BatchLogType::OP_BATCH_REMOVE) {
      bulk.delete_(index, vid, src, dst, rank);
    } else {
      LOG(FATAL) << "Unexpect";
    }
  };
  for (const auto& log : batch.getBatch()) {
    pickTagAndEdgeData(std::get<0>(log), std::get<1>(log), std::get<2>(log), callback);
  }
  if (!bulk.empty()) {
    auto status = esAdapter_.bulk(bulk);
    if (!status.ok()) {
      LOG(ERROR) << status;
      return false;
    }
  }
  return true;
}

void ESListener::pickTagAndEdgeData(BatchLogType type,
                                    const std::string& key,
                                    const std::string& value,
                                    const PickFunc& callback) {
  if (nebula::NebulaKeyUtils::isTag(vIdLen_, key)) {
    auto tagId = NebulaKeyUtils::getTagId(vIdLen_, key);
    auto ftIndexRes = schemaMan_->getFTIndex(spaceId_, tagId);
    if (!ftIndexRes.ok()) {
      return;
    }
    auto ftIndex = std::move(ftIndexRes).value();
    auto reader = RowReaderWrapper::getTagPropReader(schemaMan_, spaceId_, tagId, value);
    if (reader == nullptr) {
      LOG(ERROR) << "get tag reader failed, tagID " << tagId;
      return;
    }
    if (ftIndex.second.get_fields().size() > 1) {
      LOG(ERROR) << "Only one field will create fulltext index";
    }
    auto field = ftIndex.second.get_fields().front();
    auto v = reader->getValueByName(field);
    if (v.type() != Value::Type::STRING) {
      LOG(ERROR) << "Can't create fulltext index on type " << v.type();
    }
    std::string indexName = ftIndex.first;
    std::string vid = NebulaKeyUtils::getVertexId(vIdLen_, key).toString();
    std::string text = std::move(v).getStr();
    callback(type, indexName, vid, "", "", 0, text);
  } else if (nebula::NebulaKeyUtils::isEdge(vIdLen_, key)) {
    auto edgeType = NebulaKeyUtils::getEdgeType(vIdLen_, key);
    auto ftIndexRes = schemaMan_->getFTIndex(spaceId_, edgeType);
    if (!ftIndexRes.ok()) {
      return;
    }
    auto ftIndex = std::move(ftIndexRes).value();
    auto reader = RowReaderWrapper::getEdgePropReader(schemaMan_, spaceId_, edgeType, value);
    if (reader == nullptr) {
      LOG(ERROR) << "get edge reader failed, schema ID " << edgeType;
      return;
    }
    if (ftIndex.second.get_fields().size() > 1) {
      LOG(ERROR) << "Only one field will create fulltext index";
    }
    auto field = ftIndex.second.get_fields().front();
    auto v = reader->getValueByName(field);
    if (v.type() != Value::Type::STRING) {
      LOG(ERROR) << "Can't create fulltext index on type " << v.type();
    }
    std::string indexName = ftIndex.first;
    std::string src = NebulaKeyUtils::getSrcId(vIdLen_, key).toString();
    std::string dst = NebulaKeyUtils::getDstId(vIdLen_, key).toString();
    int64_t rank = NebulaKeyUtils::getRank(vIdLen_, key);
    std::string text = std::move(v).getStr();
    callback(type, indexName, "", src, dst, rank, text);
  }
}

bool ESListener::persist(LogID lastId, TermID lastTerm, LogID lastApplyLogId) {
  if (!writeAppliedId(lastId, lastTerm, lastApplyLogId)) {
    LOG(FATAL) << "last apply ids write failed";
  }
  return true;
}

std::pair<LogID, TermID> ESListener::lastCommittedLogId() {
  if (access(lastApplyLogFile_->c_str(), 0) != 0) {
    VLOG(3) << "Invalid or nonexistent file : " << *lastApplyLogFile_;
    return {0, 0};
  }
  int32_t fd = open(lastApplyLogFile_->c_str(), O_RDONLY);
  if (fd < 0) {
    LOG(FATAL) << "Failed to open the file \"" << lastApplyLogFile_->c_str() << "\" (" << errno
               << "): " << strerror(errno);
  }
  // read last logId from listener wal file.
  LogID logId;
  CHECK_EQ(pread(fd, reinterpret_cast<char*>(&logId), sizeof(LogID), 0),
           static_cast<ssize_t>(sizeof(LogID)));

  // read last termId from listener wal file.
  TermID termId;
  CHECK_EQ(pread(fd, reinterpret_cast<char*>(&termId), sizeof(TermID), sizeof(LogID)),
           static_cast<ssize_t>(sizeof(TermID)));
  close(fd);
  return {logId, termId};
}

LogID ESListener::lastApplyLogId() {
  if (access(lastApplyLogFile_->c_str(), 0) != 0) {
    VLOG(3) << "Invalid or nonexistent file : " << *lastApplyLogFile_;
    return 0;
  }
  int32_t fd = open(lastApplyLogFile_->c_str(), O_RDONLY);
  if (fd < 0) {
    LOG(FATAL) << "Failed to open the file \"" << lastApplyLogFile_->c_str() << "\" (" << errno
               << "): " << strerror(errno);
  }
  // read last applied logId from listener wal file.
  LogID logId;
  auto offset = sizeof(LogID) + sizeof(TermID);
  CHECK_EQ(pread(fd, reinterpret_cast<char*>(&logId), sizeof(LogID), offset),
           static_cast<ssize_t>(sizeof(LogID)));
  close(fd);
  return logId;
}

bool ESListener::writeAppliedId(LogID lastId, TermID lastTerm, LogID lastApplyLogId) {
  int32_t fd = open(lastApplyLogFile_->c_str(), O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0644);
  if (fd < 0) {
    VLOG(3) << "Failed to open file \"" << lastApplyLogFile_->c_str() << "\" (errno: " << errno
            << "): " << strerror(errno);
    return false;
  }
  auto raw = encodeAppliedId(lastId, lastTerm, lastApplyLogId);
  ssize_t written = write(fd, raw.c_str(), raw.size());
  if (written != (ssize_t)raw.size()) {
    VLOG(4) << idStr_ << "bytesWritten:" << written << ", expected:" << raw.size()
            << ", error:" << strerror(errno);
    close(fd);
    return false;
  }
  close(fd);
  return true;
}

std::string ESListener::encodeAppliedId(LogID lastId,
                                        TermID lastTerm,
                                        LogID lastApplyLogId) const noexcept {
  std::string val;
  val.reserve(sizeof(LogID) * 2 + sizeof(TermID));
  val.append(reinterpret_cast<const char*>(&lastId), sizeof(LogID))
      .append(reinterpret_cast<const char*>(&lastTerm), sizeof(TermID))
      .append(reinterpret_cast<const char*>(&lastApplyLogId), sizeof(LogID));
  return val;
}

}  // namespace kvstore
}  // namespace nebula
