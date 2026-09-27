// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PROVIDERAUTH_PROVIDER_AUTH_CLAIMS_H_
#define TAFFY_BROWSER_PROVIDERAUTH_PROVIDER_AUTH_CLAIMS_H_

#include <optional>
#include <string>
#include <string_view>

namespace taffy::provider_auth {

// The largest a claim value may be before it is refused rather than sent.
//
// A header value bound rather than a JSON one: what this produces is written
// into a request header, and the only size that matters is the size a header
// may be. It is deliberately far below any bound a transport enforces, because
// nothing this reads is meant to be long — an account identifier is tens of
// bytes, and a claim of five hundred is a token that is not what it claims.
inline constexpr size_t kMaxClaimValueBytes = 256;

// Reads one string claim out of a bearer token's own payload.
//
// The token is a remote party's document, and this reads it as one: nothing
// is verified here and nothing is trusted. The signature is not checked, the
// issuer is not checked, and the expiry is not checked — because none of those
// would make the value safer for the one use it has, which is to say back to
// the issuer which of its own accounts this credential belongs to. A vendor
// that disagrees with its own token refuses the request, which is the outcome
// a wrong value should have.
//
// What *is* enforced is the shape: three dot-separated segments, a payload
// that base64url-decodes, a JSON object, a nested object at `claim_namespace`
// carrying a string at `claim_key`, and a value that is non-empty, within
// `kMaxClaimValueBytes`, and made only of printable ASCII. A value carrying a
// carriage return would otherwise be a header the caller did not write.
//
// `std::nullopt` for every failure, and for a credential that is not a token
// at all: a pasted API key reaches this function and must leave it empty
// rather than mistaken for something structured.
//
// The token is never logged, never copied beyond the claim, and never
// returned.
std::optional<std::string> ReadStringClaim(std::string_view token,
                                           std::string_view claim_namespace,
                                           std::string_view claim_key);

}  // namespace taffy::provider_auth

#endif  // TAFFY_BROWSER_PROVIDERAUTH_PROVIDER_AUTH_CLAIMS_H_
