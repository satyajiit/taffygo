// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_MODEL_CUSTOM_PROVIDER_ENDPOINT_POLICY_H_
#define TAFFY_BROWSER_MODEL_CUSTOM_PROVIDER_ENDPOINT_POLICY_H_

#include <string>

namespace taffy {

// Why an address a person typed may not be registered.
//
// Named rather than boolean because each of these is a different sentence to
// show someone who is looking at their own server's address and cannot see
// what is wrong with it. `kCleartextNotLocal` in particular is the one that
// needs saying: the address is fine, the scheme is fine, and the pair is not.
enum class CustomEndpointRefusal {
  kNone,
  // Past `kMaxProviderEndpointBytes`.
  kTooLong,
  // Nothing parses out of it, it names no host, or its scheme is neither
  // https nor http.
  kNotAnAddress,
  // Plain http to something that is not a literal local address.
  kCleartextNotLocal,
  // A `user:password@` prefix.
  kCarriesCredentials,
  kCarriesQuery,
  kCarriesFragment,
};

// Whether a person may register this address as their own model endpoint
// (decision 0096 section 3).
//
// This is the *registration* question and only that. It says nothing about
// whether a particular model request may be sent to the address later: that
// is settled by comparing the request's address with the register, which is a
// different question with a different answer, in
// `taffy/browser/core_model_effect_validation.h`.
//
// Ports and paths are allowed — the whole point is that a server at
// `http://192.168.1.9:11434/v1` has both and today loses both.
CustomEndpointRefusal
ClassifyCustomProviderEndpoint(const std::string &value);

// Whether the address is a literal local one, in the sense decision 0096
// section 3 means: loopback, an RFC1918 or link-local range written as digits,
// or a name ending `.local`.
//
// It is the half of the classifier above that decides whether cleartext is
// allowed, exposed so that it can be asserted on its own — a predicate this
// security-relevant should be answerable without also being asked about
// queries, fragments and lengths. Android's cleartext exception is scoped to
// this same set rather than opened for the application, so a second spelling
// of "local" anywhere in the product would be a second answer.
bool IsLiteralLocalEndpoint(const std::string &value);

} // namespace taffy

#endif // TAFFY_BROWSER_MODEL_CUSTOM_PROVIDER_ENDPOINT_POLICY_H_
