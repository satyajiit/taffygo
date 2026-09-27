// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PROVIDERAUTH_AUTH_CALLBACK_PARSER_H_
#define TAFFY_BROWSER_PROVIDERAUTH_AUTH_CALLBACK_PARSER_H_

#include <optional>
#include <string>
#include <string_view>

namespace taffy {

// One parser, two callback paths, and it lives with the one that survives.
//
// This file was `browser/account/account_auth_callback_parser.*` and moved
// here ahead of the account plane's removal (decision 0200), because the
// provider half is what is left afterwards: a vendor subscription's OAuth
// leg is bring-your-own traffic to a host the person chose, which decision
// 0200 keeps. Moving it first means the account plane's directory can be
// deleted whole rather than picked over.
//
// The type names still say `Account` on purpose. Both flows parse the same
// shape and share `ParseCallbackWithPrefix` below, so renaming the shared
// type in the same change that moves the file would make the move
// unreviewable — every hunk would be a rename and the reader could not see
// that nothing else changed. The rename belongs with the directory rename
// that follows the account plane out.

enum class AccountAuthCallbackOutcome {
  kAuthorizationCode,
  kDenied,
  kProviderError,
};

struct ParsedAccountAuthCallback {
  std::string state;
  std::optional<std::string> authorization_code;
  AccountAuthCallbackOutcome outcome =
      AccountAuthCallbackOutcome::kProviderError;
};

// Parses the fixed Android callback origin for the account flow. Success is
// query-only. Provider errors may repeat their bounded error facts in the
// fragment, but duplicated values must agree. Descriptions and the Supabase
// `sb` marker are validated and discarded; they never enter logs, UI state,
// or the portable protocol. This entry point leaves with the account plane;
// its twin below is the one this directory owns.
std::optional<ParsedAccountAuthCallback> ParseAccountAuthCallback(
    std::string_view raw_uri);

// The provider-subscription twin (decision 0081): identical rules over the
// provider callback path `com.taffygo.browser://provider-auth`, so the
// redirect router can tell the two flows apart by prefix before either
// broker parses anything. The parsed shape is deliberately the same type.
std::optional<ParsedAccountAuthCallback> ParseProviderAuthCallback(
    std::string_view raw_uri);

}  // namespace taffy

#endif  // TAFFY_BROWSER_PROVIDERAUTH_AUTH_CALLBACK_PARSER_H_
