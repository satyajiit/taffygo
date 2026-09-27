// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/providerauth/provider_credential_host.h"

#include <string>
#include <string_view>

#include "taffy/browser/providerauth/provider_auth_configuration.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "url/gurl.h"
#include "url/origin.h"
#include "url/url_constants.h"

namespace taffy {
namespace {

// The canonical form of a provider address, as the catalog rule spells it.
//
// Serializing the parsed origin and comparing it with what arrived is what
// makes this a check rather than a list of things to watch for: a path, a
// query, a fragment, a `user:password@` prefix, an explicit `:443` and an
// upper-case host all survive parsing and none of them survives Serialize().
// The same reasoning is written out at length beside `IsHttpsOrigin` in
// `core_model_effect_validation.cc`; it is restated here rather than shared
// because that one answers a question about an effect the isolated core
// composed, and this one answers a question about a document a vendor served.
// Two parties, two answers, and relaxing one must not relax the other.
bool IsCanonicalHttpsOrigin(const std::string &value) {
  if (value.empty() ||
      value.size() > core_service::mojom::kMaxProviderEndpointBytes) {
    return false;
  }
  const GURL parsed(value);
  const url::Origin origin = url::Origin::Create(parsed);
  return parsed.is_valid() && parsed.SchemeIs(url::kHttpsScheme) &&
         !origin.opaque() && origin.Serialize() == value;
}

// Whether `host` is `parent` itself or a name beneath it.
//
// The separating dot is required rather than assumed, which is the whole
// content of this function: `evilgithubcopilot.com` ends with the characters
// of `githubcopilot.com` and is a domain somebody else owns.
bool IsAtOrBeneath(std::string_view host, std::string_view parent) {
  if (host == parent) {
    return true;
  }
  return host.size() > parent.size() + 1u && host.ends_with(parent) &&
         host[host.size() - parent.size() - 1u] == '.';
}

// The vendor row, when it is one whose credentials name their own address.
//
// One spelling of "this row licenses a domain", shared by the two questions
// that ask it: whether an address was expected at all, and whether a particular
// address is inside what the row licenses. Two spellings would eventually
// disagree, and the disagreement would read as a vendor that sometimes issues
// an address and sometimes does not.
const ProviderAuthVendor *LicensedVendorFor(std::string_view provider_id) {
  const ProviderAuthVendor *vendor = ProviderAuthVendorFor(provider_id);
  if (!vendor || vendor->credential_host_suffix[0] == '\0') {
    return nullptr;
  }
  return vendor;
}

}  // namespace

bool ProviderCredentialNamesItsOwnHost(std::string_view provider_id) {
  return LicensedVendorFor(provider_id) != nullptr;
}

bool ProviderCredentialHostLicensed(std::string_view provider_id,
                                    const std::string &origin) {
  const ProviderAuthVendor *vendor = LicensedVendorFor(provider_id);
  if (!vendor) {
    return false;
  }
  if (!IsCanonicalHttpsOrigin(origin)) {
    return false;
  }
  // The suffix carries its own leading dot, so a host that is exactly the
  // licensed domain with no label in front of it does not match — and should
  // not: the row licenses names *under* a domain, and the domain itself is
  // the vendor's site rather than one of the addresses this exchange issues.
  //
  // The URL is a named local because `GURL::host()` returns a view into it at
  // the pinned Chromium (url/gurl.h at 152.0.7977.42), and a view into a
  // temporary would outlive what it points at.
  const GURL parsed(origin);
  const std::string_view host = parsed.host();
  const std::string_view suffix(vendor->credential_host_suffix);
  return host.size() > suffix.size() && host.ends_with(suffix);
}

bool ProviderCredentialHostAddresses(const std::string &catalog_origin,
                                     const std::string &credential_origin) {
  if (!IsCanonicalHttpsOrigin(catalog_origin) ||
      !IsCanonicalHttpsOrigin(credential_origin)) {
    return false;
  }
  const GURL catalog(catalog_origin);
  const GURL credential(credential_origin);
  return IsAtOrBeneath(credential.host(), catalog.host());
}

}  // namespace taffy
