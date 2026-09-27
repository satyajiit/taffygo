// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The browser's half of decision 0093: what a person chose, held where it
// survives a restart and can be read before the isolated core is up.
//
// The level is stored under its contract spelling rather than its wire
// number. A number on disk is an ordinal, and an enumeration that is ever
// renumbered would read every stored row back as a different rung with
// nothing to say so; a name either reads back as itself or does not read back
// at all, and the second is a row this file drops.

#include "taffy/browser/model/provider_model_preference_store.h"

#include <stddef.h>

#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/values.h"
#include "components/prefs/pref_service.h"
#include "taffy/browser/profile_preferences.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;

constexpr char kModelIdKey[] = "model_id";
constexpr char kThinkingLevelKey[] = "thinking_level";

struct StoredThinkingLevel {
  std::string_view name;
  api::ThinkingLevelView level;
};

// The seven rungs, spelled as the contract spells them. One table for both
// directions, because two would be two lists that must agree and nothing
// would compare them.
constexpr StoredThinkingLevel kThinkingLevels[] = {
    {"OFF", api::ThinkingLevelView::kOff},
    {"MINIMAL", api::ThinkingLevelView::kMinimal},
    {"LOW", api::ThinkingLevelView::kLow},
    {"MEDIUM", api::ThinkingLevelView::kMedium},
    {"HIGH", api::ThinkingLevelView::kHigh},
    {"XHIGH", api::ThinkingLevelView::kXhigh},
    {"MAX", api::ThinkingLevelView::kMax},
};

// Compared against the contract's own count rather than against a literal, so
// a rung added to the schema is a failure here instead of a rung this store
// silently cannot spell.
static_assert(std::size(kThinkingLevels) == api::kMaxModelThinkingLevels,
              "every thinking rung the contract declares needs a stored name");

std::optional<api::ThinkingLevelView> LevelFromName(std::string_view name) {
  for (const StoredThinkingLevel &known : kThinkingLevels) {
    if (known.name == name) {
      return known.level;
    }
  }
  return std::nullopt;
}

std::string_view NameForLevel(api::ThinkingLevelView level) {
  for (const StoredThinkingLevel &known : kThinkingLevels) {
    if (known.level == level) {
      return known.name;
    }
  }
  return std::string_view();
}

}  // namespace

std::vector<ProviderModelPreference>
ReadProviderModelPreferences(const PrefService &prefs) {
  std::vector<ProviderModelPreference> preferences;
  const base::DictValue &stored =
      prefs.GetDict(profile_preferences::kProviderModelPreferences);
  for (const auto [provider_id, value] : stored) {
    if (preferences.size() >=
        static_cast<size_t>(api::kMaxProviderRosterEntries)) {
      break;
    }
    const base::DictValue *entry = value.GetIfDict();
    if (provider_id.empty() || !entry) {
      continue;
    }
    ProviderModelPreference preference;
    preference.provider_id = provider_id;
    if (const std::string *model_id = entry->FindString(kModelIdKey)) {
      preference.model_id = *model_id;
    }
    if (const std::string *level = entry->FindString(kThinkingLevelKey)) {
      preference.thinking_level = LevelFromName(*level);
      // A name this build cannot spell drops the whole row rather than the
      // one field. The command it becomes states the choice as a whole
      // (decision 0093 section 1), so a row reduced to its model id would ask
      // for a model beside "Taffy decides" — an answer this person never
      // gave.
      if (!preference.thinking_level) {
        continue;
      }
    }
    if (!preference.model_id && !preference.thinking_level) {
      continue;
    }
    preferences.push_back(std::move(preference));
  }
  return preferences;
}

bool WriteProviderModelPreference(
    PrefService *prefs, const std::string &provider_id,
    const std::optional<std::string> &model_id,
    std::optional<api::ThinkingLevelView> thinking_level) {
  if (!prefs || provider_id.empty()) {
    return false;
  }
  base::DictValue stored =
      prefs->GetDict(profile_preferences::kProviderModelPreferences).Clone();
  if (!model_id && !thinking_level) {
    stored.Remove(provider_id);
    prefs->SetDict(profile_preferences::kProviderModelPreferences,
                   std::move(stored));
    return true;
  }
  if (!stored.contains(provider_id) &&
      stored.size() >= static_cast<size_t>(api::kMaxProviderRosterEntries)) {
    return false;
  }
  base::DictValue entry;
  if (model_id) {
    entry.Set(kModelIdKey, *model_id);
  }
  if (thinking_level) {
    const std::string_view name = NameForLevel(*thinking_level);
    if (name.empty()) {
      return false;
    }
    entry.Set(kThinkingLevelKey, name);
  }
  stored.Set(provider_id, std::move(entry));
  prefs->SetDict(profile_preferences::kProviderModelPreferences,
                 std::move(stored));
  return true;
}

}  // namespace taffy
