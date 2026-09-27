// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/storage/browser/core_storage_schema_test_util.h"

#include <algorithm>

#include "sql/database.h"
#include "sql/statement.h"
#include "taffy/components/storage/browser/generated/core_service_journal_schema.h"

namespace taffy::storage_test {

namespace {

const storage_schema::HistoricalSchema* FindHistoricalSchema(uint32_t version) {
  for (const storage_schema::HistoricalSchema& schema :
       storage_schema::kHistoricalSchemas) {
    if (schema.version == version) {
      return &schema;
    }
  }
  return nullptr;
}

std::string CollapseWhitespace(const std::string& sql) {
  std::string collapsed;
  collapsed.reserve(sql.size());
  bool pending_space = false;
  for (const char character : sql) {
    const bool is_space = character == ' ' || character == '\t' ||
                          character == '\n' || character == '\r';
    if (is_space) {
      pending_space = !collapsed.empty();
      continue;
    }
    if (pending_space) {
      collapsed.push_back(' ');
      pending_space = false;
    }
    collapsed.push_back(character);
  }
  return collapsed;
}

}  // namespace

bool CreateHistoricalSchema(sql::Database* database, uint32_t version) {
  const storage_schema::HistoricalSchema* schema = FindHistoricalSchema(version);
  if (!schema ||
      schema->statement_offset > storage_schema::kHistoricalStatements.size() ||
      schema->statement_count >
          storage_schema::kHistoricalStatements.size() -
              schema->statement_offset) {
    return false;
  }
  if (!database->Execute(storage_schema::kLedgerStatement)) {
    return false;
  }
  for (size_t index = 0; index < schema->statement_count; ++index) {
    if (!database->Execute(storage_schema::kHistoricalStatements
                               [schema->statement_offset + index])) {
      return false;
    }
  }
  sql::Statement ledger(database->GetUniqueStatement(
      "INSERT INTO taffy_storage_schema(component,version,checksum) "
      "VALUES(?,?,?)"));
  ledger.BindString(0, storage_schema::kComponent);
  ledger.BindInt(1, static_cast<int>(schema->version));
  ledger.BindString(2, schema->checksum);
  return ledger.Run();
}

std::vector<std::string> ReadSchemaObjects(sql::Database* database) {
  std::vector<std::string> objects;
  sql::Statement rows(database->GetUniqueStatement(
      "SELECT type,name,IFNULL(sql,'') FROM sqlite_master"));
  while (rows.Step()) {
    objects.push_back(rows.ColumnString(0) + "|" + rows.ColumnString(1) + "|" +
                      CollapseWhitespace(rows.ColumnString(2)));
  }
  if (!rows.Succeeded()) {
    return {};
  }
  std::sort(objects.begin(), objects.end());
  return objects;
}

}  // namespace taffy::storage_test
