// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PROVIDERAUTH_PROVIDER_CREDENTIAL_HOST_H_
#define TAFFY_BROWSER_PROVIDERAUTH_PROVIDER_CREDENTIAL_HOST_H_

#include <string>
#include <string_view>

namespace taffy {

// One vendor answers its second token exchange with the address that
// particular credential's requests belong to, because which address that is
// depends on the plan behind the account. The address is therefore a remote
// party's word rather than a compiled constant, and these questions are what
// the browser asks before it acts on it. The two bounds are asked twice each —
// once where the vendor says the address, and again where the request is sent
// — because those are two different parties saying it: the vendor's token
// response, and then the platform store the record was sealed in.
//
// They are separate functions rather than one because they answer different
// things, and none implies the next. The first is about the row alone and is
// asked when there is no address to inspect at all: was one expected? The
// second is about the vendor: a row licenses a domain, and an address outside
// it is refused whatever else is true of it. The third is about what a person
// is told: something in this product states where a model request went, and it
// states the address the isolated core named, so an address the browser
// substitutes must be one that statement remains true of. Writing any two of
// them together would make relaxing one quietly relax the other.

// Whether this vendor's credentials name their own address.
//
// Asked before either bound below, because it is what the *absence* of an
// address means. A credential that names none is the ordinary answer for every
// vendor whose row licenses no domain, and the catalog's own address is where
// its requests belong. For a row that licenses one it is a failure, and a
// silent one: such a row's catalog origin is the licensing *parent* of the
// domain rather than an API, because `ProviderCredentialHostAddresses` below
// requires the issued host to sit at or beneath the catalog host and no single
// issued host exists for the row to name instead. `github-copilot` is the row
// this is written for — it names `https://githubcopilot.com`, the parent of
// the `.githubcopilot.com` its per-account hosts are issued under, and that
// parent is a licensing bound rather than somewhere a request can go. Sending
// there is sending nowhere, and what comes back is a plausible HTTP error that
// reads as a broken vendor rather than as a browser that lost the address.
bool ProviderCredentialNamesItsOwnHost(std::string_view provider_id);

// Whether `origin` is an address this vendor's row licenses.
//
// True only for a vendor whose row carries a licensed domain — every other
// vendor's address is the one the catalog names, so there is nothing here for
// them to say and an answer would be a permission they never asked for. The
// address must be a canonical https origin and nothing else: a path, a query,
// a port, an embedded credential and an upper-case host are all refused,
// because there is exactly one spelling of an address here and a second one
// would be a second thing to compare against.
bool ProviderCredentialHostLicensed(std::string_view provider_id,
                                    const std::string &origin);

// Whether `credential_origin` may stand in for `catalog_origin`.
//
// The substituted address must be the catalog's own host or a name beneath
// it. That is what keeps the product's statement of where a request went
// true: the isolated core names the catalog origin, never the per-credential
// one, and a substitution that left that domain would send a person's page
// somewhere they were told nothing about.
//
// Both arguments must be canonical https origins. The catalog side is one
// already — it passed the catalog rule in
// `taffy/browser/core_model_effect_validation.h` before reaching here — and
// asking again costs nothing and keeps this function safe for any caller.
bool ProviderCredentialHostAddresses(const std::string &catalog_origin,
                                     const std::string &credential_origin);

}  // namespace taffy

#endif  // TAFFY_BROWSER_PROVIDERAUTH_PROVIDER_CREDENTIAL_HOST_H_
