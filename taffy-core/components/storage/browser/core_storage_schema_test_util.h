// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_SCHEMA_TEST_UTIL_H_
#define TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_SCHEMA_TEST_UTIL_H_

#include <stdint.h>

#include <string>
#include <vector>

namespace sql {
class Database;
}

namespace taffy::storage_test {

// Writes into `database` the schema that `version` physically was: the creation
// list the generated history records for it, followed by the ledger row a
// database at that version carried. Returns false when no history is recorded
// for `version`, or when any statement fails.
//
// Nothing here trims the head creation list. kStatements is in the order the
// head document lists its tables, not in the order versions introduced them, so
// a fixture built by dropping a suffix of it hands the old database tables that
// version never had — and the migration then fails on the table it was written
// to create.
bool CreateHistoricalSchema(sql::Database* database, uint32_t version);

// Every object in sqlite_master, rendered as "<type>|<name>|<sql>" and sorted,
// so two databases compare as a whole rather than by a table name spot-check.
// Runs of whitespace inside the SQL are collapsed to one space, because SQLite
// stores the text of the statement that created an object and a migration is
// free to spell one definition differently from the head creation list.
std::vector<std::string> ReadSchemaObjects(sql::Database* database);

}  // namespace taffy::storage_test

#endif  // TAFFY_COMPONENTS_STORAGE_BROWSER_CORE_STORAGE_SCHEMA_TEST_UTIL_H_
