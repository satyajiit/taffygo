// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/providerauth/auth_callback_parser.h"

#include <algorithm>
#include <array>
#include <ranges>
#include <span>
#include <string>
#include <utility>

#include "base/containers/flat_map.h"
#include "net/base/url_util.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "url/gurl.h"

namespace taffy {
namespace {

namespace service = core_service::mojom;

constexpr size_t kMaxCallbackBytes = 8u * 1024u;
constexpr size_t kMaxAuthorizationCodeBytes = 4u * 1024u;
constexpr size_t kMaxErrorFieldBytes = 1024u;
constexpr std::string_view kCallbackPrefix = "com.taffygo.browser://auth";
constexpr std::string_view kProviderCallbackPrefix =
    "com.taffygo.browser://provider-auth";

using CallbackFields = base::flat_map<std::string, std::string>;

bool IsAllowedKey(std::string_view key,
                  const std::span<const std::string_view> allowed) {
  return std::ranges::find(allowed, key) != allowed.end();
}

bool HasControlByte(std::string_view value) {
  return std::ranges::any_of(value, [](const unsigned char character) {
    return character < 0x20u || character == 0x7fu;
  });
}

bool IsBase64UrlIdentifier(std::string_view value) {
  if (value.empty() || value.size() > service::kMaxIdentifierBytes) {
    return false;
  }
  return std::ranges::all_of(value, [](const char character) {
    return (character >= 'A' && character <= 'Z') ||
           (character >= 'a' && character <= 'z') ||
           (character >= '0' && character <= '9') || character == '-' ||
           character == '_';
  });
}

std::optional<CallbackFields> ReadFields(
    const GURL& url,
    const std::span<const std::string_view> allowed) {
  CallbackFields values;
  for (net::QueryIterator it(url); !it.IsAtEnd(); it.Advance()) {
    const std::string key(it.GetKey());
    const std::string value = it.GetUnescapedValue();
    if (!IsAllowedKey(key, allowed) || HasControlByte(value) ||
        !values.emplace(key, value).second) {
      return std::nullopt;
    }
  }
  return values;
}

const std::string* FindEither(const CallbackFields& query,
                              const CallbackFields& fragment,
                              std::string_view key) {
  const auto query_value = query.find(key);
  const auto fragment_value = fragment.find(key);
  if (query_value != query.end() && fragment_value != fragment.end() &&
      query_value->second != fragment_value->second) {
    return nullptr;
  }
  if (query_value != query.end()) {
    return &query_value->second;
  }
  return fragment_value == fragment.end() ? nullptr : &fragment_value->second;
}

bool DuplicatesAgree(const CallbackFields& query,
                     const CallbackFields& fragment) {
  for (const auto& [key, value] : query) {
    const auto other = fragment.find(key);
    if (other != fragment.end() && other->second != value) {
      return false;
    }
  }
  return true;
}

}  // namespace

namespace {

std::optional<ParsedAccountAuthCallback> ParseCallbackWithPrefix(
    std::string_view raw_uri, std::string_view prefix) {
  if (raw_uri.empty() || raw_uri.size() > kMaxCallbackBytes) {
    return std::nullopt;
  }
  if (!raw_uri.starts_with(prefix)) {
    return std::nullopt;
  }
  std::string surrogate = "https://callback.invalid";
  surrogate.append(raw_uri.substr(prefix.size()));
  const GURL url(surrogate);
  if (!url.is_valid() || !url.SchemeIs("https") ||
      url.host() != "callback.invalid" || url.has_username() ||
      url.has_password() ||
      url.has_port() || (!url.path().empty() && url.path() != "/")) {
    return std::nullopt;
  }

  constexpr std::array<std::string_view, 6> kQueryKeys = {
      "state", "code", "error", "error_code", "error_description", "sb"};
  std::optional<CallbackFields> query = ReadFields(url, kQueryKeys);
  if (!query || query->contains("sb")) {
    return std::nullopt;
  }
  const auto state = query->find("state");
  if (state == query->end() || !IsBase64UrlIdentifier(state->second)) {
    return std::nullopt;
  }

  const auto code = query->find("code");
  if (code != query->end()) {
    if (url.has_ref() || query->size() != 2u || code->second.empty() ||
        code->second.size() > kMaxAuthorizationCodeBytes) {
      return std::nullopt;
    }
    return ParsedAccountAuthCallback{
        state->second, code->second,
        AccountAuthCallbackOutcome::kAuthorizationCode};
  }

  constexpr std::array<std::string_view, 4> kFragmentKeys = {
      "error", "error_code", "error_description", "sb"};
  CallbackFields fragment;
  if (url.has_ref()) {
    const GURL fragment_url("https://callback.invalid/?" + url.GetRef());
    if (!fragment_url.is_valid() || fragment_url.has_ref()) {
      return std::nullopt;
    }
    std::optional<CallbackFields> parsed_fragment =
        ReadFields(fragment_url, kFragmentKeys);
    if (!parsed_fragment) {
      return std::nullopt;
    }
    fragment = std::move(*parsed_fragment);
  }
  if (!DuplicatesAgree(*query, fragment)) {
    return std::nullopt;
  }
  const std::string* error = FindEither(*query, fragment, "error");
  const std::string* error_code = FindEither(*query, fragment, "error_code");
  const std::string* description =
      FindEither(*query, fragment, "error_description");
  const std::string* sb = FindEither(*query, fragment, "sb");
  if ((!error && !error_code) ||
      (error && (error->empty() || error->size() > kMaxErrorFieldBytes)) ||
      (error_code &&
       (error_code->empty() || error_code->size() > kMaxErrorFieldBytes)) ||
      (description && description->size() > kMaxErrorFieldBytes) ||
      (sb && (sb->empty() || sb->size() > kMaxErrorFieldBytes))) {
    return std::nullopt;
  }
  const bool denied = (error && *error == "access_denied") ||
                      (error_code && *error_code == "access_denied");
  return ParsedAccountAuthCallback{
      state->second, std::nullopt,
      denied ? AccountAuthCallbackOutcome::kDenied
             : AccountAuthCallbackOutcome::kProviderError};
}

}  // namespace

std::optional<ParsedAccountAuthCallback> ParseAccountAuthCallback(
    std::string_view raw_uri) {
  return ParseCallbackWithPrefix(raw_uri, kCallbackPrefix);
}

std::optional<ParsedAccountAuthCallback> ParseProviderAuthCallback(
    std::string_view raw_uri) {
  return ParseCallbackWithPrefix(raw_uri, kProviderCallbackPrefix);
}

}  // namespace taffy
