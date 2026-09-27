// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PROVIDERAUTH_PROVIDER_AUTH_WIRE_H_
#define TAFFY_BROWSER_PROVIDERAUTH_PROVIDER_AUTH_WIRE_H_

#include <stddef.h>

#include <string>

namespace taffy::provider_auth {

// The four encodings every leg of a vendor sign-in shares. They live here
// rather than in one leg's file because the legs are split across several
// translation units and a helper copied into each of them is a helper that
// drifts: the constant-time comparison in particular is only worth anything
// if there is exactly one of it.

// `byte_count` bytes of fresh entropy, base64url with no padding. The buffer
// is zeroed before it is dropped.
std::string RandomBase64Url(size_t byte_count);

// The S256 PKCE challenge for a verifier.
std::string PkceChallenge(const std::string &verifier);

// Whether two values are equal, in time that does not depend on where they
// first differ. Empty is never equal to anything, so an uninitialised flow
// value cannot be matched by an empty redirect parameter.
bool ConstantTimeEquals(const std::string &left, const std::string &right);

// One `key=value` pair of an application/x-www-form-urlencoded body.
std::string FormField(const std::string &key, const std::string &value);

}  // namespace taffy::provider_auth

#endif  // TAFFY_BROWSER_PROVIDERAUTH_PROVIDER_AUTH_WIRE_H_
