// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <vector>

#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "sql/database.h"
#include "sql/test/test_helpers.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/components/storage/browser/core_storage_schema_test_util.h"
#include "taffy/components/storage/browser/generated/core_service_journal_schema.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

mojom::CoreBootstrapPtr Load(CoreStorageBroker* broker) {
  base::RunLoop loop;
  mojom::CoreBootstrapPtr bootstrap;
  broker->LoadBootstrap(
      1, false, base::BindLambdaForTesting([&](mojom::CoreBootstrapPtr loaded) {
        bootstrap = std::move(loaded);
        loop.Quit();
      }));
  loop.Run();
  return bootstrap;
}

class CoreStorageMigrationTest : public testing::Test {
 protected:
  // Opens `path` after every broker has let go of it and reads the whole
  // physical schema back.
  std::vector<std::string> SchemaOf(const base::FilePath& path) {
    task_environment_.RunUntilIdle();
    sql::Database database(sql::test::kTestTag);
    if (!database.Open(path)) {
      return {};
    }
    return storage_test::ReadSchemaObjects(&database);
  }

  base::test::TaskEnvironment task_environment_;
};

// The invariant the whole migration ladder exists to hold: a database that is
// carried up from an older version must end up with exactly the schema a first
// run creates. Comparing table names would not catch a column, constraint or
// index that only one of the two paths produces, so this compares every row of
// sqlite_master.
TEST_F(CoreStorageMigrationTest, EveryRecordedVersionMigratesToTheFreshSchema) {
  base::ScopedTempDir directory;
  ASSERT_TRUE(directory.CreateUniqueTempDir());

  const base::FilePath head_path =
      directory.GetPath().AppendASCII("head.sqlite3");
  {
    CoreStorageBroker broker(head_path, false);
    ASSERT_TRUE(Load(&broker));
  }
  const std::vector<std::string> head = SchemaOf(head_path);
  ASSERT_FALSE(head.empty());

  // Every migration has a recorded history and every recorded history has a
  // migration; the generator enforces both directions, and a build that
  // disagreed would leave a version untested rather than failing.
  ASSERT_EQ(storage_schema::kMigrations.size(),
            storage_schema::kHistoricalSchemas.size());

  for (const storage_schema::Migration& migration :
       storage_schema::kMigrations) {
    SCOPED_TRACE(testing::Message()
                 << "migrating from version " << migration.from_version);
    const base::FilePath path = directory.GetPath().AppendASCII(
        "v" + base::NumberToString(migration.from_version) + ".sqlite3");
    {
      sql::Database database(sql::test::kTestTag);
      ASSERT_TRUE(database.Open(path));
      ASSERT_TRUE(storage_test::CreateHistoricalSchema(&database,
                                                       migration.from_version));
      // Whether the fixture already matches head is the whole difference
      // between the two kinds of entry, so it is asserted rather than
      // tolerated in either direction.
      //
      // A migration with statements must have something to do: a fixture that
      // already matched head would pass the rest of this test without the
      // statements running at all, which is how a broken ladder looks green.
      // An entry with no statements must have nothing to do, and its fixture
      // must therefore match head exactly -- it exists because a version bump
      // can change what the schema document says without changing a byte on
      // disk, and what it carries forward is the recorded version and
      // checksum, checked below.
      if (migration.statement_count == 0u) {
        EXPECT_EQ(head, storage_test::ReadSchemaObjects(&database));
      } else {
        EXPECT_NE(head, storage_test::ReadSchemaObjects(&database));
      }
      database.Close();
    }

    {
      CoreStorageBroker broker(path, false);
      mojom::CoreBootstrapPtr bootstrap = Load(&broker);
      ASSERT_TRUE(bootstrap);
      EXPECT_EQ(storage_schema::kVersion,
                bootstrap->core_journal_schema_version);
      EXPECT_EQ(storage_schema::kChecksum,
                bootstrap->core_journal_schema_checksum);
    }
    EXPECT_EQ(head, SchemaOf(path));
  }
}

// The ladder is only reachable if the checksum a database recorded is the one
// its migration entry names, so a fixture built from the recorded history must
// be accepted rather than migrated past or refused.
TEST_F(CoreStorageMigrationTest, RecordedHistoryCarriesItsMigrationChecksum) {
  ASSERT_FALSE(storage_schema::kHistoricalSchemas.empty());
  for (const storage_schema::HistoricalSchema& schema :
       storage_schema::kHistoricalSchemas) {
    SCOPED_TRACE(testing::Message() << "version " << schema.version);
    bool matched = false;
    for (const storage_schema::Migration& migration :
         storage_schema::kMigrations) {
      if (migration.from_version == schema.version) {
        EXPECT_EQ(migration.from_checksum, schema.checksum);
        matched = true;
      }
    }
    EXPECT_TRUE(matched);
  }
}

}  // namespace
}  // namespace taffy
