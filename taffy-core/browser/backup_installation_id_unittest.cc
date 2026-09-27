// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/backup_installation_id.h"

#include <optional>
#include <string>

#include "base/uuid.h"
#include "components/prefs/testing_pref_service.h"
#include "taffy/browser/application_preferences.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

void RegisterApplicationPrefs(TestingPrefServiceSimple* prefs) {
  application_preferences::RegisterLocalStatePreferences(prefs->registry());
}

TEST(BackupInstallationIdTest, RegisteredApplicationPrefStartsEmpty) {
  TestingPrefServiceSimple local_state;
  RegisterApplicationPrefs(&local_state);

  EXPECT_TRUE(local_state.FindPreference(
      application_preferences::kBackupInstallationId));
  EXPECT_TRUE(
      local_state.GetString(application_preferences::kBackupInstallationId)
          .empty());
}

TEST(BackupInstallationIdTest, RepeatedReadsKeepOneValidIdentity) {
  TestingPrefServiceSimple local_state;
  RegisterApplicationPrefs(&local_state);

  const std::optional<std::string> first =
      GetOrCreateBackupInstallationId(&local_state);
  const std::optional<std::string> second =
      GetOrCreateBackupInstallationId(&local_state);

  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  EXPECT_TRUE(base::Uuid::ParseLowercase(*first).is_valid());
  EXPECT_EQ('4', (*first)[14]);
  EXPECT_EQ(*first, *second);
  EXPECT_EQ(*first, local_state.GetString(
                        application_preferences::kBackupInstallationId));
}

TEST(BackupInstallationIdTest, ExistingCanonicalV4IdentityIsPreserved) {
  TestingPrefServiceSimple local_state;
  RegisterApplicationPrefs(&local_state);
  constexpr char kExisting[] = "82b6af0c-cf00-4f1c-86c3-a2ba0b9af525";
  local_state.SetString(application_preferences::kBackupInstallationId,
                        kExisting);

  const std::optional<std::string> existing =
      GetOrCreateBackupInstallationId(&local_state);
  ASSERT_TRUE(existing);
  EXPECT_EQ(kExisting, *existing);
}

TEST(BackupInstallationIdTest, FreshApplicationStoresGetDifferentIdentities) {
  TestingPrefServiceSimple first_store;
  TestingPrefServiceSimple second_store;
  RegisterApplicationPrefs(&first_store);
  RegisterApplicationPrefs(&second_store);

  const std::optional<std::string> first =
      GetOrCreateBackupInstallationId(&first_store);
  const std::optional<std::string> second =
      GetOrCreateBackupInstallationId(&second_store);

  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  EXPECT_NE(*first, *second);
}

TEST(BackupInstallationIdTest, MalformedStoredValueGetsNewStableIdentity) {
  TestingPrefServiceSimple local_state;
  RegisterApplicationPrefs(&local_state);
  local_state.SetString(application_preferences::kBackupInstallationId,
                        "not-a-uuid");

  const std::optional<std::string> repaired =
      GetOrCreateBackupInstallationId(&local_state);
  const std::optional<std::string> repeated =
      GetOrCreateBackupInstallationId(&local_state);

  ASSERT_TRUE(repaired);
  EXPECT_NE("not-a-uuid", *repaired);
  EXPECT_TRUE(base::Uuid::ParseLowercase(*repaired).is_valid());
  EXPECT_EQ(repaired, repeated);
  EXPECT_EQ(*repaired, local_state.GetString(
                           application_preferences::kBackupInstallationId));
}

TEST(BackupInstallationIdTest, NonV4UuidGetsNewV4Identity) {
  TestingPrefServiceSimple local_state;
  RegisterApplicationPrefs(&local_state);
  local_state.SetString(application_preferences::kBackupInstallationId,
                        "00000000-0000-0000-0000-000000000000");

  const std::optional<std::string> repaired =
      GetOrCreateBackupInstallationId(&local_state);

  ASSERT_TRUE(repaired);
  EXPECT_EQ('4', (*repaired)[14]);
  EXPECT_NE("00000000-0000-0000-0000-000000000000", *repaired);
}

TEST(BackupInstallationIdTest, UnavailableOrUnregisteredStoreFailsClosed) {
  EXPECT_FALSE(GetOrCreateBackupInstallationId(nullptr));
  TestingPrefServiceSimple unregistered;
  EXPECT_FALSE(GetOrCreateBackupInstallationId(&unregistered));
}

}  // namespace
}  // namespace taffy
