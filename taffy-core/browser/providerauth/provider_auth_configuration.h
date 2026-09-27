// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PROVIDERAUTH_PROVIDER_AUTH_CONFIGURATION_H_
#define TAFFY_BROWSER_PROVIDERAUTH_PROVIDER_AUTH_CONFIGURATION_H_

#include <string_view>

#include "base/containers/span.h"
#include "base/memory/raw_ptr_exclusion.h"

namespace taffy {

// The shape of one vendor's sign-in, fixed by this binary.
enum class ProviderAuthFlowKind {
  // RFC 8628 device authorization: no redirect ever returns to the product.
  kDeviceCode,
  // Authorization-code with PKCE through a Custom Tab.
  kPkce,
};

// How an authorization code gets back into the browser (decision 0095
// section 2). The product is the browser, on a phone, so none of these
// opens a socket or binds a port.
enum class ProviderAuthRedirectKind {
  // The vendor redirects to an address it has on file for the client whose
  // identity this row carries. The navigation is matched and cancelled
  // before it connects, the code is taken from the address, and no page ever
  // sees it.
  kTabInterception,
  // Nothing is intercepted: the vendor shows the person a code, and the
  // person enters it on the product's own chrome. This is the shape of a
  // vendor that displays rather than redirects, and it is also the standing
  // fallback for every interception row, because interception depends on the
  // vendor keeping an address the product recognises.
  kManualCode,
  // The product's own custom-scheme callback. Reserved, and carried by no
  // row today: it belongs to a registration TaffyGo has made itself, and a
  // borrowed client's registered redirect is not ours to change.
  kCustomScheme,
};

// What the token exchange hands back.
enum class ProviderAuthExchangeKind {
  // The ordinary answer: an access token, usually a refresh token, and an
  // expiry. The product seals the triple and refreshes it when it goes stale.
  kOauthTokenPair,
  // The exchange mints a permanent API key instead. There is no token pair
  // and no refresh leg — not a refresh leg that fails, one that does not
  // exist, because the key does not expire and nothing rotates it.
  kMintedApiKey,
};

// One extra parameter a vendor's authorization request carries beyond the
// OAuth ones every flow composes.
//
// It is a typed pair rather than a pre-joined query string, and that is the
// whole reason the type exists. A row that carried `"a=1&redirect_uri=..."`
// as one string would append a second `redirect_uri` the composer never
// wrote, the vendor would honour the last one, the consent screen would look
// exactly right, and the authorization code would be delivered to somebody
// else's address with no leg of the flow failing. Split into a name and a
// value, the value is escaped where every other value is and cannot introduce
// a parameter at all.
struct ProviderAuthAuthorizationParam {
  const char *name;
  const char *value;
};

// One vendor's compiled OAuth facts. Every origin here is a binary constant
// validated at use, never a value a served catalog can supply or move: a
// remote document that could repoint a token endpoint would be exfiltrating
// refresh tokens (decision 0081). The same vendors appear in the Rust core's
// `SIGN_IN_VENDORS` (which decides `configurable`) and the Android flow map
// (`ProviderSignInFlows`, which carries each flow's shape and acknowledgement
// obligation); each layer owns its own facet, `tools/lib/vendor_agreement.py`
// refuses a disagreement, and a sign-in cannot work unless all three agree.
//
// Most of these rows carry a **borrowed** client identity: the public client
// a vendor ships with a command-line tool of its own (decision 0095). A
// borrowed identity is revocable by somebody who is not us, so each row names
// whose tool it belongs to and the date that vendor's terms were read, and a
// row with no dated review is not offered whatever identity is present. Each
// row switches on when its terms review is done, dated, and written into the
// row beside the identity it licenses. A date typed here is a claim that
// somebody read the terms; nothing else in the product can make that claim,
// and nothing should be able to make it by accident, which is what decision
// 0112 says the column asserts and what it does not: a date is not a claim
// that the flow works, and the flow working is not grounds for a date.
struct ProviderAuthVendor {
  // The catalog identity the flow signs in to.
  const char *provider_id;
  ProviderAuthFlowKind flow;
  // Inert for a device flow, which has no redirect at all: RFC 8628 ends at
  // the token poll and nothing ever navigates back. Such a row is pinned to
  // kManualCode with no registered address, because the only thing a person
  // does by hand in that flow is enter the user code the vendor already
  // showed them.
  ProviderAuthRedirectKind redirect;
  ProviderAuthExchangeKind exchange;
  // The authorization page a Custom Tab opens. Empty for a device flow.
  const char *authorization_url;
  // The device-authorization endpoint. A device flow requires one; a PKCE
  // flow may also name one, and then it is the fallback used when the
  // authorization surface cannot be opened at all (decision 0095 section 3
  // prefers the device shape wherever a vendor offers it).
  const char *device_code_url;
  // The token endpoint every grant and refresh is exchanged at.
  const char *token_url;
  // The second exchange some vendors require after the first: the token the
  // first leg returns is not the API credential, and this endpoint turns it
  // into one. Empty for every vendor that needs only one exchange.
  const char *secondary_token_url;
  // The DNS suffix an address this vendor's second exchange names must fall
  // under, leading dot included. Empty for every vendor that issues no
  // address, which is all but one of them.
  //
  // One vendor answers its second exchange with the host that credential's
  // requests belong to, and which host that is depends on the account: an
  // individual plan, a business one and an enterprise one are three different
  // names. The address is therefore not a compiled constant and cannot be, so
  // this is the bound instead of one — the browser accepts what the vendor
  // says only inside the domain the row licenses, and refuses anything else
  // outright. A token response is a remote party's document, and a document
  // that could name any host would be a document that could point a person's
  // credential at a server of the author's choosing.
  const char *credential_host_suffix;
  // The scheme the second exchange presents the first exchange's token
  // under. Empty means `Bearer`, which is what RFC 6750 names.
  //
  // The one vendor with a second exchange does not use it. Its endpoint is an
  // internal one of its own rather than a published OAuth resource server,
  // and it takes the legacy `token` scheme its own editor integration sends;
  // presented as a bearer the same token is refused, and the refusal is a 401
  // that reads exactly like an expired sign-in.
  const char *secondary_authorization_scheme;
  // The revocation endpoint sign-out posts to, best-effort. Empty when the
  // vendor publishes none — revocation is then the person's account page.
  const char *revocation_url;
  // The address this row's client is registered to redirect to, which is the
  // one address the interceptor matches. Empty for every row that is not
  // kTabInterception. Never the product's own callback: see
  // `ProviderAuthRedirectFor`, which is the only way a flow obtains one.
  const char *registered_redirect_uri;
  // The authorization-request parameter the address above is presented in.
  // Empty means `redirect_uri`, which is what RFC 6749 names and what every
  // row but one uses.
  //
  // The exception is a vendor whose callback is the caller's to choose rather
  // than a registration to match: it takes the address under its own name and
  // ignores `redirect_uri` entirely, so a row that could not rename the
  // parameter would open an authorization page the vendor has nowhere to
  // answer. The column names the parameter and nothing else — the address is
  // still written once, in `registered_redirect_uri`, so the value the
  // interceptor matches and the value the vendor is told cannot drift apart.
  // It is read only when composing the authorization request; a token
  // exchange names its redirect the way RFC 6749 does or not at all.
  const char *authorization_redirect_param;
  // Whose tool this row's client identity is, in plain words, for the person
  // who is about to be shown a consent screen naming a client they did not
  // install (decision 0095 sections 1 and 4). Empty only where the identity
  // is the product's own.
  const char *borrowed_from;
  // Extra authorization parameters this vendor's own tool sends and its
  // authorization server expects, in the order they are appended. Empty for
  // every row that needs only the OAuth ones.
  //
  // The table a row points at is a named constant defined beside `kVendors`,
  // never an initialiser list written inline: `tools/.../check_vendor_agreement.py`
  // reads a row positionally by counting its string literals, and a row that
  // carried its parameters as literals would move the column that check reads
  // the terms date out of. Written as a named table there are no literals in
  // the row at all, and the check's counts are unchanged.
  // RAW_PTR_EXCLUSION: the table a row points at is a static-storage-duration
  // constant in the same translation unit as `kVendors`, so nothing here can
  // dangle and a checked span would buy protection these pointers cannot
  // need. It is the same reason `FeaturePostureEntry` gives.
  RAW_PTR_EXCLUSION base::span<const ProviderAuthAuthorizationParam>
      extra_authorization_params;
  // Extra headers the second exchange carries, beside the authorization one.
  // Empty for every row without a second exchange, and for a second exchange
  // that asks for nothing but the token.
  //
  // The one vendor that asks names the editor it thinks it is talking to, in
  // three headers, and refuses a request that names none. What this product
  // puts there is its own name: the endpoint wants to know which client is
  // asking, and answering with somebody else's would be a claim about who is
  // calling that is not true — the same reasoning the `originator` parameter
  // above is decided by.
  // RAW_PTR_EXCLUSION: for the reason the span above gives.
  RAW_PTR_EXCLUSION base::span<const ProviderAuthAuthorizationParam>
      secondary_request_headers;
  // The date that vendor's terms were read, as YYYY-MM-DD. Empty means no
  // review has happened, and the row is not offered.
  const char *terms_reviewed_on;
  // Two vendor quirks, carried because parity with the tool whose identity
  // this row borrows is the mitigation decision 0095 names. Both are read at
  // exactly one place each, so neither can reach any other flow.
  //
  // Whether the authorization request asks the vendor to display the code
  // rather than redirect (`code=true`).
  bool authorization_displays_code;
  // Whether this vendor expects the OAuth `state` parameter to be the PKCE
  // verifier rather than an independent nonce. See `StartPkceFlow` for what
  // the browser does about it and why it costs the other rows nothing.
  bool state_is_pkce_verifier;
  // Whether the vendor's authorization request names a client at all. One
  // row's does not: its exchange authenticates by the PKCE verifier alone,
  // so there is nothing to borrow and nothing to register, and an empty
  // identity there is the finished state rather than an absent one. Every
  // other row's absence is a registration or a borrowing still to be made.
  bool presents_client_identity;
  // The OAuth client identity this product presents. A client id is a public
  // identifier, not a secret. An empty value means neither a registration of
  // the product's own nor a borrowed identity has been written here yet: the
  // flow machinery is complete and refuses to start, reporting
  // FAILED_UNAVAILABLE — the same posture as the undeployed Worker.
  const char *client_id;
  // The value one vendor's token endpoint requires beside the identity above,
  // and the reason it is written here rather than kept out of the tree.
  //
  // Google's installed-application flow makes `client_secret` a required
  // field of the exchange even for a native client that cannot keep one. RFC
  // 8252 section 8.5 says so in as many words — an installed app's secret is
  // not a secret, because the app is distributed to everyone and the value
  // goes with it — and Google publishes the pair for exactly that reason.
  // The value on the one row that carries it is a community-recovered
  // installed-app pair, published, and no more confidential than the client
  // id beside it: it authenticates nothing and grants nothing on its own,
  // because the flow still turns on the person's own consent and on a PKCE
  // verifier this browser mints per attempt.
  //
  // It is emitted only into a token exchange and a refresh, never into an
  // authorization URL and never into a log, and a row without one sends no
  // empty field. If a vendor ever ships a client whose secret is genuinely
  // confidential, that vendor does not belong in this table at all — this
  // column is for the OAuth field, not for a way to carry a credential.
  const char *client_secret;
  // The scopes the identity requests, space-joined. For a borrowed identity
  // this is the borrowed tool's own scope, unchanged: asking for more than
  // the tool asks for is the fastest way to have the identity revoked.
  const char *scope;
};

// The compiled vendor facts, or null for a vendor with no flow.
const ProviderAuthVendor *ProviderAuthVendorFor(std::string_view provider_id);

// Installs one extra vendor row for this object's lifetime, consulted before
// the compiled table. Test-only: it exists so a flow shape whose terms review
// has not happened yet — every shipping row today — can still be driven end
// to end against a dictated endpoint. Nothing shipping constructs one, and
// only one may exist at a time.
class ScopedProviderAuthVendorForTesting {
 public:
  explicit ScopedProviderAuthVendorForTesting(const ProviderAuthVendor *vendor);
  ScopedProviderAuthVendorForTesting(
      const ScopedProviderAuthVendorForTesting &) = delete;
  ScopedProviderAuthVendorForTesting &operator=(
      const ScopedProviderAuthVendorForTesting &) = delete;
  ~ScopedProviderAuthVendorForTesting();
};

// Whether this vendor's flow may be offered at all: a flow exists, an
// identity is in place, and that vendor's terms have been read on a named
// date. The date is a gate rather than a note because a borrowed identity is
// somebody else's to revoke, and the thing that makes borrowing defensible is
// that a person read what the vendor permits before the product acted on it
// (decision 0095 section 1). An identity with no dated review is a row that
// would work and must not run.
bool ProviderAuthVendorRegistered(std::string_view provider_id);

// The redirect address this vendor's authorization request presents, and the
// only address the interceptor matches for it. Empty when the row has no
// redirect at all, which is every device flow and every manual-code row.
//
// This is the only way a flow obtains a redirect address, and it is why the
// product's own custom-scheme callback is not a constant anything outside
// this file can reach. A borrowed client's registered redirect is not ours to
// change (decision 0095 section 2), so handing our callback to a borrowed row
// would ask the vendor to redirect somewhere it has never heard of — a
// sign-in that fails at the vendor, for a reason no log would explain. Here
// that cannot be written: the product's callback is returned for a
// kCustomScheme row and for nothing else, and no row is kCustomScheme.
std::string_view ProviderAuthRedirectFor(const ProviderAuthVendor &vendor);

// How long a person has to finish authorizing before the flow times out.
inline constexpr int kProviderAuthFlowDeadlineMinutes = 10;

}  // namespace taffy

#endif  // TAFFY_BROWSER_PROVIDERAUTH_PROVIDER_AUTH_CONFIGURATION_H_
