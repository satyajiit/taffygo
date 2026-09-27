// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ACCOUNT_PLANE_CONFIGURATION_H_
#define TAFFY_BROWSER_ACCOUNT_PLANE_CONFIGURATION_H_

#include <cstddef>
#include <string>
#include <string_view>

namespace taffy {

enum class AccountPlaneConfigurationStatus {
  kReady,
  kInvalidOrigin,
  kMissingPublishableKey,
  kInvalidPublishableKey,
};

enum class AccountNetworkResponseDisposition {
  kCompleted,
  kRejected,
  kOutcomeUnknown,
};

// What a refusing account-plane response said, reduced to a closed set. A 4xx
// body from the account plane is a fixed server sentence or a fixed server
// code, never a person's data, and the browser keeps nothing of it but this tag
// and, for a coded refusal, the code itself when it is shaped like one. The
// sentence is matched whole and never retained.
enum class AccountRefusalTag {
  kUnrecognised,
  kProviderDisabled,
  kBadIdToken,
  kNonceMismatch,
  kNoncePresenceMismatch,
  kUnacceptableAudience,
  kValidationFailed,
  kRateLimited,
  kSessionOrTokenNotFound,
  kCoded,
};

struct AccountRefusalDiagnostic {
  AccountRefusalTag tag = AccountRefusalTag::kUnrecognised;
  // The account plane's `error_code`, kept only when it is `[a-z_]{1,64}`.
  std::string code;
};

// A refusal body larger than this is not read at all.
inline constexpr size_t kMaxAccountRefusalBodyBytes = 4096;

enum class GoogleServerClientConfigurationStatus {
  kDisabled,
  kReady,
  kTooLong,
  kMalformed,
};

// Validates the build-owned account route. Runtime/UI input is never accepted.
AccountPlaneConfigurationStatus
ValidateAccountPlaneConfiguration(std::string_view origin,
                                  std::string_view publishable_key);

// Empty is an intentional build-time disable. A non-empty value must be a
// bounded Google OAuth web/server client identity; no runtime or UI value is
// accepted as a substitute.
GoogleServerClientConfigurationStatus
ValidateGoogleServerClientConfiguration(std::string_view server_client_id);

// Consequential requests are ambiguous unless a complete bounded HTTP
// response proves success or a closed deterministic credential rejection.
AccountNetworkResponseDisposition
ClassifyAccountNetworkResponse(int net_error, int http_status,
                               bool has_complete_body);

// Reduces a refusing response body to its closed diagnostic. Anything that
// is not a JSON object within the cap, or says nothing this table knows, is
// kUnrecognised with no code.
AccountRefusalDiagnostic ClassifyAccountRefusalBody(std::string_view body);

// The snake_case name a log line prints for a tag.
std::string_view AccountRefusalTagName(AccountRefusalTag tag);

// One WARNING line: the operation, the HTTP status, the tag and the code.
// Content-free by construction; the body itself is never written anywhere.
void LogAccountRefusal(std::string_view operation, int http_status,
                       std::string_view body);

// Private profiles never link an account, even if a compromised core emits a
// forged account effect.
constexpr bool AccountPlaneAllowsProfile(bool off_the_record) {
  return !off_the_record;
}

} // namespace taffy

#endif // TAFFY_BROWSER_ACCOUNT_PLANE_CONFIGURATION_H_
