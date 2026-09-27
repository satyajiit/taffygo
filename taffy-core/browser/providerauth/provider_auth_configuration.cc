// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/providerauth/provider_auth_configuration.h"

#include <stddef.h>

#include <array>
#include <string_view>

#include "base/check.h"

namespace taffy {
namespace {

// The redirect URI the product would register with a vendor of its own. It is
// deliberately not in the header: decision 0095 section 2 reserves it for the
// product's own registrations, and every row this binary carries today
// borrows somebody else's client, whose registered redirect is not ours to
// change. Reachable only through `ProviderAuthRedirectFor`, which hands it
// out for a kCustomScheme row and for nothing else, so "present the product's
// callback to a borrowed client" is not a mistake a caller can make.
//
// The path is distinct from the account plane's callback so the redirect
// router can tell the two flows apart before either broker parses anything.
constexpr char kProviderAuthRedirectUri[] = "com.taffygo.browser://provider-auth";

// The product's own scheme, so a row cannot smuggle the reserved callback in
// through its registered-address column.
constexpr std::string_view kProductScheme = "com.taffygo.browser:";

// The six vendors, alphabetical. Origins are each vendor's published OAuth
// surface for its own consumer products; none of them is served data.
//
// Every row's client identity is borrowed or absent. The identity column and
// the date column are filled in together, by somebody who has read that
// vendor's terms, and that edit is the whole of enabling a vendor — see
// `ProviderAuthVendorRegistered` and the struct's own comment.
//
// Every row is dated, and the dates are two: xAI's review is 2026-09-02, and
// the other six were reviewed on 2026-09-03 by the repository owner, who
// instructed that the product offer each of these sign-ins and gave the same
// reading of each vendor's terms decision 0029 point 4 gives of xAI's — the
// vendor publishes a public client for its own first-party tool, and a person
// signing in with it is spending their own plan. Decision 0113 records that
// act and supersedes decision 0029 point 4's narrower position on OpenAI.
//
// Decision 0112 still holds and is what these dates mean: a date is a claim
// that somebody read that vendor's terms, and it is not a claim that the flow
// works. The flow working is evidence for the verification report; the date
// is not evidence of anything except that a person read something.
//
// The extra authorization parameters OpenAI's own command-line tool sends,
// and one this product decides for itself.
//
// The first two are that tool's, unchanged, because they are what its
// authorization server is asked for and parity with the borrowed tool is the
// mitigation decision 0095 names: the identity token is asked to carry the
// account's organizations, and the vendor's simplified flow is requested so
// the consent screen is the one that tool's users see.
//
// The third is not borrowed and is deliberately not the tool's value. An
// originator names the product making the request, and TaffyGo is not that
// tool. Sending its name would be a claim about who is calling, made to the
// vendor, that is not true — and decision 0095 section 4 already commits this
// product to telling the person whose client they are authorizing, which is
// hard to square with telling the vendor something else. The cost is stated
// rather than hidden: an authorization server that only recognises its own
// originator may refuse this, and only a device attempt can settle it. If it
// does refuse, that is one value in this table, not a change to any flow.
constexpr ProviderAuthAuthorizationParam kOpenAiAuthorizationParams[] = {
    {"id_token_add_organizations", "true"},
    {"codex_cli_simplified_flow", "true"},
    {"originator", "taffygo"},
};

// What this product calls itself to the one endpoint that asks.
//
// GitHub's Copilot session-token endpoint is an internal one of that vendor's
// own, and it refuses a request that does not name an editor. The value is
// this product's name and nothing borrowed: the endpoint is asking which
// client is calling, and the honest answer is the one that costs nothing to
// give. If that vendor ever restricts the field to editors it recognises,
// this is the one value to change, and a refusal will say so in a 400 rather
// than fail somewhere further along.
constexpr char kTaffyGoEditorIdentity[] = "TaffyGo/1.0";

constexpr ProviderAuthAuthorizationParam kCopilotSecondaryHeaders[] = {
    {"editor-version", kTaffyGoEditorIdentity},
    {"editor-plugin-version", kTaffyGoEditorIdentity},
    {"user-agent", kTaffyGoEditorIdentity},
};

constexpr std::array<ProviderAuthVendor, 6> kVendors = {{
    // Anthropic's flow displays the code rather than redirecting, which is
    // what `code=true` asks for, so the person enters what the page showed
    // them. Its `state` is the PKCE verifier rather than an independent
    // nonce; `StartPkceFlow` carries that deviation and explains what it
    // costs.
    //
    // The row is kTabInterception even so, and that is not a contradiction:
    // the strategy licenses the row to carry an address, and manual entry is
    // the standing fallback of every interception row. This client's
    // registered address is a loopback port its command-line tool binds, and
    // the address is not decoration — `redirect_uri` is a required field of
    // this vendor's token exchange, checked against the registration, so an
    // authorization that displayed a code perfectly still fails at the
    // exchange without it. If the vendor ever redirects rather than
    // displaying, the interceptor already matches the same address and the
    // flow finishes with nothing to paste.
    {
        "anthropic",
        ProviderAuthFlowKind::kPkce,
        ProviderAuthRedirectKind::kTabInterception,
        ProviderAuthExchangeKind::kOauthTokenPair,
        "https://claude.ai/oauth/authorize",
        "",
        "https://platform.claude.com/v1/oauth/token",
        "",
        "",
        "",
        "",
        "http://localhost:53692/callback",
        "",
        "Anthropic's Claude command-line tool",
        {},
        {},
        "2026-09-03",
        /*authorization_displays_code=*/true,
        /*state_is_pkce_verifier=*/true,
        /*presents_client_identity=*/true,
        // The public client of that vendor's own command-line tool. The tool
        // ships it base64-encoded rather than in the clear, which obscures it
        // and does not make it a secret: a client id identifies an
        // application and authenticates nothing (decision 0095 section 1).
        // Written here decoded, because a value nobody can read is a value
        // nobody can revoke on purpose.
        "9d1c250a-e61b-44d9-88ed-5944d1962f5e",
        "",
        // The borrowed tool's own scope, unchanged. The last three are what
        // let the credential reach that vendor's session, tool and upload
        // surfaces; asking for fewer than the tool asks for produces a
        // credential the vendor accepts and then refuses in use.
        //
        // One literal and not two adjacent ones, long line and all:
        // `check_vendor_agreement.py` reads this row by counting string
        // literals, and a scope split for width would be counted as two
        // columns and move every read after it.
        // NOLINTNEXTLINE(whitespace/line_length)
        "org:create_api_key user:profile user:inference user:sessions:claude_code user:mcp_servers user:file_upload",
    },
    // The one vendor whose device-flow token is not the API credential: it is
    // exchanged a second time, and that second answer also names the host the
    // credential is good for. Which host that is depends on the plan behind
    // the account, so the row licenses a domain rather than an address and
    // the browser substitutes inside it at send time.
    {
        "github-copilot",
        ProviderAuthFlowKind::kDeviceCode,
        ProviderAuthRedirectKind::kManualCode,
        ProviderAuthExchangeKind::kOauthTokenPair,
        "",
        "https://github.com/login/device/code",
        "https://github.com/login/oauth/access_token",
        "https://api.github.com/copilot_internal/v2/token",
        ".githubcopilot.com",
        "token",
        "",
        "",
        "",
        "GitHub's Copilot command-line client",
        {},
        kCopilotSecondaryHeaders,
        "2026-09-03",
        /*authorization_displays_code=*/false,
        /*state_is_pkce_verifier=*/false,
        /*presents_client_identity=*/true,
        // The public OAuth app that vendor's own editor integration and
        // command-line client present. Its `Iv1.` prefix is that vendor's own
        // marker for a GitHub App client id, which is public by construction:
        // the secret half of a GitHub App is a separate value this flow never
        // has and never needs, because a device-code grant authenticates the
        // person rather than the client.
        "Iv1.b507a08c87ecfe98",
        "",
        "read:user",
    },
    // Both endpoints sit under this vendor's `/api` prefix and neither is
    // where the shape of the other five rows would put it. They were written
    // here by pattern once — `/oauth/device/code` and `/oauth/token`, which
    // is what almost every other vendor serves — and a row of plausible
    // addresses is the one kind of mistake nothing in this tree can catch:
    // every check agrees the table is well shaped, and the vendor answers a
    // person's first sign-in with a 404.
    {
        "kimi-coding",
        ProviderAuthFlowKind::kDeviceCode,
        ProviderAuthRedirectKind::kManualCode,
        ProviderAuthExchangeKind::kOauthTokenPair,
        "",
        "https://auth.kimi.com/api/oauth/device_authorization",
        "https://auth.kimi.com/api/oauth/token",
        "",
        "",
        "",
        "",
        "",
        "",
        "Moonshot's Kimi command-line tool",
        {},
        {},
        "2026-09-03",
        /*authorization_displays_code=*/false,
        /*state_is_pkce_verifier=*/false,
        /*presents_client_identity=*/true,
        "17e5f671-d194-4dfb-9706-5516cb48c098",
        "",
        "",
    },
    // The interception row decision 0095 section 2 was written for: the
    // borrowed client is registered to redirect to a loopback address, which
    // a command-line tool answers by binding a port. This product answers it
    // by matching the navigation and cancelling it before it connects, so
    // nothing listens and the code never reaches a page. The device-code
    // fallback is not recorded here yet; naming that endpoint is all it takes.
    {
        "openai",
        ProviderAuthFlowKind::kPkce,
        ProviderAuthRedirectKind::kTabInterception,
        ProviderAuthExchangeKind::kOauthTokenPair,
        "https://auth.openai.com/oauth/authorize",
        "",
        "https://auth.openai.com/oauth/token",
        "",
        "",
        "",
        "https://auth.openai.com/oauth/revoke",
        "http://localhost:1455/auth/callback",
        "",
        "OpenAI's Codex command-line tool",
        kOpenAiAuthorizationParams,
        {},
        "2026-09-03",
        /*authorization_displays_code=*/false,
        /*state_is_pkce_verifier=*/false,
        /*presents_client_identity=*/true,
        // The public client that vendor's own command-line tool presents. A
        // client id identifies an application and does not authenticate one,
        // so it carries no secret and is published wherever that tool is
        // (decision 0095 section 1). It is borrowed rather than ours, which
        // is what `borrowed_from` above says out loud, and it is revocable by
        // somebody who is not us — which is why naming it here rather than
        // deriving it anywhere gives the row a kill switch a reader can find.
        "app_EMoamEEZ73f0CkXaXp7hrann",
        "",
        "openid profile email offline_access",
    },
    // The exchange mints a permanent API key rather than a token pair, so
    // this row has no refresh leg at all and the credential it produces is a
    // key. Its authorization request names no client: the exchange
    // authenticates by the PKCE verifier alone, which is why the identity
    // column is empty here as a finished state rather than an absent one.
    //
    // Its callback address is the caller's to choose rather than a
    // registration to match, and it is named in this vendor's own parameter
    // rather than in `redirect_uri`, which this vendor ignores. A row that
    // could not rename the parameter would open an authorization page with
    // nowhere to send the answer, and the page would look correct.
    //
    // The address chosen is a loopback one, and that is the whole of the
    // choice: nothing binds the port, the interceptor cancels the navigation
    // before it connects, and if interception ever failed the request would
    // fail locally rather than carry an authorization code to a host. The
    // product's own custom-scheme callback stays reserved for the product's
    // own registrations, and this is not one.
    {
        "openrouter",
        ProviderAuthFlowKind::kPkce,
        ProviderAuthRedirectKind::kTabInterception,
        ProviderAuthExchangeKind::kMintedApiKey,
        "https://openrouter.ai/auth",
        "",
        "https://openrouter.ai/api/v1/auth/keys",
        "",
        "",
        "",
        "",
        "http://127.0.0.1:51455/openrouter-callback",
        "callback_url",
        "",
        {},
        {},
        "2026-09-03",
        /*authorization_displays_code=*/false,
        /*state_is_pkce_verifier=*/false,
        /*presents_client_identity=*/false,
        "",
        "",
        "",
    },
    {
        "xai",
        ProviderAuthFlowKind::kDeviceCode,
        ProviderAuthRedirectKind::kManualCode,
        ProviderAuthExchangeKind::kOauthTokenPair,
        "",
        "https://auth.x.ai/oauth2/device/code",
        "https://auth.x.ai/oauth2/token",
        "",
        "",
        "",
        "",
        "",
        "",
        "xAI's Grok command-line tool",
        {},
        // Read on this date by the repository owner (decision 0081 makes this
        // an owner's act and nothing else may write it). Decision 0029 point 4
        // is the position being acted on: this is the one launch vendor whose
        // terms it records as permitting plan-backed access, and it is the one
        // row that already carries an identity, because the vendor operates a
        // public device-authorization client for exactly this kind of sign-in.
        {},
        "2026-09-02",
        /*authorization_displays_code=*/false,
        /*state_is_pkce_verifier=*/false,
        /*presents_client_identity=*/true,
        "b1a00492-073a-47ea-816f-4c329264a828",
        "",
        "openid profile email offline_access grok-cli:access api:access",
    },
}};

constexpr bool IsEmpty(const char *value) {
  return value == nullptr || value[0] == '\0';
}

// A review date is either absent or a real date. "Reviewed" is a claim a
// person makes, and a claim spelled `yes` would be one nobody could check
// against anything.
constexpr bool IsIsoDateOrAbsent(const char *value) {
  if (value == nullptr) {
    return true;
  }
  const std::string_view date(value);
  if (date.empty()) {
    return true;
  }
  if (date.size() != 10u || date[4] != '-' || date[7] != '-') {
    return false;
  }
  for (size_t index = 0; index < date.size(); ++index) {
    if (index == 4u || index == 7u) {
      continue;
    }
    if (date[index] < '0' || date[index] > '9') {
      return false;
    }
  }
  return true;
}

// A licensed domain, spelled as a suffix: a leading dot and then at least two
// labels, in the alphabet a host name is written in.
//
// The two-label floor is the rule that matters. A suffix of `.com` would
// license every name anybody has ever registered, which is not a bound at all,
// and it would read in the table exactly like a real one. The alphabet check
// is what keeps a scheme, a port or a path out: none of them can be written
// here, so a row cannot license something that is not a domain.
constexpr bool IsDomainSuffixOrAbsent(const char *value) {
  if (value == nullptr || value[0] == '\0') {
    return true;
  }
  const std::string_view suffix(value);
  if (suffix.size() < 4u || suffix.size() > 128u || suffix[0] != '.') {
    return false;
  }
  size_t dots = 0;
  for (size_t index = 0; index < suffix.size(); ++index) {
    const char character = suffix[index];
    if (character == '.') {
      ++dots;
      // A trailing dot or a doubled one would make an empty label, and an
      // empty label matches nothing while looking like it matches something.
      if (index + 1u == suffix.size() || suffix[index + 1u] == '.') {
        return false;
      }
      continue;
    }
    const bool alphanumeric = (character >= 'a' && character <= 'z') ||
                              (character >= '0' && character <= '9');
    if (!alphanumeric && character != '-') {
      return false;
    }
  }
  return dots >= 2u;
}

// Every structural rule the table obeys, checked where the table is written
// rather than where a flow reads it. A row that breaks one of these is a
// sign-in that fails at the vendor, or worse works at the vendor and is wrong
// here, and neither reads as a table edit by the time anybody sees it.
constexpr bool EveryRowIsWellShaped() {
  for (const ProviderAuthVendor &vendor : kVendors) {
    if (IsEmpty(vendor.provider_id) || IsEmpty(vendor.token_url)) {
      return false;
    }
    if (!IsIsoDateOrAbsent(vendor.terms_reviewed_on)) {
      return false;
    }
    if (vendor.flow == ProviderAuthFlowKind::kDeviceCode) {
      // RFC 8628 has no redirect and no authorization page.
      if (!IsEmpty(vendor.authorization_url) ||
          IsEmpty(vendor.device_code_url) ||
          vendor.redirect != ProviderAuthRedirectKind::kManualCode) {
        return false;
      }
    } else if (IsEmpty(vendor.authorization_url)) {
      return false;
    }
    // Exactly one strategy matches an address, and it is the only one that
    // may carry one. The reserved strategy carries none because its address
    // is the compiled constant above.
    if (vendor.redirect == ProviderAuthRedirectKind::kTabInterception) {
      if (IsEmpty(vendor.registered_redirect_uri) ||
          std::string_view(vendor.registered_redirect_uri)
              .starts_with(kProductScheme)) {
        return false;
      }
    } else if (!IsEmpty(vendor.registered_redirect_uri)) {
      return false;
    }
    // The state quirk is a PKCE deviation and cannot describe a device flow.
    if (vendor.state_is_pkce_verifier &&
        vendor.flow != ProviderAuthFlowKind::kPkce) {
      return false;
    }
    // A row that presents no client has nothing to borrow, nothing to
    // register and nothing to authenticate with; a row that presents one
    // names whose it is once it has one.
    //
    // The scope is deliberately not required beside an identity. One vendor's
    // authorization request takes a client and no scope at all, and demanding
    // one here would mean inventing a scope for it — a value the vendor would
    // refuse, written to satisfy a rule rather than to describe a flow.
    if (!vendor.presents_client_identity &&
        (!IsEmpty(vendor.client_id) || !IsEmpty(vendor.borrowed_from) ||
         !IsEmpty(vendor.client_secret))) {
      return false;
    }
    if (!IsEmpty(vendor.client_id) && !vendor.presents_client_identity) {
      return false;
    }
    // A client secret is a field of the exchange, so a row cannot carry one
    // without the identity it belongs to. It is never a substitute for one.
    if (!IsEmpty(vendor.client_secret) && IsEmpty(vendor.client_id)) {
      return false;
    }
    // The redirect parameter names a column, so it cannot exist without an
    // address to put in it, and only the strategy that carries an address may
    // rename the parameter that presents it.
    if (!IsEmpty(vendor.authorization_redirect_param) &&
        (IsEmpty(vendor.registered_redirect_uri) ||
         vendor.redirect != ProviderAuthRedirectKind::kTabInterception)) {
      return false;
    }
    // A minted key is one exchange and no more.
    if (vendor.exchange == ProviderAuthExchangeKind::kMintedApiKey &&
        !IsEmpty(vendor.secondary_token_url)) {
      return false;
    }
    // A licensed domain belongs to the exchange that can name an address.
    // Only the second exchange answers with one, so a suffix on a row without
    // that exchange is a permission nothing reads — and a permission nothing
    // reads is the kind that is still there when somebody later adds the code
    // that would have read it.
    if (!IsDomainSuffixOrAbsent(vendor.credential_host_suffix) ||
        (!IsEmpty(vendor.credential_host_suffix) &&
         IsEmpty(vendor.secondary_token_url))) {
      return false;
    }
  }
  return true;
}

// The reservation itself, checked rather than described: the product's own
// callback belongs to a registration the product has made, and it has made
// none. The day it does, this assertion is what a reviewer is sent to.
constexpr bool NoRowUsesTheProductCallback() {
  for (const ProviderAuthVendor &vendor : kVendors) {
    if (vendor.redirect == ProviderAuthRedirectKind::kCustomScheme) {
      return false;
    }
  }
  return true;
}

static_assert(EveryRowIsWellShaped(),
              "a vendor row breaks one of the table's shape rules");
static_assert(NoRowUsesTheProductCallback(),
              "the product's own callback is reserved until the product has a "
              "registration of its own (decision 0095 section 2)");

const ProviderAuthVendor *g_vendor_for_testing = nullptr;

}  // namespace

const ProviderAuthVendor *ProviderAuthVendorFor(std::string_view provider_id) {
  if (g_vendor_for_testing &&
      provider_id == g_vendor_for_testing->provider_id) {
    return g_vendor_for_testing;
  }
  for (const ProviderAuthVendor &vendor : kVendors) {
    if (provider_id == vendor.provider_id) {
      return &vendor;
    }
  }
  return nullptr;
}

ScopedProviderAuthVendorForTesting::ScopedProviderAuthVendorForTesting(
    const ProviderAuthVendor *vendor) {
  CHECK(!g_vendor_for_testing);
  g_vendor_for_testing = vendor;
}

ScopedProviderAuthVendorForTesting::~ScopedProviderAuthVendorForTesting() {
  g_vendor_for_testing = nullptr;
}

bool ProviderAuthVendorRegistered(std::string_view provider_id) {
  const ProviderAuthVendor *vendor = ProviderAuthVendorFor(provider_id);
  if (!vendor) {
    return false;
  }
  // The date first, because it is the one that is not about the machinery. An
  // identity says the product can talk to the vendor; a dated review says
  // somebody read what the vendor permits before it did.
  if (vendor->terms_reviewed_on[0] == '\0') {
    return false;
  }
  return !vendor->presents_client_identity || vendor->client_id[0] != '\0';
}

std::string_view ProviderAuthRedirectFor(const ProviderAuthVendor &vendor) {
  switch (vendor.redirect) {
    case ProviderAuthRedirectKind::kTabInterception:
      return vendor.registered_redirect_uri;
    case ProviderAuthRedirectKind::kManualCode:
      return {};
    case ProviderAuthRedirectKind::kCustomScheme:
      return kProviderAuthRedirectUri;
  }
  return {};
}

}  // namespace taffy
