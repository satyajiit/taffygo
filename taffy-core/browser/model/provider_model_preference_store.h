// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_MODEL_PROVIDER_MODEL_PREFERENCE_STORE_H_
#define TAFFY_BROWSER_MODEL_PROVIDER_MODEL_PREFERENCE_STORE_H_

#include <optional>
#include <string>
#include <vector>

#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"

class PrefService;

namespace taffy {

// One provider's standing model choice, as the browser holds it.
//
// Both fields are optional and they mean different things when absent
// (decision 0093 section 3). No model id is "whatever order the provider
// itself offers"; no thinking level is "Taffy decides", which is not the same
// answer as `kOff`, and is why the level is an optional rather than an
// enumeration with a member meaning no member.
//
// An entry with neither is not a preference at all, and the store holds none:
// writing both absent removes the row.
struct ProviderModelPreference {
  std::string provider_id;
  std::optional<std::string> model_id;
  std::optional<core_api::mojom::ThinkingLevelView> thinking_level;
};

// Every stored choice, in the order the preference dictionary iterates.
//
// A row that does not read back as a preference is skipped rather than
// repaired: the file is the browser's own, but it is still a file, and a torn
// or hand-edited value must not become a choice nobody made. The result is
// bounded by `kMaxProviderRosterEntries` for the same reason the roster is —
// this list becomes one command per entry.
std::vector<ProviderModelPreference>
ReadProviderModelPreferences(const PrefService &prefs);

// The choice for one provider, as it should now stand.
//
// Whole-state, never a change to apply: passing an absent model id clears any
// stored one rather than leaving it, so no stored row can show a model this
// person never chose beside a thinking level they did.
//
// False means nothing was written, and the caller must not forward the
// command either — the browser file is the authority, and a core holding a
// choice this store could not replay would disagree with it at the next
// start. The one reachable refusal is a new provider past the roster bound: a
// preference about a provider that cannot appear on the roster is a
// preference about a provider that cannot exist.
bool WriteProviderModelPreference(
    PrefService *prefs, const std::string &provider_id,
    const std::optional<std::string> &model_id,
    std::optional<core_api::mojom::ThinkingLevelView> thinking_level);

}  // namespace taffy

#endif  // TAFFY_BROWSER_MODEL_PROVIDER_MODEL_PREFERENCE_STORE_H_
