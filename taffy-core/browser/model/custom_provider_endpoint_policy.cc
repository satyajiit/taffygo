// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The address policy of decision 0096 section 3: may a person register this?
//
// The refusal is made with GURL and net::IPAddress and never with a string
// pattern, for the reason the comment above the `core_api_command_factory`
// target in taffy-core/browser/BUILD.gn writes down: the patterns are the part
// that gets it wrong. `https://0x7f.1/` and `https://2130706433/` are both
// 127.0.0.1 once canonicalized, and `https://[::ffff:169.254.169.254]/` is the
// link-local metadata address wearing IPv6. GURL has already canonicalized the
// host by the time anything here runs, and an IPv4-mapped IPv6 literal is
// converted back to its IPv4 form before any range is consulted — otherwise
// `[::ffff:8.8.8.8]` would sit inside the reserved `::/8` block and read as
// local while naming a public address.

#include "taffy/browser/model/custom_provider_endpoint_policy.h"

#include <stddef.h>

#include <string>
#include <string_view>

#include "base/strings/string_util.h"
#include "net/base/ip_address.h"
#include "net/base/url_util.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

// The suffix that makes a *name* local. It is the one name form decision 0096
// admits, and it is admitted by the shape of the name rather than by what it
// resolves to.
constexpr std::string_view kMulticastDnsSuffix = ".local";

struct LocalRange {
  const char *literal;
  size_t prefix_bits;
};

// The private ranges of both families, written out rather than inferred from
// `IsPubliclyRoutable()`. That predicate is the right one for the opposite
// question — is this address safe to *reach* from a catalog document — and the
// wrong one here, because it is false for carrier-grade NAT, the documentation
// ranges, multicast and the unspecified address as well, none of which is a
// server a person runs on their own network.
//
// `fc00::/7` is here because it is what RFC1918 is in IPv6, and a person whose
// home network is v6-only reaches their own machine at one of these. Leaving it
// out would have admitted a laptop at `192.168.1.9` and refused the same laptop
// at its unique-local address, which is a distinction about the person's router
// rather than about who receives their data.
constexpr LocalRange kPrivateRanges[] = {
    {"10.0.0.0", 8},   {"172.16.0.0", 12},
    {"192.168.0.0", 16}, {"fc00::", 7},
};

// The cloud instance-metadata addresses, which sit inside ranges this file
// otherwise admits: the IPv4 one is link-local and the IPv6 one is unique-local.
// Nobody runs a model server on either, so refusing them costs a person nothing
// and takes the single most-attempted request-forgery destination off the list
// of addresses this product can be talked into holding. This is narrower than
// decision 0096 section 3 requires, and refusing more than a rule demands is
// always allowed; the record notes it.
constexpr std::string_view kMetadataAddresses[] = {
    "169.254.169.254",
    "fd00:ec2::254",
};

// Whether `address` falls in `range`.
//
// Compared only within one family. `net::IPAddressMatchesPrefix` will map a
// v4 address into v6 to compare it against a v6 prefix, which is the right
// behaviour for a caller asking about one prefix and the wrong one for a table
// holding both families: a v4 address would be tested against `fc00::/7` in its
// mapped form, and the answer to that question is not one this table is asking.
bool MatchesRange(const net::IPAddress &address, const LocalRange &range) {
  net::IPAddress prefix;
  if (!prefix.AssignFromIPLiteral(range.literal)) {
    return false;
  }
  if (prefix.size() != address.size()) {
    return false;
  }
  return net::IPAddressMatchesPrefix(address, prefix, range.prefix_bits);
}

bool IsPrivateLiteral(const net::IPAddress &address) {
  for (const LocalRange &range : kPrivateRanges) {
    if (MatchesRange(address, range)) {
      return true;
    }
  }
  return false;
}

bool IsInstanceMetadataAddress(const net::IPAddress &address) {
  for (const std::string_view literal : kMetadataAddresses) {
    net::IPAddress metadata;
    if (metadata.AssignFromIPLiteral(literal) && metadata == address) {
      return true;
    }
  }
  return false;
}

bool NamesALiteralLocalAddress(const GURL &url) {
  net::IPAddress address;
  if (address.AssignFromIPLiteral(url.HostNoBrackets())) {
    if (address.IsIPv4MappedIPv6()) {
      address = net::ConvertIPv4MappedIPv6ToIPv4(address);
    }
    if (IsInstanceMetadataAddress(address)) {
      return false;
    }
    // Loopback is 127.0.0.0/8 and ::1; link-local is 169.254.0.0/16 and
    // fe80::/10. Both predicates are net's own, so the ranges cannot drift
    // from the ones the network stack itself recognizes.
    return address.IsLoopback() || address.IsLinkLocal() ||
           IsPrivateLiteral(address);
  }

  // `localhost` is admitted as the name it is.
  //
  // It looks like the DNS name the paragraph below refuses and it is not one:
  // RFC 6761 reserves it, resolvers are required to answer it from the loopback
  // interface without consulting the network, and Chromium's own stack enforces
  // that. So the reason this file refuses names — that resolution happens after
  // the check and the answer can change between them — is the one reason that
  // cannot apply here, because there is no party who owns this name and could
  // repoint it. `net::IsLocalhost` is the network stack's own answer, which
  // also covers the `*.localhost` subdomains RFC 6761 reserves alongside it.
  //
  // Admitting it matters rather more than it looks: `http://localhost:11434` is
  // what a person running a model server on the machine in front of them will
  // type, and refusing it would have made the ordinary case the broken one.
  if (net::IsLocalhost(url)) {
    return true;
  }

  // Otherwise a name, and the only name form that is admitted is `.local`.
  //
  // A DNS name that happens to resolve to a private address is refused here
  // even though resolving it would say "private", because resolution happens
  // after this check and the answer can change between the two. A name
  // accepted on the strength of one lookup is a name that can be repointed at
  // anything at all by the party that owns it, and nothing would re-ask.
  const std::string_view host = url.host();
  return host.size() > kMulticastDnsSuffix.size() &&
         base::EndsWith(host, kMulticastDnsSuffix,
                        base::CompareCase::SENSITIVE);
}

} // namespace

CustomEndpointRefusal
ClassifyCustomProviderEndpoint(const std::string &value) {
  if (value.size() > mojom::kMaxProviderEndpointBytes) {
    return CustomEndpointRefusal::kTooLong;
  }
  const GURL url(value);
  if (!url.is_valid() || url.host().empty() ||
      !(url.SchemeIs(url::kHttpsScheme) || url.SchemeIs(url::kHttpScheme))) {
    return CustomEndpointRefusal::kNotAnAddress;
  }
  // Asked before the cleartext rule, because a person who typed a query onto
  // their endpoint has a different thing to fix than one who typed http, and
  // being told about the scheme first would send them to fix the wrong one.
  if (url.has_username() || url.has_password()) {
    return CustomEndpointRefusal::kCarriesCredentials;
  }
  if (url.has_query()) {
    return CustomEndpointRefusal::kCarriesQuery;
  }
  if (url.has_ref()) {
    return CustomEndpointRefusal::kCarriesFragment;
  }
  if (url.SchemeIs(url::kHttpScheme) && !NamesALiteralLocalAddress(url)) {
    return CustomEndpointRefusal::kCleartextNotLocal;
  }
  return CustomEndpointRefusal::kNone;
}

bool IsLiteralLocalEndpoint(const std::string &value) {
  const GURL url(value);
  return url.is_valid() && !url.host().empty() &&
         NamesALiteralLocalAddress(url);
}

} // namespace taffy
