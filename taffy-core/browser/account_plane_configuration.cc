// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/account_plane_configuration.h"

#include <array>
#include <optional>
#include <string>

#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "net/base/net_errors.h"
#include "url/gurl.h"

namespace taffy {

AccountPlaneConfigurationStatus
ValidateAccountPlaneConfiguration(std::string_view origin,
                                  std::string_view publishable_key) {
  const GURL parsed_origin(origin);
  if (!parsed_origin.is_valid() || !parsed_origin.SchemeIs("https") ||
      parsed_origin.has_username() || parsed_origin.has_password() ||
      parsed_origin.has_query() || parsed_origin.has_ref() ||
      (!parsed_origin.path().empty() && parsed_origin.path() != "/")) {
    return AccountPlaneConfigurationStatus::kInvalidOrigin;
  }
  if (publishable_key.empty()) {
    return AccountPlaneConfigurationStatus::kMissingPublishableKey;
  }
  constexpr std::string_view kModernPublishablePrefix = "sb_publishable_";
  if (publishable_key.size() > 512u ||
      !base::StartsWith(publishable_key, kModernPublishablePrefix) ||
      publishable_key.size() == kModernPublishablePrefix.size()) {
    return AccountPlaneConfigurationStatus::kInvalidPublishableKey;
  }
  for (const char character : publishable_key) {
    if (!base::IsAsciiAlphaNumeric(character) && character != '_' &&
        character != '-') {
      return AccountPlaneConfigurationStatus::kInvalidPublishableKey;
    }
  }
  return AccountPlaneConfigurationStatus::kReady;
}

GoogleServerClientConfigurationStatus
ValidateGoogleServerClientConfiguration(std::string_view server_client_id) {
  if (server_client_id.empty()) {
    return GoogleServerClientConfigurationStatus::kDisabled;
  }
  if (server_client_id.size() > 512u) {
    return GoogleServerClientConfigurationStatus::kTooLong;
  }
  constexpr std::string_view kGoogleClientSuffix =
      ".apps.googleusercontent.com";
  if (!base::EndsWith(server_client_id, kGoogleClientSuffix)) {
    return GoogleServerClientConfigurationStatus::kMalformed;
  }
  const std::string_view prefix = server_client_id.substr(
      0, server_client_id.size() - kGoogleClientSuffix.size());
  if (prefix.empty() || !base::IsAsciiAlphaNumeric(prefix.front()) ||
      !base::IsAsciiAlphaNumeric(prefix.back()) ||
      prefix.find('-') == std::string_view::npos) {
    return GoogleServerClientConfigurationStatus::kMalformed;
  }
  for (const char character : prefix) {
    if (!base::IsAsciiAlphaNumeric(character) && character != '-') {
      return GoogleServerClientConfigurationStatus::kMalformed;
    }
  }
  return GoogleServerClientConfigurationStatus::kReady;
}

AccountNetworkResponseDisposition
ClassifyAccountNetworkResponse(int net_error, int http_status,
                               bool has_complete_body) {
  if (net_error != net::OK || !has_complete_body || http_status < 100 ||
      http_status > 599) {
    return AccountNetworkResponseDisposition::kOutcomeUnknown;
  }
  if (http_status >= 200 && http_status <= 299) {
    return AccountNetworkResponseDisposition::kCompleted;
  }
  if (http_status == 400 || http_status == 401 || http_status == 403 ||
      http_status == 404 || http_status == 409 || http_status == 422) {
    return AccountNetworkResponseDisposition::kRejected;
  }
  return AccountNetworkResponseDisposition::kOutcomeUnknown;
}

namespace {

struct KnownRefusalText {
  std::string_view text;
  AccountRefusalTag tag;
};

// The fixed sentences the account plane's id_token grant answers with, plus
// the wording an older build used for a switched-off provider. Compared whole.
constexpr std::array<KnownRefusalText, 5> kKnownRefusalTexts = {{
    {"Bad ID token", AccountRefusalTag::kBadIdToken},
    {"Nonces mismatch", AccountRefusalTag::kNonceMismatch},
    {"Passed nonce and nonce in id_token should either both exist or not.",
     AccountRefusalTag::kNoncePresenceMismatch},
    {"Unacceptable audience in id_token",
     AccountRefusalTag::kUnacceptableAudience},
    {"Unsupported provider: provider is not enabled",
     AccountRefusalTag::kProviderDisabled},
}};

struct KnownRefusalCode {
  std::string_view code;
  AccountRefusalTag tag;
};

constexpr std::array<KnownRefusalCode, 7> kKnownRefusalCodes = {{
    {"provider_disabled", AccountRefusalTag::kProviderDisabled},
    {"validation_failed", AccountRefusalTag::kValidationFailed},
    {"over_request_rate_limit", AccountRefusalTag::kRateLimited},
    {"over_email_send_rate_limit", AccountRefusalTag::kRateLimited},
    {"session_not_found", AccountRefusalTag::kSessionOrTokenNotFound},
    {"refresh_token_not_found", AccountRefusalTag::kSessionOrTokenNotFound},
    {"refresh_token_already_used", AccountRefusalTag::kSessionOrTokenNotFound},
}};

bool IsRefusalCodeShaped(std::string_view code) {
  if (code.empty() || code.size() > 64) {
    return false;
  }
  for (const char character : code) {
    if (!((character >= 'a' && character <= 'z') || character == '_')) {
      return false;
    }
  }
  return true;
}

} // namespace

AccountRefusalDiagnostic ClassifyAccountRefusalBody(std::string_view body) {
  AccountRefusalDiagnostic diagnostic;
  if (body.empty() || body.size() > kMaxAccountRefusalBodyBytes) {
    return diagnostic;
  }
  const std::optional<base::Value> parsed =
      base::JSONReader::Read(body, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return diagnostic;
  }
  const base::DictValue &dict = parsed->GetDict();
  if (const std::string *code = dict.FindString("error_code");
      code && IsRefusalCodeShaped(*code)) {
    diagnostic.code = *code;
    diagnostic.tag = AccountRefusalTag::kCoded;
    for (const KnownRefusalCode &known : kKnownRefusalCodes) {
      if (known.code == *code) {
        diagnostic.tag = known.tag;
        break;
      }
    }
  }
  for (const std::string_view field : {"error_description", "msg"}) {
    const std::string *text = dict.FindString(field);
    if (!text) {
      continue;
    }
    for (const KnownRefusalText &known : kKnownRefusalTexts) {
      if (known.text == *text) {
        diagnostic.tag = known.tag;
        return diagnostic;
      }
    }
  }
  return diagnostic;
}

std::string_view AccountRefusalTagName(AccountRefusalTag tag) {
  switch (tag) {
  case AccountRefusalTag::kUnrecognised:
    return "unrecognised";
  case AccountRefusalTag::kProviderDisabled:
    return "provider_disabled";
  case AccountRefusalTag::kBadIdToken:
    return "bad_id_token";
  case AccountRefusalTag::kNonceMismatch:
    return "nonce_mismatch";
  case AccountRefusalTag::kNoncePresenceMismatch:
    return "nonce_presence_mismatch";
  case AccountRefusalTag::kUnacceptableAudience:
    return "unacceptable_audience";
  case AccountRefusalTag::kValidationFailed:
    return "validation_failed";
  case AccountRefusalTag::kRateLimited:
    return "rate_limited";
  case AccountRefusalTag::kSessionOrTokenNotFound:
    return "session_or_token_not_found";
  case AccountRefusalTag::kCoded:
    return "coded";
  }
  return "unrecognised";
}

void LogAccountRefusal(std::string_view operation, int http_status,
                       std::string_view body) {
  const AccountRefusalDiagnostic diagnostic = ClassifyAccountRefusalBody(body);
  LOG(WARNING) << "taffy account: " << operation << " refused http="
               << http_status << " tag=" << AccountRefusalTagName(diagnostic.tag)
               << (diagnostic.code.empty() ? "" : " code=") << diagnostic.code;
}

} // namespace taffy
