// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_model_effect_validation.h"

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/strings/string_util.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "url/gurl.h"
#include "url/origin.h"
#include "url/url_constants.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

bool IsIdentifier(const std::string& value, size_t maximum) {
  return !value.empty() && value.size() <= maximum;
}

// The catalog rule: the endpoint as an https origin, and nothing else.
//
// Serializing the parsed origin and comparing it with what arrived is what
// makes this a check rather than a list of things to watch for. A path, a
// query, a fragment and a `user:password@` prefix all survive parsing and none
// of them survives Serialize(), so each is refused without being named, and so
// is the next one nobody thought of. An explicit `:443` and an upper-case host
// are refused for the same reason and that is deliberate: there is exactly one
// spelling of an origin here, so two effects that would reach the same
// provider cannot be written differently.
//
// It matters because this string is chosen inside the sandbox. The core says
// which host it wants reached; the closed wire family, compiled in on this
// side, decides the path underneath it. A core that could name the path could
// name any route on a host a person had already trusted with a credential.
bool IsHttpsOrigin(const std::string& value) {
  if (value.empty() || value.size() > mojom::kMaxProviderEndpointBytes) {
    return false;
  }
  const GURL parsed(value);
  const url::Origin origin = url::Origin::Create(parsed);
  return parsed.is_valid() && parsed.SchemeIs(url::kHttpsScheme) &&
         !origin.opaque() && origin.Serialize() == value;
}

// Header names a credential travels in.
//
// Two tables rather than one, and they overlap on purpose. The exact names are
// the ones with standing meaning to a transport, written out so a reader can
// see the rule without inferring it. The substrings cover the family nobody
// can enumerate: `x-api-key`, `api-key`, `x-goog-api-key` and `x-auth-token`
// are four spellings of one idea and the next provider will invent a fifth.
//
// The credential for this call reaches the provider one way, by resolving
// `credential_handle` against the browser's secure store. A second way that
// the sandboxed core could fill in itself would be a way with no store behind
// it, and the key would have had to exist in the core to be written there.
constexpr std::string_view kDeniedHeaderNames[] = {
    "authorization", "proxy-authorization", "cookie",
    "set-cookie",    "www-authenticate",    "proxy-authenticate",
};

constexpr std::string_view kDeniedHeaderSubstrings[] = {
    "api-key",    "apikey",   "api_key", "auth",      "bearer",
    "credential", "password", "secret",  "signature", "token",
};

bool NamesACredential(std::string_view lowered) {
  for (std::string_view denied : kDeniedHeaderNames) {
    if (lowered == denied) {
      return true;
    }
  }
  for (std::string_view denied : kDeniedHeaderSubstrings) {
    if (lowered.find(denied) != std::string_view::npos) {
      return true;
    }
  }
  return false;
}

// RFC 9110's `token`. A name outside it is not a header name at all, and the
// deny list above only means anything against names this narrow: a byte the
// transport would treat as a separator could end the name early and leave the
// rest of it reading as something the check never saw.
bool IsHeaderToken(const std::string& value) {
  if (value.empty() || value.size() > mojom::kMaxModelHeaderNameBytes) {
    return false;
  }
  constexpr std::string_view kTokenPunctuation = "!#$%&'*+-.^_`|~";
  for (char character : value) {
    if (!base::IsAsciiAlphaNumeric(character) &&
        kTokenPunctuation.find(character) == std::string_view::npos) {
      return false;
    }
  }
  return true;
}

// Visible ASCII, plus the space and tab a field value may contain.
//
// This is the check that makes the deny list above hold. A value carrying a
// carriage return and a line feed would end its own header and start another
// one of the core's choosing, and `Authorization` written that way is a name
// the deny list never gets to look at.
bool IsHeaderValue(const std::string& value) {
  if (value.size() > mojom::kMaxModelHeaderValueBytes) {
    return false;
  }
  for (char character : value) {
    const auto byte = static_cast<unsigned char>(character);
    if (byte != '\t' && (byte < 0x20u || byte > 0x7Eu)) {
      return false;
    }
  }
  return true;
}

bool AreValidStaticHeaders(
    const std::vector<mojom::ModelStaticHeaderPtr>& headers) {
  if (headers.size() > mojom::kMaxModelStaticHeaders) {
    return false;
  }
  std::set<std::string> seen;
  for (const mojom::ModelStaticHeaderPtr& header : headers) {
    if (!header || !IsHeaderToken(header->name) ||
        !IsHeaderValue(header->value)) {
      return false;
    }
    const std::string lowered = base::ToLowerASCII(header->name);
    // Refused rather than merged. Two values for one name are two requests as
    // far as a provider is concerned, and which one arrives would be decided
    // by whichever transport happened to read the list.
    if (NamesACredential(lowered) || !seen.insert(lowered).second) {
      return false;
    }
  }
  return true;
}

// The register rule: the endpoint as a string somebody typed and this browser
// wrote down.
//
// Byte equality, and deliberately nothing cleverer. Re-validating the address
// here would be asking "is this acceptable?", which is a question with an
// endless supply of wrong answers — a scheme nobody considered, a host that
// canonicalizes into a different one, a range whose meaning changed. The
// register asks "did this person type this?", which has exactly one.
//
// A null lookup is a caller with no register in reach, and it refuses: there
// is no second authority that could accept an address on the register's
// behalf.
bool IsRegisteredEndpoint(const mojom::ModelRequestEffect& effect,
                          const RegisteredEndpointLookup& registered_endpoint) {
  if (!registered_endpoint) {
    return false;
  }
  const std::optional<std::string> registered =
      registered_endpoint.Run(effect.provider_id);
  return registered && *registered == effect.endpoint;
}

// The one thing about a person's own address a caller with no register can
// say: that it is an address-sized string. It accepts nothing — the address is
// settled where the request is sent — and it is not the address policy, which
// governs what may be registered rather than what may be sent.
bool IsBoundedAddress(const std::string& value) {
  return !value.empty() && value.size() <= mojom::kMaxProviderEndpointBytes;
}

// Everything about a model request except where it goes.
bool HasValidFields(const mojom::ModelRequestEffect& effect,
                    size_t max_identifier_bytes,
                    size_t max_effect_bytes) {
  const bool has_media_handle = effect.media_attachment_handle.has_value();
  const bool has_media_mime = effect.media_attachment_mime_type.has_value();
  const bool valid_media =
      has_media_handle == has_media_mime &&
      (!has_media_handle ||
       (!effect.probe && !effect.task_id.empty() &&
        effect.disclosure == mojom::DisclosureClass::kPageContent &&
        IsIdentifier(*effect.media_attachment_handle,
                     mojom::kMaxMediaAttachmentHandleBytes) &&
        *effect.media_attachment_mime_type == "image/png"));
  const bool valid_schedule =
      effect.not_before_monotonic_ms == 0u ||
      (!effect.probe && !effect.task_id.empty() && !has_media_handle);
  return IsIdentifier(effect.route_id, max_identifier_bytes) &&
         IsIdentifier(effect.model_id, max_identifier_bytes) &&
         effect.request_body.size() <= max_effect_bytes &&
         effect.max_output_bytes > 0u &&
         effect.max_output_bytes <= max_effect_bytes &&
         // Empty means no task owns this call, which is a direct request and
         // not a defect. A non-empty one is the identity CancelTask matches.
         effect.task_id.size() <= max_identifier_bytes &&
         // A probe is task-less by construction (decision 0083): a probe that
         // named a task would be a paid call a cancellation could reach and
         // the ledger never journalled as that task's work.
         (!effect.probe || effect.task_id.empty()) &&
         IsIdentifier(effect.provider_id, mojom::kMaxProviderIdBytes) &&
         (!effect.credential_handle ||
          IsIdentifier(*effect.credential_handle, max_identifier_bytes)) &&
         AreValidStaticHeaders(effect.static_headers) && valid_media &&
         valid_schedule;
}

}  // namespace

bool IsCanonicalHttpsProviderOrigin(const std::string& value) {
  return IsHttpsOrigin(value);
}

bool IsValidCoreModelRequest(
    const mojom::ModelRequestEffect& effect,
    size_t max_identifier_bytes,
    size_t max_effect_bytes,
    const RegisteredEndpointLookup& registered_endpoint) {
  if (!HasValidFields(effect, max_identifier_bytes, max_effect_bytes)) {
    return false;
  }
  // Two rules, chosen by the authority the request itself claims (decision
  // 0096 section 2), and written so that neither is expressed in terms of the
  // other. A catalog address is judged; a person's own address is recognized.
  // A request that claims the wrong kind is therefore refused by the rule it
  // claimed rather than quietly answered by the other one, and relaxing either
  // rule later cannot relax the one beside it.
  switch (effect.endpoint_kind) {
    case mojom::ModelEndpointKind::kCatalogOrigin:
      return IsHttpsOrigin(effect.endpoint);
    case mojom::ModelEndpointKind::kUserBaseUrl:
      return IsRegisteredEndpoint(effect, registered_endpoint);
  }
  return false;
}

bool IsValidCoreModelRequest(const mojom::ModelRequestEffect& effect,
                             size_t max_identifier_bytes,
                             size_t max_effect_bytes) {
  if (!HasValidFields(effect, max_identifier_bytes, max_effect_bytes)) {
    return false;
  }
  switch (effect.endpoint_kind) {
    case mojom::ModelEndpointKind::kCatalogOrigin:
      return IsHttpsOrigin(effect.endpoint);
    case mojom::ModelEndpointKind::kUserBaseUrl:
      return IsBoundedAddress(effect.endpoint);
  }
  return false;
}

bool IsValidCustomEndpointProbe(
    const mojom::CustomEndpointProbeEffect& effect) {
  return IsIdentifier(effect.provider_id, mojom::kMaxProviderIdBytes) &&
         IsBoundedAddress(effect.endpoint) &&
         (!effect.credential_handle ||
          IsIdentifier(*effect.credential_handle,
                       mojom::kMaxIdentifierBytes)) &&
         effect.max_response_bytes > 0u &&
         effect.max_response_bytes <= mojom::kMaxProviderListingBytes;
}

}  // namespace taffy
