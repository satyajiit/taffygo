// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/backup_restore_preference_readback.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/values.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"

#if BUILDFLAG(IS_POSIX)
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#endif

namespace taffy {
namespace {

using Witness = BackupRestorePreferenceWriteWitness;

std::vector<Witness> OneWitness(std::string dotted_path, base::Value expected) {
  std::vector<Witness> witnesses;
  witnesses.push_back(
      {.dotted_path = std::move(dotted_path), .expected = std::move(expected)});
  return witnesses;
}

std::vector<Witness> AbsentWitness(std::string dotted_path) {
  std::vector<Witness> witnesses;
  witnesses.push_back({.dotted_path = std::move(dotted_path),
                       .expected = base::Value(),
                       .must_be_absent = true});
  return witnesses;
}

class BackupRestorePreferenceReadbackTest : public testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(directory_.CreateUniqueTempDir()); }

  base::FilePath Path(std::string_view name) const {
    return directory_.GetPath().AppendASCII(name);
  }

  void Write(const base::FilePath& path, std::string_view contents) {
    ASSERT_TRUE(base::WriteFile(path, contents));
  }

 private:
  base::ScopedTempDir directory_;
};

TEST_F(BackupRestorePreferenceReadbackTest, ExactTypedWitnessesMatch) {
  const base::FilePath path = Path("Preferences");
  Write(path,
        R"({"profile":{"name":"Restore review","omitted":true},"counter":7})");
  std::vector<Witness> witnesses;
  witnesses.push_back({.dotted_path = "profile.name",
                       .expected = base::Value("Restore review")});
  witnesses.push_back(
      {.dotted_path = "profile.omitted", .expected = base::Value(true)});
  witnesses.push_back({.dotted_path = "counter", .expected = base::Value(7)});

  EXPECT_TRUE(BackupRestorePreferenceFileMatches(path, std::move(witnesses)));
}

TEST_F(BackupRestorePreferenceReadbackTest,
       MissingMismatchedAndWrongTypeWitnessesFail) {
  const base::FilePath path = Path("Preferences");
  Write(path, R"({"profile":{"name":"Restore review","omitted":true}})");

  EXPECT_FALSE(BackupRestorePreferenceFileMatches(
      path, OneWitness("profile.missing", base::Value(true))));
  EXPECT_FALSE(BackupRestorePreferenceFileMatches(
      path, OneWitness("profile.name", base::Value("Different"))));
  EXPECT_FALSE(BackupRestorePreferenceFileMatches(
      path, OneWitness("profile.omitted", base::Value("true"))));
  EXPECT_FALSE(BackupRestorePreferenceFileMatches(
      path, OneWitness("", base::Value(true))));
}

TEST_F(BackupRestorePreferenceReadbackTest, MissingFileFails) {
  EXPECT_FALSE(BackupRestorePreferenceFileMatches(
      Path("missing"), OneWitness("profile.name", base::Value("name"))));
}

TEST_F(BackupRestorePreferenceReadbackTest,
       ExplicitAbsenceRequiresNoStoredValueAndWellTypedAncestors) {
  const auto path = Path("Local State");
  for (std::string_view contents :
       {R"({})", R"({"taffy":{}})", R"({"taffy":{"other":7}})"}) {
    Write(path, contents);
    EXPECT_TRUE(BackupRestorePreferenceFileMatches(
        path, AbsentWitness("taffy.reservation")));
  }
  for (std::string_view contents :
       {R"({"taffy":null})", R"({"taffy":[]})", R"({"taffy":false})",
        R"({"taffy":{"reservation":null}})",
        R"({"taffy":{"reservation":{}}})"}) {
    Write(path, contents);
    EXPECT_FALSE(BackupRestorePreferenceFileMatches(
        path, AbsentWitness("taffy.reservation")));
  }
}

TEST_F(BackupRestorePreferenceReadbackTest,
       AbsenceDoesNotAcceptAnInvalidPathOrContradictoryExpectedValue) {
  const auto path = Path("Local State");
  Write(path, R"({})");
  for (const char* dotted : {"", ".taffy", "taffy.", "taffy..reservation"}) {
    EXPECT_FALSE(
        BackupRestorePreferenceFileMatches(path, AbsentWitness(dotted)));
  }
  auto witnesses = AbsentWitness("taffy.reservation");
  witnesses.front().expected = base::Value("still-present");
  EXPECT_FALSE(BackupRestorePreferenceFileMatches(path, std::move(witnesses)));
}

TEST_F(BackupRestorePreferenceReadbackTest,
       ExactNullAndAbsenceAreDifferentWitnesses) {
  const auto path = Path("Local State");
  Write(path, R"({"value":null})");
  EXPECT_TRUE(BackupRestorePreferenceFileMatches(
      path, OneWitness("value", base::Value())));
  EXPECT_FALSE(
      BackupRestorePreferenceFileMatches(path, AbsentWitness("value")));
  Write(path, R"({})");
  EXPECT_FALSE(BackupRestorePreferenceFileMatches(
      path, OneWitness("value", base::Value())));
  EXPECT_TRUE(BackupRestorePreferenceFileMatches(path, AbsentWitness("value")));
}

TEST_F(BackupRestorePreferenceReadbackTest, MalformedAndTruncatedJsonFail) {
  const base::FilePath malformed = Path("malformed");
  const base::FilePath truncated = Path("truncated");
  Write(malformed, R"({"profile":,})");
  Write(truncated, R"({"profile":{"name":"Restore review")");

  EXPECT_FALSE(BackupRestorePreferenceFileMatches(
      malformed, OneWitness("profile.name", base::Value("Restore review"))));
  EXPECT_FALSE(BackupRestorePreferenceFileMatches(
      truncated, OneWitness("profile.name", base::Value("Restore review"))));
}

TEST_F(BackupRestorePreferenceReadbackTest, NonDictionaryRootFails) {
  const base::FilePath path = Path("Preferences");
  Write(path, R"([{"profile":"Restore review"}])");

  EXPECT_FALSE(BackupRestorePreferenceFileMatches(
      path, OneWitness("profile", base::Value("Restore review"))));
}

TEST_F(BackupRestorePreferenceReadbackTest, OversizeFileFailsBeforeParsing) {
  const base::FilePath path = Path("Preferences");
  base::File file(path,
                  base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  ASSERT_TRUE(file.IsValid());
  ASSERT_TRUE(file.SetLength(
      static_cast<int64_t>(kMaximumBackupRestorePreferenceFileBytes) + 1));
  file.Close();

  EXPECT_FALSE(BackupRestorePreferenceFileMatches(
      path, OneWitness("profile.name", base::Value("Restore review"))));
}

TEST_F(BackupRestorePreferenceReadbackTest, NoWitnessesFail) {
  const base::FilePath path = Path("Preferences");
  Write(path, R"({"profile":{"name":"Restore review"}})");

  EXPECT_FALSE(BackupRestorePreferenceFileMatches(path, {}));
}

TEST_F(BackupRestorePreferenceReadbackTest, UnrelatedPreferencesAreAllowed) {
  const base::FilePath path = Path("Preferences");
  Write(
      path,
      R"({"profile":{"name":"Restore review"},"unrelated":{"counter":9,"enabled":false}})");

  EXPECT_TRUE(BackupRestorePreferenceFileMatches(
      path, OneWitness("profile.name", base::Value("Restore review"))));
}

#if BUILDFLAG(IS_POSIX)
TEST_F(BackupRestorePreferenceReadbackTest,
       SynchronizesExactAbsenceWithOtherRequiredValues) {
  const auto path = Path("Local State");
  const std::string contents = R"({"profile":{"published":true},"taffy":{}})";
  Write(path, contents);
  auto witnesses = AbsentWitness("taffy.reservation");
  witnesses.push_back(
      {.dotted_path = "profile.published", .expected = base::Value(true)});
  EXPECT_TRUE(
      BackupRestorePreferenceFileMatchesAndSync(path, std::move(witnesses)));
  std::string after;
  ASSERT_TRUE(base::ReadFileToString(path, &after));
  EXPECT_EQ(contents, after);
}

TEST_F(BackupRestorePreferenceReadbackTest,
       SynchronizesExactWitnessWithoutEditing) {
  const auto path = Path("Local State");
  const std::string contents = R"({"reservation":"owned","other":7})";
  Write(path, contents);
  EXPECT_TRUE(BackupRestorePreferenceFileMatchesAndSync(
      path, OneWitness("reservation", base::Value("owned"))));
  std::string after;
  ASSERT_TRUE(base::ReadFileToString(path, &after));
  EXPECT_EQ(contents, after);
}

TEST_F(BackupRestorePreferenceReadbackTest,
       SyncRequiresExactNonemptyWitnesses) {
  const auto path = Path("Local State");
  Write(path, R"({"reservation":"owned"})");
  EXPECT_FALSE(BackupRestorePreferenceFileMatchesAndSync(path, {}));
  EXPECT_FALSE(BackupRestorePreferenceFileMatchesAndSync(
      path, OneWitness("reservation", base::Value("different"))));
  EXPECT_FALSE(BackupRestorePreferenceFileMatchesAndSync(
      path, OneWitness("reservation", base::Value(true))));
  EXPECT_FALSE(BackupRestorePreferenceFileMatchesAndSync(
      path, OneWitness("missing", base::Value("owned"))));
}

TEST_F(BackupRestorePreferenceReadbackTest,
       SyncRefusesUnsafeAndNonregularPaths) {
  const auto witness = [] {
    return OneWitness("reservation", base::Value("owned"));
  };
  EXPECT_FALSE(
      BackupRestorePreferenceFileMatchesAndSync(base::FilePath(), witness()));
  EXPECT_FALSE(BackupRestorePreferenceFileMatchesAndSync(
      base::FilePath("relative"), witness()));
  EXPECT_FALSE(
      BackupRestorePreferenceFileMatchesAndSync(Path("missing"), witness()));
  EXPECT_FALSE(BackupRestorePreferenceFileMatchesAndSync(Path("."), witness()));
  EXPECT_FALSE(
      BackupRestorePreferenceFileMatchesAndSync(Path(".."), witness()));
  EXPECT_FALSE(BackupRestorePreferenceFileMatchesAndSync(
      Path("Local State").DirName(), witness()));
  const auto fifo = Path("pipe");
  ASSERT_EQ(0, mkfifo(fifo.value().c_str(), 0600));
  EXPECT_FALSE(BackupRestorePreferenceFileMatchesAndSync(fifo, witness()));
}

TEST_F(BackupRestorePreferenceReadbackTest, SyncRefusesSymbolicLinks) {
  const auto path = Path("Local State");
  Write(path, R"({"reservation":"owned"})");
  const auto witness = [] {
    return OneWitness("reservation", base::Value("owned"));
  };
  const auto symbolic = Path("symbolic");
  ASSERT_TRUE(base::CreateSymbolicLink(path, symbolic));
  EXPECT_FALSE(BackupRestorePreferenceFileMatchesAndSync(symbolic, witness()));
  const auto ancestor = Path("ancestor");
  ASSERT_TRUE(base::CreateSymbolicLink(path.DirName(), ancestor));
  EXPECT_FALSE(BackupRestorePreferenceFileMatchesAndSync(
      ancestor.AppendASCII("Local State"), witness()));
}

// Separate from the symbolic-link rule because a device cannot set the fixture
// up: link(2) is refused with EPERM on Android application-private storage, so
// the adversary this test describes cannot be built there. Skipping says that;
// asserting on link() succeeding reported it as a product failure instead.
TEST_F(BackupRestorePreferenceReadbackTest, SyncRefusesHardLinks) {
  const auto path = Path("Local State");
  Write(path, R"({"reservation":"owned"})");
  const auto witness = [] {
    return OneWitness("reservation", base::Value("owned"));
  };
  const auto hard = Path("hard");
  if (link(path.value().c_str(), hard.value().c_str()) != 0) {
    GTEST_SKIP() << "this filesystem refuses hard links (errno " << errno
                 << "), so the second name this test needs cannot be made";
  }
  EXPECT_FALSE(BackupRestorePreferenceFileMatchesAndSync(hard, witness()));
  // The original name is refused too: a second name is a property of the file,
  // not of the path used to reach it.
  EXPECT_FALSE(BackupRestorePreferenceFileMatchesAndSync(path, witness()));
}

TEST_F(BackupRestorePreferenceReadbackTest,
       SyncRefusesMalformedAndOversizedContents) {
  const auto path = Path("Local State");
  const auto witness = [] {
    return OneWitness("reservation", base::Value("owned"));
  };
  Write(path, R"({"reservation":)");
  EXPECT_FALSE(BackupRestorePreferenceFileMatchesAndSync(path, witness()));
  Write(path, R"(["owned"])");
  EXPECT_FALSE(BackupRestorePreferenceFileMatchesAndSync(path, witness()));
  base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_WRITE);
  ASSERT_TRUE(file.SetLength(
      static_cast<int64_t>(kMaximumBackupRestorePreferenceFileBytes) + 1));
  file.Close();
  EXPECT_FALSE(BackupRestorePreferenceFileMatchesAndSync(path, witness()));
}
#endif

}  // namespace
}  // namespace taffy
