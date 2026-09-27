// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "base/functional/bind.h"
#include "base/values.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/testing_pref_service.h"
#include "taffy/browser/backup_browser_target_writers.h"
#include "taffy/browser/profile_preferences.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

using storage::backup::BackupBrowserPreferenceRecord;
namespace prefs = profile_preferences;

class BackupBrowserPreferenceTargetWriterTest : public testing::Test {
 protected:
  void SetUp() override {
    prefs::RegisterProfilePreferences(preferences_.registry());
  }

  TestingPrefServiceSimple preferences_;
};

TEST_F(BackupBrowserPreferenceTargetWriterTest,
       WritesAndReadsBackExactlyFourTypedChoices) {
  const BackupBrowserPreferenceRecord expected{"DARK", "HINDI", "GB", true};
  ChromiumBackupBrowserPreferenceTargetWriter chromium_writer(&preferences_);
  BackupBrowserPreferenceTargetWriter& writer = chromium_writer;

  EXPECT_EQ(writer.WriteAndReadBack(expected),
            BackupBrowserTargetWriteResult::kAppliedAndReadBack);
  auto readback = writer.ReadBack();
  ASSERT_TRUE(readback.has_value());
  EXPECT_EQ(*readback, expected);
  ASSERT_TRUE(preferences_.GetUserPrefValue(prefs::kTheme));
  ASSERT_TRUE(preferences_.GetUserPrefValue(prefs::kAppLanguage));
  ASSERT_TRUE(preferences_.GetUserPrefValue(prefs::kRegionCode));
  ASSERT_TRUE(preferences_.GetUserPrefValue(prefs::kForceDarkWeb));
  EXPECT_EQ(preferences_.GetString(prefs::kTheme), "DARK");
  EXPECT_EQ(preferences_.GetString(prefs::kAppLanguage), "HINDI");
  EXPECT_EQ(preferences_.GetString(prefs::kRegionCode), "GB");
  EXPECT_TRUE(preferences_.GetBoolean(prefs::kForceDarkWeb));
}

TEST_F(BackupBrowserPreferenceTargetWriterTest,
       ExcludedPreferenceRootsAreNeverModified) {
  preferences_.SetString(prefs::kProviderRoute, "BYOK");
  preferences_.SetString(prefs::kAccountSessionRecords, "account-private");
  preferences_.SetString(prefs::kSyncKeyRecords, "sync-private");
  preferences_.SetBoolean(prefs::kDiagnosticsOptIn, true);
  const BackupBrowserPreferenceRecord record{"LIGHT", "ENGLISH", "US", false};

  ChromiumBackupBrowserPreferenceTargetWriter writer(&preferences_);
  EXPECT_EQ(writer.WriteAndReadBack(record),
            BackupBrowserTargetWriteResult::kAppliedAndReadBack);
  EXPECT_EQ(preferences_.GetString(prefs::kProviderRoute), "BYOK");
  EXPECT_EQ(preferences_.GetString(prefs::kAccountSessionRecords),
            "account-private");
  EXPECT_EQ(preferences_.GetString(prefs::kSyncKeyRecords), "sync-private");
  EXPECT_TRUE(preferences_.GetBoolean(prefs::kDiagnosticsOptIn));
}

TEST_F(BackupBrowserPreferenceTargetWriterTest,
       InvalidChoiceIsRefusedBeforeAnySelectedPreferenceChanges) {
  const BackupBrowserPreferenceRecord invalid{"DARK", "HINDI", "ZZ", true};
  ChromiumBackupBrowserPreferenceTargetWriter writer(&preferences_);
  EXPECT_EQ(writer.WriteAndReadBack(invalid),
            BackupBrowserTargetWriteResult::kRefusedBeforeMutation);
  for (const char* name : {prefs::kTheme, prefs::kAppLanguage,
                           prefs::kRegionCode, prefs::kForceDarkWeb}) {
    EXPECT_FALSE(preferences_.GetUserPrefValue(name));
  }
}

TEST_F(BackupBrowserPreferenceTargetWriterTest,
       ExistingSelectedChoiceRefusesWithoutOverwritingIt) {
  preferences_.SetString(prefs::kTheme, "LIGHT");
  const BackupBrowserPreferenceRecord incoming{"DARK", "HINDI", "GB", true};
  ChromiumBackupBrowserPreferenceTargetWriter writer(&preferences_);
  EXPECT_EQ(writer.WriteAndReadBack(incoming),
            BackupBrowserTargetWriteResult::kRefusedBeforeMutation);
  EXPECT_EQ(preferences_.GetString(prefs::kTheme), "LIGHT");
  EXPECT_FALSE(preferences_.GetUserPrefValue(prefs::kAppLanguage));
  EXPECT_FALSE(preferences_.GetUserPrefValue(prefs::kRegionCode));
  EXPECT_FALSE(preferences_.GetUserPrefValue(prefs::kForceDarkWeb));
}

TEST_F(BackupBrowserPreferenceTargetWriterTest,
       ManagedChoiceRefusesBeforeOtherChoicesAreWritten) {
  preferences_.SetManagedPref(prefs::kRegionCode, base::Value("US"));
  const BackupBrowserPreferenceRecord incoming{"DARK", "HINDI", "GB", true};
  ChromiumBackupBrowserPreferenceTargetWriter writer(&preferences_);
  EXPECT_EQ(writer.WriteAndReadBack(incoming),
            BackupBrowserTargetWriteResult::kRefusedBeforeMutation);
  EXPECT_FALSE(preferences_.GetUserPrefValue(prefs::kTheme));
  EXPECT_FALSE(preferences_.GetUserPrefValue(prefs::kAppLanguage));
  EXPECT_FALSE(preferences_.GetUserPrefValue(prefs::kRegionCode));
  EXPECT_FALSE(preferences_.GetUserPrefValue(prefs::kForceDarkWeb));
}

TEST_F(BackupBrowserPreferenceTargetWriterTest,
       AChangedPostMutationReadbackIsUnknownAndDoesNotAuthorizeRetry) {
  PrefChangeRegistrar observer;
  observer.Init(&preferences_);
  observer.Add(prefs::kForceDarkWeb, base::BindRepeating(
                                         [](PrefService* preferences) {
                                           preferences->SetString(prefs::kTheme,
                                                                  "LIGHT");
                                         },
                                         base::Unretained(&preferences_)));
  const BackupBrowserPreferenceRecord incoming{"DARK", "HINDI", "GB", true};
  ChromiumBackupBrowserPreferenceTargetWriter writer(&preferences_);
  EXPECT_EQ(writer.WriteAndReadBack(incoming),
            BackupBrowserTargetWriteResult::kOutcomeUnknown);
  // Readback failure is not proof that no mutation happened and is not an
  // instruction to roll back the independently observed value.
  EXPECT_EQ(preferences_.GetString(prefs::kTheme), "LIGHT");
  EXPECT_EQ(preferences_.GetString(prefs::kAppLanguage), "HINDI");
  EXPECT_EQ(preferences_.GetString(prefs::kRegionCode), "GB");
  EXPECT_TRUE(preferences_.GetBoolean(prefs::kForceDarkWeb));
  EXPECT_EQ(writer.WriteAndReadBack(incoming),
            BackupBrowserTargetWriteResult::kRefusedBeforeMutation);
  EXPECT_EQ(preferences_.GetString(prefs::kTheme), "LIGHT");
}

TEST(BackupBrowserPreferenceTargetWriterStandaloneTest,
     MissingOwnerOrRegistrationIsRefused) {
  const BackupBrowserPreferenceRecord incoming{"DARK", "HINDI", "GB", true};
  ChromiumBackupBrowserPreferenceTargetWriter absent(nullptr);
  EXPECT_EQ(absent.WriteAndReadBack(incoming),
            BackupBrowserTargetWriteResult::kRefusedBeforeMutation);
  EXPECT_FALSE(absent.ReadBack());

  TestingPrefServiceSimple unregistered;
  ChromiumBackupBrowserPreferenceTargetWriter missing(&unregistered);
  EXPECT_EQ(missing.WriteAndReadBack(incoming),
            BackupBrowserTargetWriteResult::kRefusedBeforeMutation);
  EXPECT_FALSE(missing.ReadBack());
}

}  // namespace
}  // namespace taffy
