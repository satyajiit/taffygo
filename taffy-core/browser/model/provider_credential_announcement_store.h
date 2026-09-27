// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_MODEL_PROVIDER_CREDENTIAL_ANNOUNCEMENT_STORE_H_
#define TAFFY_BROWSER_MODEL_PROVIDER_CREDENTIAL_ANNOUNCEMENT_STORE_H_

#include <optional>
#include <string>
#include <vector>

#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"

class PrefService;

namespace taffy {

// What the browser told the core about one provider's credential, held so it
// can tell the next core generation the same thing (decision 0117).
//
// It is a record of an announcement and never a record of material. `handle`
// names a sealed record in the Android store; nothing here can be spent, and
// a handle whose record has gone answers no credential at resolve time, which
// refuses the request rather than repairing anything.
//
// `state` is carried because a restart that promoted a credential the browser
// knows needs a sign-in back to usable would answer a person with a failing
// request instead of the sign-in they can actually act on.
struct ProviderCredentialAnnouncement {
  std::string provider_id;
  core_api::mojom::ProviderAuthMethodView auth_method =
      core_api::mojom::ProviderAuthMethodView::kApiKey;
  std::string handle;
  core_api::mojom::ProviderCredentialStateView state =
      core_api::mojom::ProviderCredentialStateView::kUsable;
};

// Every announced credential, in the order the preference dictionary iterates.
//
// A row that does not read back whole is skipped rather than repaired: the
// file is the browser's own, but it is still a file, and a torn value must not
// become a credential nobody saved. Both closed enumerations are read by
// exact value, so a member this build does not carry drops the row instead of
// arriving as whichever member sorted first. The result is bounded by
// `kMaxProviderRosterEntries` for the reason the roster is — this list becomes
// one command per entry.
std::vector<ProviderCredentialAnnouncement>
ReadProviderCredentialAnnouncements(const PrefService &prefs);

// The announcement for one provider, or nothing.
//
// Answered from `ReadProviderCredentialAnnouncements` so the single lookup and
// the listing can never disagree about a row, including about which rows a
// bound leaves out.
std::optional<ProviderCredentialAnnouncement>
ReadProviderCredentialAnnouncement(const PrefService &prefs,
                                   const std::string &provider_id);

// Records that one provider's credential was announced, replacing whatever was
// recorded before, and files it as usable.
//
// False means nothing was written, and the caller must not forward the command
// either: a core holding a credential this file does not hold would be
// disagreeing with the file from the next start, and the disagreement is
// invisible — the row reads connected and every request through it is refused.
bool WriteProviderCredentialAnnouncement(
    PrefService *prefs, const std::string &provider_id,
    core_api::mojom::ProviderAuthMethodView auth_method,
    const std::string &handle);

// Records the state one provider's credential is now in.
//
// False when no announcement is held for that provider, which is the honest
// answer rather than a row invented from a state report: a state is a fact
// about a credential, and there is no credential here to state it about.
bool WriteProviderCredentialAnnouncementState(
    PrefService *prefs, const std::string &provider_id,
    core_api::mojom::ProviderCredentialStateView state);

// Removes one provider's announcement. True when the register no longer holds
// one, which includes a provider that never had one: the state afterwards is
// the state that was asked for.
bool ForgetProviderCredentialAnnouncement(PrefService *prefs,
                                          const std::string &provider_id);

} // namespace taffy

#endif // TAFFY_BROWSER_MODEL_PROVIDER_CREDENTIAL_ANNOUNCEMENT_STORE_H_
