// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_MODEL_PROFILE_MODEL_BROKER_ROUTE_H_
#define TAFFY_BROWSER_MODEL_PROFILE_MODEL_BROKER_ROUTE_H_

#include <string>
#include <string_view>

#include "base/containers/span.h"
#include "base/memory/raw_ptr_exclusion.h"

// Everything a wire family fixes about one model request, as plain data.
//
// These four records are what `ProfileModelBroker::RouteFor` composes and what
// its transport reads; they hold no pointer to the broker and know nothing of
// a call. They are in a namespace of their own rather than nested in the class
// so that the header the class lives in stays about the class, and so that a
// future `taffy::ModelRoute` — the task-consent code already has an
// `IsModelRoute` — cannot collide with them at namespace level, which is the
// shape `tools/lib/cpp_type_collisions.py` refuses.
namespace taffy::model_broker {

// The header a family's credential travels in, and what precedes it in the
// value. Every name here is one `core_model_effect_validation.cc` refuses
// the core, which is what makes "the credential arrives one way" a property
// of the pair rather than of this file alone.
struct CredentialPlacement {
  std::string_view header_name;
  std::string_view value_prefix;
};

// One fixed, non-secret header a wire family always sends.
//
// A family fact rather than a provider one, which is the distinction that
// decides where it lives. The same vendor's row serves two families — a
// metered key on one and a subscription on the other — and a value put on
// the provider would travel on both, announcing a client identity to an
// endpoint that was never issued one. Both halves point at static storage,
// so the span a route carries outlives every call written from it.
struct FamilyHeader {
  std::string_view name;
  std::string_view value;
};

// A header this family reads out of the credential's own claims, and where
// in that credential's payload the value sits.
//
// All three empty on every family that reads none, which is four of the
// five. It is catalog-shaped data on the route rather than a branch in the
// transport for the reason every other family fact is: a second vendor that
// wants its account named is a row here, and the transport goes on knowing
// nothing about whose account it is.
//
// The claim is read, never verified. What it is for is telling the issuer
// which of its own accounts this credential belongs to, and an issuer that
// disagrees with its own token refuses the request — which is the outcome a
// wrong value should have. See `provider_auth::ReadStringClaim`.
struct CredentialClaimHeader {
  std::string_view header_name;
  std::string_view claim_namespace;
  std::string_view claim_key;
};

// Everything the wire family fixes about the request, all of it compiled in.
//
// Two spellings of one path, because the two kinds of endpoint put the same
// request in different places. A catalog endpoint is an origin and `path`
// carries everything under it — the vendor's prefix, the version segment and
// the operation. A person's own endpoint is a base URL that already carries
// whichever of those its server uses, so what this process adds is the
// operation alone, and `base_relative_path` is that operation with no
// leading separator, ready to be joined beneath the base.
//
// They are composed together from the same three pieces in `RouteFor` and
// never written twice, because a family whose two spellings disagreed would
// send a person's own server to a route the catalog invented for somebody
// else's.
struct ModelRoute {
  std::string path;
  std::string base_relative_path;
  std::string content_type;
  CredentialPlacement credential;
  // Empty for every family that fixes none, which is three of the five.
  //
  // RAW_PTR_EXCLUSION: every table a route points at is a
  // static-storage-duration constant compiled into `RouteFor`'s own
  // translation unit, so nothing here can dangle and a checked span would
  // buy protection these pointers cannot need.
  RAW_PTR_EXCLUSION base::span<const FamilyHeader> family_headers;
  CredentialClaimHeader credential_claim;
};

}  // namespace taffy::model_broker

#endif  // TAFFY_BROWSER_MODEL_PROFILE_MODEL_BROKER_ROUTE_H_
