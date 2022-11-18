// Copyright (c) 2021 vesoft inc. All rights reserved.
//
// This source code is licensed under Apache 2.0 License.

#include "graph/util/FTIndexUtils.h"

#include "common/expression/Expression.h"
#include "graph/util/ExpressionUtils.h"

DECLARE_uint32(ft_request_retry_times);

namespace nebula {
namespace graph {

bool FTIndexUtils::needTextSearch(const Expression* expr) {
  switch (expr->kind()) {
    case Expression::Kind::kTSFuzzy:
    case Expression::Kind::kTSPrefix:
    case Expression::Kind::kTSRegexp:
    case Expression::Kind::kTSWildcard: {
      return true;
    }
    default:
      return false;
  }
}

StatusOr<::nebula::plugin::ESAdapter> FTIndexUtils::getESAdapter(meta::MetaClient* client) {
  auto tcs = client->getServiceClientsFromCache(meta::cpp2::ExternalServiceType::ELASTICSEARCH);
  if (!tcs.ok()) {
    return tcs.status();
  }
  if (tcs.value().empty()) {
    return Status::SemanticError("No text search client found");
  }
  std::vector<::nebula::plugin::ESClient> clients;
  for (const auto& c : tcs.value()) {
    std::string protocol = c.conn_type_ref().has_value() ? *c.get_conn_type() : "http";
    std::string address = c.host.toString();
    std::string user = c.user_ref().has_value() ? *c.user_ref() : "";
    std::string password = c.pwd_ref().has_value() ? *c.pwd_ref() : "";
    clients.emplace_back(protocol, address, user, password);
  }
  return ::nebula::plugin::ESAdapter(std::move(clients));
}

StatusOr<Expression*> FTIndexUtils::rewriteTSFilter(ObjectPool* pool,
                                                    bool isEdge,
                                                    Expression* expr,
                                                    const std::string& index,
                                                    ::nebula::plugin::ESAdapter& esAdapter) {
  auto vRet = textSearch(expr, index, esAdapter);
  if (!vRet.ok()) {
    return vRet.status();
  }
  auto result = std::move(vRet).value();
  if (result.items.empty()) {
    return nullptr;
  }
  auto tsArg = static_cast<TextSearchExpression*>(expr)->arg();
  Expression* propExpr;
  if (isEdge) {
    propExpr = EdgePropertyExpression::make(pool, tsArg->from(), tsArg->prop());
  } else {
    propExpr = TagPropertyExpression::make(pool, tsArg->from(), tsArg->prop());
  }
  std::vector<Expression*> rels;
  for (auto& item : result.items) {
    auto constExpr = ConstantExpression::make(pool, Value(item.text));
    rels.emplace_back(RelationalExpression::makeEQ(pool, propExpr, constExpr));
  }
  if (rels.size() == 1) {
    return rels.front();
  }
  return ExpressionUtils::pushOrs(pool, rels);
}

StatusOr<nebula::plugin::ESQueryResult> FTIndexUtils::textSearch(
    Expression* expr, const std::string& index, ::nebula::plugin::ESAdapter& esAdapter) {
  auto tsExpr = static_cast<TextSearchExpression*>(expr);
  std::string pattern = tsExpr->arg()->val();
  std::function<StatusOr<nebula::plugin::ESQueryResult>()> execFunc;
  switch (tsExpr->kind()) {
    case Expression::Kind::kTSFuzzy: {
      execFunc = [&index, &pattern, &esAdapter]() { return esAdapter.fuzzy(index, pattern); };
      break;
    }
    case Expression::Kind::kTSPrefix: {
      execFunc = [&index, &pattern, &esAdapter]() { return esAdapter.prefix(index, pattern); };
      break;
    }
    case Expression::Kind::kTSRegexp: {
      execFunc = [&index, &pattern, &esAdapter]() { return esAdapter.regexp(index, pattern); };
      break;
    }
    case Expression::Kind::kTSWildcard: {
      execFunc = [&index, &pattern, &esAdapter]() { return esAdapter.wildcard(index, pattern); };
      break;
    }
    default: {
      return Status::SemanticError("text search expression error");
    }
  }

  auto retryCnt = FLAGS_ft_request_retry_times > 0 ? FLAGS_ft_request_retry_times : 1;
  StatusOr<nebula::plugin::ESQueryResult> result;
  while (retryCnt-- > 0) {
    result = execFunc();
    if (!result.ok()) {
      continue;
    }
    break;
  }
  return result;
}

}  // namespace graph
}  // namespace nebula
