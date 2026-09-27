// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_SUPPORT_TASK_TRANSACTION_SCHEMA_H_
#define TAFFY_TEST_SUPPORT_TASK_TRANSACTION_SCHEMA_H_

#include <stdint.h>

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace taffy::test::internal {

struct TransactionFieldSpec {
  std::string name;
  std::string type;
};

struct TransactionVariantSpec {
  std::vector<TransactionFieldSpec> fields;
};

// The binary codec's ordered type graph. It is built directly from the
// committed contract sources, never from a second hand-maintained layout.
struct TaskTransactionSchema {
  std::string root;
  std::string version_field;
  std::string magic;
  uint32_t version = 0u;
  size_t max_bytes = 0u;
  size_t max_collection_items = 0u;
  size_t max_string_bytes = 0u;
  std::map<std::string, std::map<uint32_t, std::string>> enums;
  std::map<std::string, std::vector<TransactionFieldSpec>> records;
  std::map<std::string, std::map<uint32_t, TransactionVariantSpec>> unions;
};

// Cached for the process because every durable batch uses one immutable
// schema. Null names a missing or malformed schema in `error`.
const TaskTransactionSchema* GetTaskTransactionSchema(std::string* error);

}  // namespace taffy::test::internal

#endif  // TAFFY_TEST_SUPPORT_TASK_TRANSACTION_SCHEMA_H_
