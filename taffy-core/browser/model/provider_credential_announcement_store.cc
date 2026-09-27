// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The browser's half of decision 0117 for a catalog provider's credential:
// what the core was told, held where it survives a restart so the next core
// generation can be told the same thing.
//
// It holds no material and it is not a second authority over what material
// exists. The sealed record stays in the Android store; a row here names it
// and says what the browser last reported about it. A row naming a record the
// store no longer holds resolves to no credential at request time and the
// request is refused, which is what a stale handle has always done.
//
// Both enumerations are stored under their contract spelling rather than
// their wire number, for `provider_model_preference_store.cc`'s reason: a
// number on disk is an ordinal, and an enumeration that is ever renumbered
// would read every stored row back as a different member with nothing to say
// so. A name either reads back as itself or does not read back at all, and
// the second is a row this file drops.

#include "taffy/browser/model/provider_credential_announcement_store.h"

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
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;

constexpr char kAuthMethodKey[] = "auth_method";
constexpr char kHandleKey[] = "handle";
constexpr char kStateKey[] = "state";

struct StoredAuthMethod {
  std::string_view name;
  api::ProviderAuthMethodView method;
};

struct StoredCredentialState {
  std::string_view name;
  api::ProviderCredentialStateView state;
};

// One table per enumeration and one for both directions, because two would be
// two lists that must agree and nothing would compare them.
constexpr StoredAuthMethod kAuthMethods[] = {
    {"API_KEY", api::ProviderAuthMethodView::kApiKey},
    {"OAUTH", api::ProviderAuthMethodView::kOauth},
};

constexpr StoredCredentialState kCredentialStates[] = {
    {"USABLE", api::ProviderCredentialStateView::kUsable},
    {"NEEDS_SIGN_IN", api::ProviderCredentialStateView::kNeedsSignIn},
    {"REFRESH_FAILED", api::ProviderCredentialStateView::kRefreshFailed},
};

// Compared against the contract's own maxima rather than against literals, so
// a member added to either closed enumeration fails to compile here instead of
// becoming a member this store silently cannot spell.
static_assert(std::size(kAuthMethods) ==
                  static_cast<size_t>(api::ProviderAuthMethodView::kMaxValue) +
                      1u,
              "every provider auth method needs a stored name");
static_assert(
    std::size(kCredentialStates) ==
        static_cast<size_t>(api::ProviderCredentialStateView::kMaxValue) + 1u,
    "every provider credential state needs a stored name");

std::optional<api::ProviderAuthMethodView> MethodFromName(
    std::string_view name) {
  for (const StoredAuthMethod &known : kAuthMethods) {
    if (known.name == name) {
      return known.method;
    }
  }
  return std::nullopt;
}

std::string_view NameForMethod(api::ProviderAuthMethodView method) {
  for (const StoredAuthMethod &known : kAuthMethods) {
    if (known.method == method) {
      return known.name;
    }
  }
  return std::string_view();
}

std::optional<api::ProviderCredentialStateView> StateFromName(
    std::string_view name) {
  for (const StoredCredentialState &known : kCredentialStates) {
    if (known.name == name) {
      return known.state;
    }
  }
  return std::nullopt;
}

std::string_view NameForState(api::ProviderCredentialStateView state) {
  for (const StoredCredentialState &known : kCredentialStates) {
    if (known.state == state) {
      return known.name;
    }
  }
  return std::string_view();
}

// Whether a stored handle reads back as one at all.
//
// Length and emptiness only, the same question the address register asks of an
// address. What a handle may contain is the secure store's rule and the
// command factory's; asking it again here would be a second authority over a
// name this file only carries.
bool ReadsBackAsAHandle(const std::string &value) {
  return !value.empty() &&
         value.size() <= static_cast<size_t>(api::kMaxIdentifierBytes);
}

}  // namespace

std::vector<ProviderCredentialAnnouncement>
ReadProviderCredentialAnnouncements(const PrefService &prefs) {
  std::vector<ProviderCredentialAnnouncement> announced;
  const base::DictValue &stored =
      prefs.GetDict(profile_preferences::kProviderCredentialAnnouncements);
  for (const auto [provider_id, value] : stored) {
    if (announced.size() >=
        static_cast<size_t>(api::kMaxProviderRosterEntries)) {
      break;
    }
    const base::DictValue *entry = value.GetIfDict();
    if (provider_id.empty() || !entry) {
      continue;
    }
    const std::string *method_name = entry->FindString(kAuthMethodKey);
    const std::string *handle = entry->FindString(kHandleKey);
    const std::string *state_name = entry->FindString(kStateKey);
    if (!method_name || !handle || !state_name ||
        !ReadsBackAsAHandle(*handle)) {
      continue;
    }
    const std::optional<api::ProviderAuthMethodView> method =
        MethodFromName(*method_name);
    const std::optional<api::ProviderCredentialStateView> state =
        StateFromName(*state_name);
    // A name this build cannot spell drops the whole row rather than one
    // field. Either half defaulted would be a claim about somebody's
    // credential that nobody made: a method decides which vendor rule the
    // core applies, and a state decides whether a request is sent at all.
    if (!method || !state) {
      continue;
    }
    announced.push_back(ProviderCredentialAnnouncement{provider_id, *method,
                                                       *handle, *state});
  }
  return announced;
}

std::optional<ProviderCredentialAnnouncement>
ReadProviderCredentialAnnouncement(const PrefService &prefs,
                                   const std::string &provider_id) {
  if (provider_id.empty()) {
    return std::nullopt;
  }
  for (ProviderCredentialAnnouncement &entry :
       ReadProviderCredentialAnnouncements(prefs)) {
    if (entry.provider_id == provider_id) {
      return std::move(entry);
    }
  }
  return std::nullopt;
}

bool WriteProviderCredentialAnnouncement(
    PrefService *prefs, const std::string &provider_id,
    api::ProviderAuthMethodView auth_method, const std::string &handle) {
  if (!prefs || provider_id.empty() || !ReadsBackAsAHandle(handle)) {
    return false;
  }
  const std::string_view method_name = NameForMethod(auth_method);
  if (method_name.empty()) {
    return false;
  }
  base::DictValue stored =
      prefs->GetDict(profile_preferences::kProviderCredentialAnnouncements)
          .Clone();
  // The bound refuses a new provider and never an existing one. Replacing a
  // credential a person already has is not what a roster bound is about.
  if (!stored.contains(provider_id) &&
      stored.size() >= static_cast<size_t>(api::kMaxProviderRosterEntries)) {
    return false;
  }
  base::DictValue entry;
  entry.Set(kAuthMethodKey, method_name);
  entry.Set(kHandleKey, handle);
  // A save states a credential that is meant to work. Anything else it might
  // be in is a later report, and carrying a previous row's state through a
  // replacement would file the old credential's trouble against the new one.
  entry.Set(kStateKey, NameForState(api::ProviderCredentialStateView::kUsable));
  stored.Set(provider_id, std::move(entry));
  prefs->SetDict(profile_preferences::kProviderCredentialAnnouncements,
                 std::move(stored));
  return true;
}

bool WriteProviderCredentialAnnouncementState(
    PrefService *prefs, const std::string &provider_id,
    api::ProviderCredentialStateView state) {
  if (!prefs || provider_id.empty()) {
    return false;
  }
  const std::string_view state_name = NameForState(state);
  if (state_name.empty()) {
    return false;
  }
  base::DictValue stored =
      prefs->GetDict(profile_preferences::kProviderCredentialAnnouncements)
          .Clone();
  base::DictValue *entry = stored.FindDict(provider_id);
  if (!entry) {
    return false;
  }
  entry->Set(kStateKey, state_name);
  prefs->SetDict(profile_preferences::kProviderCredentialAnnouncements,
                 std::move(stored));
  return true;
}

bool ForgetProviderCredentialAnnouncement(PrefService *prefs,
                                          const std::string &provider_id) {
  if (!prefs || provider_id.empty()) {
    return false;
  }
  base::DictValue stored =
      prefs->GetDict(profile_preferences::kProviderCredentialAnnouncements)
          .Clone();
  stored.Remove(provider_id);
  prefs->SetDict(profile_preferences::kProviderCredentialAnnouncements,
                 std::move(stored));
  return true;
}

}  // namespace taffy
