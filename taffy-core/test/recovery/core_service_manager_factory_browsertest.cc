// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_service_manager_factory.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/test/bind.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/test/base/platform_browser_test.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "taffy/browser/application_preferences.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

void DestroyPrimaryOffTheRecordProfile(Profile* regular_profile) {
  Profile* const private_profile =
      regular_profile->GetPrimaryOTRProfile(/*create_if_needed=*/false);
  if (private_profile) {
    regular_profile->DestroyOffTheRecordProfile(private_profile);
  }
}

class CoreServiceManagerFactoryBrowserTest : public PlatformBrowserTest {};

IN_PROC_BROWSER_TEST_F(CoreServiceManagerFactoryBrowserTest,
                       RegularAndPrivateProfilesOwnDistinctManagers) {
  Profile* const regular_profile = GetProfile();
  ASSERT_TRUE(regular_profile);
  ASSERT_FALSE(regular_profile->IsOffTheRecord());
  ASSERT_FALSE(regular_profile->HasPrimaryOTRProfile());

  CoreServiceManager* const regular_manager =
      CoreServiceManagerFactory::GetForProfile(regular_profile);
  ASSERT_TRUE(regular_manager);

  Profile* const private_profile =
      regular_profile->GetPrimaryOTRProfile(/*create_if_needed=*/true);
  ASSERT_TRUE(private_profile);
  base::ScopedClosureRunner destroy_private_profile(base::BindOnce(
      &DestroyPrimaryOffTheRecordProfile, base::Unretained(regular_profile)));

  ASSERT_TRUE(private_profile->IsOffTheRecord());
  EXPECT_EQ(regular_profile, private_profile->GetOriginalProfile());

  CoreServiceManager* const private_manager =
      CoreServiceManagerFactory::GetForProfile(private_profile);
  ASSERT_TRUE(private_manager);
  EXPECT_NE(regular_manager, private_manager);
  EXPECT_EQ(regular_manager,
            CoreServiceManagerFactory::GetForProfile(regular_profile));
  EXPECT_EQ(private_manager,
            CoreServiceManagerFactory::GetForProfile(private_profile));
  EXPECT_EQ(regular_manager,
            CoreServiceManagerFactory::GetForBrowserContext(regular_profile));
  EXPECT_EQ(private_manager,
            CoreServiceManagerFactory::GetForBrowserContext(private_profile));

  destroy_private_profile.RunAndReset();
  EXPECT_FALSE(regular_profile->HasPrimaryOTRProfile());
  EXPECT_EQ(regular_manager,
            CoreServiceManagerFactory::GetForProfileIfExists(regular_profile));
}

IN_PROC_BROWSER_TEST_F(CoreServiceManagerFactoryBrowserTest,
                       AClosedAccessGateCannotConcealAnExistingInstance) {
  Profile* const profile = GetProfile();
  ASSERT_TRUE(profile);
  ASSERT_TRUE(g_browser_process);
  PrefService* const local_state = g_browser_process->local_state();
  ASSERT_TRUE(local_state);
  const char* const preference =
      application_preferences::kBackupRestoreProfileReservations;
  ASSERT_TRUE(local_state->FindPreference(preference));
  ASSERT_TRUE(CoreServiceManagerFactory::GetForProfile(profile));
  EXPECT_TRUE(
      CoreServiceManagerFactory::HasExistingInstanceForProfile(profile));
  EXPECT_FALSE(
      CoreServiceManagerFactory::HasExistingInstanceForProfile(nullptr));

  const bool had_user_value =
      local_state->FindPreference(preference)->HasUserSetting();
  base::DictValue original = local_state->GetDict(preference).Clone();
  base::ScopedClosureRunner restore_preference(base::BindLambdaForTesting([&] {
    if (had_user_value) {
      local_state->SetDict(preference, std::move(original));
    } else {
      local_state->ClearPref(preference);
    }
  }));
  base::DictValue corrupt;
  corrupt.Set("not-a-reservation", true);
  local_state->SetDict(preference, std::move(corrupt));
  EXPECT_EQ(nullptr, CoreServiceManagerFactory::GetForProfile(profile));
  EXPECT_EQ(nullptr, CoreServiceManagerFactory::GetForProfileIfExists(profile));
  EXPECT_TRUE(
      CoreServiceManagerFactory::HasExistingInstanceForProfile(profile));
}

}  // namespace
}  // namespace taffy
