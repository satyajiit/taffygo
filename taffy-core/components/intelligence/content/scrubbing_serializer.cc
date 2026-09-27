// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/scrubbing_serializer.h"

#include <string>
#include <utility>
#include <vector>

#include "taffy/components/intelligence/content/secret_shape_scanner.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace taffy {

namespace {

constexpr char kTruncationMarker[] = "[truncated]";

// One placeholder per credential class. Static strings: a placeholder derived
// from the value — its length, its first character, a hash — would be a leak
// with extra steps.
const char* PlaceholderFor(CredentialClass credential_class) {
  switch (credential_class) {
    case CredentialClass::kUnknown:
      return "[redacted-sensitive]";
    case CredentialClass::kPassword:
      return "[redacted-password]";
    case CredentialClass::kOneTimeCode:
      return "[redacted-one-time-code]";
    case CredentialClass::kCardSecurityCode:
      return "[redacted-card-security-code]";
    case CredentialClass::kPersonalIdentificationNumber:
      return "[redacted-pin]";
    case CredentialClass::kPasskeyAssertion:
      return "[redacted-passkey-assertion]";
    case CredentialClass::kAuthenticationToken:
      return "[redacted-authentication-token]";
    case CredentialClass::kSessionToken:
      return "[redacted-session-token]";
    case CredentialClass::kApiKey:
      return "[redacted-api-key]";
    case CredentialClass::kPrivateKey:
      return "[redacted-private-key]";
    case CredentialClass::kSeedPhrase:
      return "[redacted-seed-phrase]";
    case CredentialClass::kRecoveryCode:
      return "[redacted-recovery-code]";
    case CredentialClass::kCaptchaAnswer:
      return "[redacted-captcha-answer]";
  }
}

// Occurrences of the fixture canary prefix in the input. See the call site for
// why this is counted independently of the accepted matches.
uint32_t CountCanaryTokens(std::string_view text) {
  const std::string_view prefix(kCanaryTokenPrefix);
  uint32_t count = 0;
  size_t from = 0;
  while (true) {
    const size_t at = text.find(prefix, from);
    if (at == std::string_view::npos) {
      return count;
    }
    ++count;
    from = at + prefix.size();
  }
}

}  // namespace

// static
ScrubbedText ScrubbingSerializer::Serialize(std::string_view text) {
  // Truncate before scanning. A diagnostic larger than the bound is a payload,
  // and scanning an unbounded attacker-influenced string in the browser
  // process is a cost an attacker gets to choose.
  const bool truncated = text.size() > kMaxScannedCharacters;
  const std::string_view scanned =
      truncated ? text.substr(0, kMaxScannedCharacters) : text;

  const std::vector<SecretMatch> matches = ScanForSecretShapes(scanned);

  ScrubReport report;
  std::string out;
  out.reserve(scanned.size());

  size_t cursor = 0;
  for (const SecretMatch& match : matches) {
    if (match.begin < cursor) {
      // ScanForSecretShapes returns non-overlapping ascending matches, so this
      // is unreachable. Skipping rather than trusting it keeps a future
      // scanner bug from producing a string that interleaves redacted and
      // unredacted bytes.
      continue;
    }
    out.append(scanned.substr(cursor, match.begin - cursor));
    out.append(match.replacement);
    cursor = match.end;

    report.applied_rule_mask |= AsMask(match.rule);
    ++report.redaction_count;
  }
  out.append(scanned.substr(cursor));

  // The canary count is taken from the input directly rather than from the
  // accepted matches, and that is deliberate. A canary inside a URL, inside a
  // certificate block or inside a bearer token is removed by whichever rule
  // claimed the larger span, and if the count came from the match list those
  // cases would report zero — a leak detector that goes quiet exactly when
  // another rule happened to cover for it. Counting the input means the
  // tripwire cannot be masked.
  report.canary_hit_count = CountCanaryTokens(scanned);
  if (report.canary_hit_count > 0) {
    report.applied_rule_mask |= AsMask(ScrubRule::kCanaryTripwire);
  }

  if (truncated) {
    out.append(kTruncationMarker);
  }
  return ScrubbedText(std::move(out), report);
}

// static
ScrubbedText ScrubbingSerializer::SerializeOriginOf(const GURL& url) {
  ScrubReport report;
  const url::Origin origin = url::Origin::Create(url);

  // Serialize() on an opaque origin returns "null", which is the right answer
  // for a diagnostic: it says "a sandboxed document" without saying which one.
  std::string serialized = origin.Serialize();

  // path(), not path_piece(): at 152.0.7977.42 GURL's component getters were
  // reshuffled so that the bare name is the non-copying one — path() returns
  // std::string_view and GetPath() is the copy — and the _piece() spellings are
  // gone. Same characters either way, leading slash included, which is why the
  // test below is "longer than the slash" rather than "non-empty".
  if (url.has_query() || url.has_ref() || url.path().size() > 1 ||
      url.has_username() || url.has_password()) {
    // Something was dropped. Recording that keeps a reader from assuming the
    // origin was the whole URL.
    report.applied_rule_mask |= AsMask(ScrubRule::kUrlReducedToOrigin);
    ++report.redaction_count;
  }
  if (url.has_username() || url.has_password()) {
    report.applied_rule_mask |= AsMask(ScrubRule::kUrlUserInfo);
  }

  return ScrubbedText(std::move(serialized), report);
}

// static
ScrubbedText ScrubbingSerializer::RedactedPlaceholder(
    CredentialClass credential_class) {
  ScrubReport report;
  report.applied_rule_mask = AsMask(ScrubRule::kSensitiveParameterValue);
  report.redaction_count = 1;
  return ScrubbedText(PlaceholderFor(credential_class), report);
}

// static
ScrubbedText ScrubbingSerializer::Empty() {
  return ScrubbedText(std::string(), ScrubReport());
}

}  // namespace taffy
