// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/feature_posture.h"

#include <memory>
#include <set>
#include <string>

#include "base/feature_list.h"
#include "base/memory/scoped_refptr.h"
#include "base/test/scoped_feature_list.h"
#include "build/build_config.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_store.h"
#include "testing/gtest/include/gtest/gtest.h"

#if BUILDFLAG(IS_ANDROID)
#include "chrome/browser/download/download_prompt_status.h"  // nogncheck
#include "chrome/common/pref_names.h"
#endif

namespace taffy {
namespace {

// The posture lists are data walked by product and test alike (the same rule
// the security posture's switch table follows). These tests walk them the
// way the product does — through a real FeatureList and a real registry —
// so a list entry cannot pass review without being provably applied.

TEST(FeaturePostureTest, EveryListedFeatureLandsInItsPostureStateAfterApply) {
  auto feature_list = std::make_unique<base::FeatureList>();
  ApplyFeaturePosture(feature_list.get());

  // Freeze the list the way startup does, restoring the previous instance
  // when the test ends so other suites see an untouched world.
  base::test::ScopedFeatureList scoped;
  scoped.InitWithFeatureList(std::move(feature_list));

  for (const FeaturePostureEntry& entry : FeaturePostureForTesting()) {
    const bool expect_enabled =
        entry.state ==
        base::FeatureList::OverrideState::OVERRIDE_ENABLE_FEATURE;
    EXPECT_EQ(base::FeatureList::IsEnabled(*entry.feature), expect_enabled)
        << entry.feature->name
        << " is not in its posture state after ApplyFeaturePosture; its "
        << "reason: " << entry.reason;
  }
}

TEST(FeaturePostureTest, ListsCarryNoDuplicatesAndEveryEntryHasAReason) {
  std::set<std::string> feature_names;
  for (const FeaturePostureEntry& entry : FeaturePostureForTesting()) {
    ASSERT_NE(entry.feature, nullptr);
    EXPECT_TRUE(feature_names.insert(entry.feature->name).second)
        << "duplicate feature entry: " << entry.feature->name;
    EXPECT_NE(std::string(entry.reason), "")
        << entry.feature->name << " has no recorded reason";
  }
  std::set<std::string> pref_names;
  for (const PrefPostureEntry& entry : PrefPostureForTesting()) {
    EXPECT_TRUE(pref_names.insert(entry.pref_name).second)
        << "duplicate pref entry: " << entry.pref_name;
    EXPECT_NE(std::string(entry.reason), "")
        << entry.pref_name << " has no recorded reason";
  }
}

TEST(FeaturePostureTest, PrefDefaultsAreOverriddenOnARealRegistry) {
  // Register the prefs with the *opposite* of the posture default, exactly
  // as upstream registration does today, then apply the posture and read the
  // defaults back. Registration by name here is deliberate: the names come
  // from the same upstream headers the product list uses.
  auto registry = base::MakeRefCounted<PrefRegistrySimple>();
  for (const PrefPostureEntry& entry : PrefPostureForTesting()) {
    registry->RegisterBooleanPref(entry.pref_name, !entry.default_value);
  }
#if BUILDFLAG(IS_ANDROID)
  // Registered by DownloadPrefs before the override runs in the product.
  registry->RegisterIntegerPref(
      prefs::kPromptForDownloadAndroid,
      static_cast<int>(DownloadPromptStatus::SHOW_INITIAL));
#endif

  OverrideProfilePrefDefaults(registry.get());

  for (const PrefPostureEntry& entry : PrefPostureForTesting()) {
    const base::Value* value = nullptr;
    ASSERT_TRUE(registry->defaults()->GetValue(entry.pref_name, &value))
        << entry.pref_name;
    ASSERT_NE(value, nullptr) << entry.pref_name;
    EXPECT_EQ(value->GetBool(), entry.default_value)
        << entry.pref_name
        << " default was not overridden; its reason: " << entry.reason;
  }
#if BUILDFLAG(IS_ANDROID)
  // The first-download prompt has no dialog host in this shell, and an
  // unhosted prompt cancels the download it was asking about.
  const base::Value* prompt = nullptr;
  ASSERT_TRUE(
      registry->defaults()->GetValue(prefs::kPromptForDownloadAndroid, &prompt));
  ASSERT_NE(prompt, nullptr);
  EXPECT_EQ(prompt->GetInt(), static_cast<int>(DownloadPromptStatus::DONT_SHOW));
#endif
}

}  // namespace
}  // namespace taffy
