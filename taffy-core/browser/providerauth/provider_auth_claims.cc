// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/providerauth/provider_auth_claims.h"

#include <string>
#include <string_view>

#include "base/base64url.h"
#include "base/json/json_reader.h"
#include "base/values.h"

namespace taffy::provider_auth {
namespace {

// The payload segment of a compact JWS, or empty when the token is not one.
//
// Exactly three segments, because two would be an unsecured token and four
// would be an encrypted one, and this function is willing to read neither.
std::string_view PayloadSegment(std::string_view token) {
  const size_t first = token.find('.');
  if (first == std::string_view::npos) {
    return {};
  }
  const size_t second = token.find('.', first + 1);
  if (second == std::string_view::npos ||
      token.find('.', second + 1) != std::string_view::npos ||
      second == first + 1) {
    return {};
  }
  return token.substr(first + 1, second - first - 1);
}

// A value that may be written into a request header as it stands.
//
// Printable ASCII only, so a separator, a control byte or a line ending
// cannot arrive inside a value and become part of the request. The bound is
// checked here rather than trusted from the document.
bool IsHeaderSafe(std::string_view value) {
  if (value.empty() || value.size() > kMaxClaimValueBytes) {
    return false;
  }
  for (const char byte : value) {
    if (byte < 0x20 || byte > 0x7e) {
      return false;
    }
  }
  return true;
}

}  // namespace

std::optional<std::string> ReadStringClaim(std::string_view token,
                                           std::string_view claim_namespace,
                                           std::string_view claim_key) {
  const std::string_view payload = PayloadSegment(token);
  if (payload.empty()) {
    return std::nullopt;
  }
  std::string decoded;
  // Unpadded, which is what a JWS carries. `IGNORE_PADDING` would also admit
  // a padded segment, and admitting a shape the specification forbids is how
  // a reader ends up disagreeing with the party that wrote the token.
  if (!base::Base64UrlDecode(
          payload, base::Base64UrlDecodePolicy::DISALLOW_PADDING, &decoded)) {
    return std::nullopt;
  }
  // Strict RFC parsing. A token's payload is a JSON document by
  // specification, and admitting a relaxed dialect would be admitting a shape
  // the party that wrote it never meant to send.
  const std::optional<base::DictValue> parsed =
      base::JSONReader::ReadDict(decoded, base::JSON_PARSE_RFC);
  if (!parsed) {
    return std::nullopt;
  }
  // `FindDict` by the whole namespace as one key, not a path: the namespace is
  // a URL and contains dots, and a path lookup would split it into segments
  // that name nothing.
  const base::DictValue *nested = parsed->FindDict(claim_namespace);
  if (!nested) {
    return std::nullopt;
  }
  const std::string *value = nested->FindString(claim_key);
  if (!value || !IsHeaderSafe(*value)) {
    return std::nullopt;
  }
  return *value;
}

}  // namespace taffy::provider_auth
