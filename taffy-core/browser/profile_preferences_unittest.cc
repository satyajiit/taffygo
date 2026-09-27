// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/profile_preferences.h"

#include "base/memory/scoped_refptr.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_store.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::profile_preferences {
namespace {

TEST(ProfilePreferencesTest, RegistersEveryAndroidProfileValue) {
  auto registry = base::MakeRefCounted<PrefRegistrySimple>();
  RegisterProfilePreferences(registry.get());

  const char* const names[] = {
      kTheme,
      kAppLanguage,
      kRegionCode,
      kPseudoLocalization,
      kForceDarkWeb,
      kProviderRoute,
      kNotificationTopics,
      kOnboardingCompleted,
      kDiagnosticsOptIn,
      kFrequentSites,
      kTimeOnSites,
      kProviderCredentialRecords,
      kSyncKeyRecords,
  };
  for (const char* name : names) {
    const base::Value* value = nullptr;
    EXPECT_TRUE(registry->defaults()->GetValue(name, &value)) << name;
    EXPECT_NE(value, nullptr) << name;
  }
}

TEST(ProfilePreferencesTest, SecurityDefaultsContainNoCredentialMaterial) {
  auto registry = base::MakeRefCounted<PrefRegistrySimple>();
  RegisterProfilePreferences(registry.get());

  const base::Value* credential_records = nullptr;
  ASSERT_TRUE(registry->defaults()->GetValue(kProviderCredentialRecords,
                                             &credential_records));
  ASSERT_NE(credential_records, nullptr);
  EXPECT_TRUE(credential_records->GetString().empty());

  const base::Value* sync_key_records = nullptr;
  ASSERT_TRUE(
      registry->defaults()->GetValue(kSyncKeyRecords, &sync_key_records));
  ASSERT_NE(sync_key_records, nullptr);
  EXPECT_TRUE(sync_key_records->GetString().empty());
}

}  // namespace
}  // namespace taffy::profile_preferences
