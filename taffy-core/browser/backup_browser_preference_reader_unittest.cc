// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <string_view>

#include "base/strings/string_view_util.h"
#include "base/values.h"
#include "components/prefs/testing_pref_service.h"
#include "crypto/hash.h"
#include "taffy/browser/backup_browser_record_adapters.h"
#include "taffy/browser/profile_preferences.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

using namespace storage::backup;
namespace prefs = profile_preferences;

class BackupBrowserPreferenceReaderTest : public testing::Test {
 protected:
  void SetUp() override {
    prefs::RegisterProfilePreferences(preferences_.registry());
  }

  TestingPrefServiceSimple preferences_;
};

TEST_F(BackupBrowserPreferenceReaderTest,
       ExactFourChoicesAndAuthenticatedDescriptor) {
  preferences_.SetString(prefs::kTheme, "DARK");
  preferences_.SetString(prefs::kAppLanguage, "HINDI");
  preferences_.SetString(prefs::kRegionCode, "GB");
  preferences_.SetBoolean(prefs::kForceDarkWeb, true);
  auto snapshot = ReadBrowserPreferenceBackupRecords(&preferences_);
  ASSERT_TRUE(snapshot.has_value());
  ASSERT_EQ(snapshot->size(), 1u);
  const auto& record = snapshot->front();
  ASSERT_TRUE(record.descriptor);
  EXPECT_EQ(record.descriptor->kind,
            core_service::mojom::BackupRecordKind::kBrowserPreference);
  EXPECT_EQ(record.descriptor->state,
            core_service::mojom::BackupRecordState::kActive);
  EXPECT_EQ(record.descriptor->schema_version, 1u);
  EXPECT_EQ(record.descriptor->plaintext_bytes, record.plaintext.size());
  EXPECT_TRUE(std::ranges::equal(record.descriptor->plaintext_sha256,
                                 crypto::hash::Sha256(record.plaintext)));
  auto decoded = DecodeBrowserPreferenceRecordV1(record.plaintext,
                                                 record.descriptor->stable_id,
                                                 record.descriptor->revision);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(*decoded,
            (BackupBrowserPreferenceRecord{"DARK", "HINDI", "GB", true}));
}

TEST_F(BackupBrowserPreferenceReaderTest, RegisteredDefaultsAreTypedChoices) {
  auto snapshot = ReadBrowserPreferenceBackupRecords(&preferences_);
  ASSERT_TRUE(snapshot.has_value());
  ASSERT_EQ(snapshot->size(), 1u);
  auto decoded = DecodeBrowserPreferenceRecordV1(
      snapshot->front().plaintext, kBrowserPreferenceBackupStableId, 1);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(*decoded,
            (BackupBrowserPreferenceRecord{"SYSTEM", "ENGLISH", "IN", false}));
}

TEST_F(BackupBrowserPreferenceReaderTest,
       OtherStoresAndConsentsCannotChangePayload) {
  auto before = ReadBrowserPreferenceBackupRecords(&preferences_);
  ASSERT_TRUE(before.has_value());
  preferences_.SetString(prefs::kAccountSessionRecords,
                         "private-account-value");
  preferences_.SetString(prefs::kProviderCredentialRecords,
                         "private-provider-value");
  preferences_.SetString(prefs::kSyncKeyRecords, "private-sync-value");
  preferences_.SetString(prefs::kFrequentSites, "private-history-value");
  preferences_.SetString(prefs::kTimeOnSites, "private-visits-value");
  preferences_.SetString(prefs::kProviderRoute, "BYOK");
  preferences_.SetBoolean(prefs::kDiagnosticsOptIn, true);
  preferences_.SetBoolean(prefs::kComposerSuggestions, true);
  preferences_.SetBoolean(prefs::kOnboardingCompleted, true);
  preferences_.SetBoolean(prefs::kPseudoLocalization, true);
  auto after = ReadBrowserPreferenceBackupRecords(&preferences_);
  ASSERT_TRUE(after.has_value());
  ASSERT_EQ(after->size(), 1u);
  EXPECT_EQ(before->front().plaintext, after->front().plaintext);
  EXPECT_EQ(base::as_string_view(after->front().plaintext).find("private-"),
            std::string_view::npos);
}

TEST_F(BackupBrowserPreferenceReaderTest,
       UnknownCountryIsNotNormalizedOrSilentlyReplaced) {
  for (const auto* region : {"ZZ", "AA", "in", "IND", ""}) {
    preferences_.SetString(prefs::kRegionCode, region);
    EXPECT_FALSE(ReadBrowserPreferenceBackupRecords(&preferences_));
  }
  EXPECT_FALSE(
      ValidateBrowserPreferenceBackupRecord({"DARK", "HINDI", "ZZ", false}));
  EXPECT_TRUE(
      ValidateBrowserPreferenceBackupRecord({"DARK", "HINDI", "US", false}));
}

TEST_F(BackupBrowserPreferenceReaderTest,
       UnknownValuesAndManagedChoicesRefuseWholeRecord) {
  preferences_.SetString(prefs::kTheme, "unknown");
  EXPECT_FALSE(ReadBrowserPreferenceBackupRecords(&preferences_));
  preferences_.SetString(prefs::kTheme, "SYSTEM");
  preferences_.SetManagedPref(prefs::kForceDarkWeb, base::Value(true));
  auto managed = ReadBrowserPreferenceBackupRecords(&preferences_);
  ASSERT_FALSE(managed.has_value());
  EXPECT_EQ(managed.error(), BackupSnapshotError::kUnsupportedSelection);
}

TEST(BackupBrowserPreferenceReaderStandaloneTest,
     MissingStoreOrRegistrationRefused) {
  EXPECT_FALSE(ReadBrowserPreferenceBackupRecords(nullptr));
  TestingPrefServiceSimple empty;
  EXPECT_FALSE(ReadBrowserPreferenceBackupRecords(&empty));
}

// A hand-edited or corrupt Preferences file is the only way a stored value can
// carry the wrong type, and this is what the product does with one. It must
// not abort: the first version of this test did, because the reader asked for
// the raw user value and the accessor that answers it is fatal outside an
// official build.
TEST_F(BackupBrowserPreferenceReaderTest,
       WrongTypeStoredChoiceBacksUpTheChoiceInForceAndNeverAborts) {
  preferences_.SetString(prefs::kAppLanguage, "HINDI");
  preferences_.SetUserPref(prefs::kTheme, base::Value(true));
  ASSERT_TRUE(preferences_.FindPreference(prefs::kTheme)->HasUserSetting());
  auto snapshot = ReadBrowserPreferenceBackupRecords(&preferences_);
  ASSERT_TRUE(snapshot.has_value());
  ASSERT_EQ(snapshot->size(), 1u);
  auto decoded = DecodeBrowserPreferenceRecordV1(
      snapshot->front().plaintext, kBrowserPreferenceBackupStableId, 1);
  ASSERT_TRUE(decoded.has_value());
  // The mistyped entry is discarded by PrefValueStore, so "SYSTEM" is the
  // theme the browser is running and the theme the person is looking at. The
  // choices around it are untouched, which is what makes this a report of the
  // live state rather than a wholesale fallback.
  EXPECT_EQ(*decoded,
            (BackupBrowserPreferenceRecord{"SYSTEM", "HINDI", "IN", false}));
}

}  // namespace
}  // namespace taffy
