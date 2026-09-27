// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What the browser's own preference file holds about a person's model choice,
// and what it refuses to hold.
//
// Two properties are the reason this file exists. The first is that a stored
// row is spelled rather than numbered: the level goes to disk under its
// contract name, so a renumbered enumeration cannot read every stored row back
// as a different rung. The second is that a row which does not read back as a
// preference is dropped rather than repaired — the file is the browser's own,
// but it is still a file, and a choice nobody made must not reach the core
// because a torn value happened to parse.

#include "taffy/browser/model/provider_model_preference_store.h"

#include <stddef.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/values.h"
#include "components/prefs/testing_pref_service.h"
#include "taffy/browser/profile_preferences.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;

class ProviderModelPreferenceStoreTest : public testing::Test {
protected:
  ProviderModelPreferenceStoreTest() {
    profile_preferences::RegisterProfilePreferences(prefs_.registry());
  }

  // The stored dictionary as it actually is on disk, for the cases that are
  // about a value this store did not write.
  void PutRaw(base::DictValue stored) {
    prefs_.SetDict(profile_preferences::kProviderModelPreferences,
                   std::move(stored));
  }

  std::vector<ProviderModelPreference> Read() {
    return ReadProviderModelPreferences(prefs_);
  }

  TestingPrefServiceSimple prefs_;
};

// Every rung survives the round trip under its own name. A table that mapped
// two rungs onto one name, or a name onto the wrong rung, would answer a
// person's "high" with something else after a restart and nothing would say
// so.
TEST_F(ProviderModelPreferenceStoreTest, EveryThinkingRungRoundTrips) {
  const api::ThinkingLevelView kRungs[] = {
      api::ThinkingLevelView::kOff,    api::ThinkingLevelView::kMinimal,
      api::ThinkingLevelView::kLow,    api::ThinkingLevelView::kMedium,
      api::ThinkingLevelView::kHigh,   api::ThinkingLevelView::kXhigh,
      api::ThinkingLevelView::kMax};

  for (const api::ThinkingLevelView rung : kRungs) {
    ASSERT_TRUE(WriteProviderModelPreference(
        &prefs_, "anthropic", std::optional<std::string>("claude-sonnet-4-5"),
        rung));

    const std::vector<ProviderModelPreference> stored = Read();
    ASSERT_EQ(stored.size(), 1u);
    EXPECT_EQ(stored[0].provider_id, "anthropic");
    EXPECT_EQ(stored[0].model_id,
              std::optional<std::string>("claude-sonnet-4-5"));
    ASSERT_TRUE(stored[0].thinking_level.has_value());
    EXPECT_EQ(*stored[0].thinking_level, rung);
  }
}

// The two absences mean different things and both are kept. No model id is
// "whatever order the provider itself offers"; no thinking level is "Taffy
// decides", which is not the lowest rung.
TEST_F(ProviderModelPreferenceStoreTest, EitherHalfOfAChoiceMayStandAlone) {
  ASSERT_TRUE(WriteProviderModelPreference(
      &prefs_, "anthropic", std::optional<std::string>("claude-sonnet-4-5"),
      std::optional<api::ThinkingLevelView>()));
  ASSERT_TRUE(WriteProviderModelPreference(
      &prefs_, "openai", std::optional<std::string>(),
      api::ThinkingLevelView::kHigh));

  const std::vector<ProviderModelPreference> stored = Read();
  ASSERT_EQ(stored.size(), 2u);
  for (const ProviderModelPreference &preference : stored) {
    if (preference.provider_id == "anthropic") {
      EXPECT_TRUE(preference.model_id.has_value());
      EXPECT_FALSE(preference.thinking_level.has_value());
    } else {
      EXPECT_EQ(preference.provider_id, "openai");
      EXPECT_FALSE(preference.model_id.has_value());
      ASSERT_TRUE(preference.thinking_level.has_value());
      EXPECT_EQ(*preference.thinking_level, api::ThinkingLevelView::kHigh);
    }
  }
}

// A rung this build cannot spell drops the whole row, not the one field.
//
// The command a row becomes states the choice as a whole, so a row reduced to
// its model id would ask for a model beside "Taffy decides" — an answer this
// person never gave. The case is not hypothetical: a file written by a newer
// build and read by an older one is exactly this shape.
TEST_F(ProviderModelPreferenceStoreTest, AnUnspellableRungDropsTheWholeRow) {
  base::DictValue entry;
  entry.Set("model_id", "claude-sonnet-4-5");
  entry.Set("thinking_level", "ULTRA");
  base::DictValue stored;
  stored.Set("anthropic", std::move(entry));
  PutRaw(std::move(stored));

  EXPECT_TRUE(Read().empty());
}

// The rows a file can hold that are not preferences at all: a value that is
// not a dictionary, a dictionary with neither field, and a key with no name.
// Each is skipped rather than repaired.
TEST_F(ProviderModelPreferenceStoreTest, ARowThatIsNotAChoiceIsSkipped) {
  base::DictValue stored;
  stored.Set("anthropic", "claude-sonnet-4-5");
  stored.Set("openai", base::DictValue());
  base::DictValue real;
  real.Set("model_id", "gemini-3-pro");
  stored.Set("google", std::move(real));
  PutRaw(std::move(stored));

  const std::vector<ProviderModelPreference> kept = Read();
  ASSERT_EQ(kept.size(), 1u);
  EXPECT_EQ(kept[0].provider_id, "google");
  EXPECT_EQ(kept[0].model_id, std::optional<std::string>("gemini-3-pro"));
}

// Whole-state, never a change to apply: a write with no model id clears the
// stored one rather than leaving it, so no row can show a model this person
// never chose beside a thinking level they did.
TEST_F(ProviderModelPreferenceStoreTest, AWriteReplacesTheRowRatherThanMerging) {
  ASSERT_TRUE(WriteProviderModelPreference(
      &prefs_, "anthropic", std::optional<std::string>("claude-sonnet-4-5"),
      api::ThinkingLevelView::kHigh));
  ASSERT_TRUE(WriteProviderModelPreference(
      &prefs_, "anthropic", std::optional<std::string>(),
      api::ThinkingLevelView::kLow));

  const std::vector<ProviderModelPreference> stored = Read();
  ASSERT_EQ(stored.size(), 1u);
  EXPECT_FALSE(stored[0].model_id.has_value());
  ASSERT_TRUE(stored[0].thinking_level.has_value());
  EXPECT_EQ(*stored[0].thinking_level, api::ThinkingLevelView::kLow);
}

// Both fields absent removes the row, and succeeds. An entry with neither is
// not a preference, and refusing the request would leave a person able to make
// a choice and unable to unmake one.
TEST_F(ProviderModelPreferenceStoreTest, ClearingRemovesTheRowAndSucceeds) {
  ASSERT_TRUE(WriteProviderModelPreference(
      &prefs_, "anthropic", std::optional<std::string>("claude-sonnet-4-5"),
      api::ThinkingLevelView::kHigh));

  EXPECT_TRUE(WriteProviderModelPreference(
      &prefs_, "anthropic", std::optional<std::string>(),
      std::optional<api::ThinkingLevelView>()));
  EXPECT_TRUE(Read().empty());

  // And clearing a provider that was never stored is not a failure either:
  // the state afterwards is the state that was asked for.
  EXPECT_TRUE(WriteProviderModelPreference(
      &prefs_, "openai", std::optional<std::string>(),
      std::optional<api::ThinkingLevelView>()));
}

// The roster bound, which this list becomes one command per entry of. A new
// provider past it is refused — a preference about a provider that cannot
// appear on the roster is a preference about a provider that cannot exist —
// while a provider already stored may still be changed.
TEST_F(ProviderModelPreferenceStoreTest, TheRosterBoundRefusesOnlyNewRows) {
  const size_t bound = static_cast<size_t>(api::kMaxProviderRosterEntries);
  for (size_t index = 0; index < bound; ++index) {
    ASSERT_TRUE(WriteProviderModelPreference(
        &prefs_, "provider-" + std::to_string(index),
        std::optional<std::string>("model-a"),
        std::optional<api::ThinkingLevelView>()))
        << index;
  }
  EXPECT_EQ(Read().size(), bound);

  EXPECT_FALSE(WriteProviderModelPreference(
      &prefs_, "one-too-many", std::optional<std::string>("model-a"),
      std::optional<api::ThinkingLevelView>()));
  EXPECT_TRUE(WriteProviderModelPreference(
      &prefs_, "provider-0", std::optional<std::string>("model-b"),
      api::ThinkingLevelView::kMedium));
  EXPECT_EQ(Read().size(), bound);
}

// Two refusals that are about the caller rather than about the file: a profile
// with no preference service, and an identity a dictionary cannot be keyed by.
// Both answer false, which the facade reads as "do not forward either".
TEST_F(ProviderModelPreferenceStoreTest, AWriteWithNowhereToGoAnswersFalse) {
  EXPECT_FALSE(WriteProviderModelPreference(
      nullptr, "anthropic", std::optional<std::string>("claude-sonnet-4-5"),
      api::ThinkingLevelView::kHigh));
  EXPECT_FALSE(WriteProviderModelPreference(
      &prefs_, "", std::optional<std::string>("claude-sonnet-4-5"),
      api::ThinkingLevelView::kHigh));
  EXPECT_TRUE(Read().empty());
}

} // namespace
} // namespace taffy
