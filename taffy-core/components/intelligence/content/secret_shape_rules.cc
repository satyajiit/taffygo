// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/secret_shape_rules.h"

#include <algorithm>
#include <string>

#include "base/strings/string_util.h"
#include "taffy/components/intelligence/content/sensitive_name_list.h"

namespace taffy {

namespace {

constexpr char kRedacted[] = "[redacted]";
constexpr char kRedactedUrl[] = "[redacted-url]";
constexpr char kRedactedCanary[] = "[redacted-canary]";
constexpr char kRedactedCertificate[] = "[redacted-certificate]";

bool IsUrlCharacter(char c) {
  // Everything a URL can contain that is not whitespace or a quoting
  // character. Deliberately generous: over-matching a URL costs a redacted
  // diagnostic, under-matching leaves a query string in a log.
  return !base::IsAsciiWhitespace(c) && c != '"' && c != '\'' && c != '<' &&
         c != '>' && c != '`';
}

bool IsBase64UrlCharacter(char c) {
  return base::IsAsciiAlphaNumeric(c) || c == '-' || c == '_' || c == '=' ||
         c == '+' || c == '/';
}

bool IsNameCharacter(char c) {
  return base::IsAsciiAlphaNumeric(c) || c == '_' || c == '-';
}

// True when a span is already one of this scrubber's own replacements.
// Scrubbing twice must be a no-op: a serializer whose output tripped its own
// rules would make the redaction count meaningless and would chew through a
// legitimate diagnostic one pass at a time.
bool IsRedactionPlaceholder(std::string_view value) {
  constexpr std::string_view kPrefix = "[redacted";
  return value.size() >= kPrefix.size() + 1 &&
         value.substr(0, kPrefix.size()) == kPrefix && value.back() == ']';
}

void Add(std::vector<SecretMatch>* out,
         size_t begin,
         size_t end,
         ScrubRule rule,
         std::string_view replacement) {
  if (end <= begin) {
    return;
  }
  SecretMatch match;
  match.begin = begin;
  match.end = end;
  match.rule = rule;
  match.replacement = replacement;
  out->push_back(match);
}

// --- the scanners ----------------------------------------------------------

// Every occurrence of the fixture canary prefix, plus the token that follows
// it. A canary here is a defect upstream, and the tripwire is how it becomes
// visible.
void ScanCanaries(std::string_view text, std::vector<SecretMatch>* out) {
  const std::string_view prefix(kCanaryTokenPrefix);
  size_t from = 0;
  while (true) {
    const size_t at = text.find(prefix, from);
    if (at == std::string_view::npos) {
      return;
    }
    size_t end = at + prefix.size();
    while (end < text.size() &&
           (base::IsAsciiAlphaNumeric(text[end]) || text[end] == '-')) {
      ++end;
    }
    Add(out, at, end, ScrubRule::kCanaryTripwire, kRedactedCanary);
    from = end;
  }
}

// "-----BEGIN ...-----" through the matching END line, or to the end of the
// text when the block is truncated. A truncated key block is still a key.
void ScanPemBlocks(std::string_view text, std::vector<SecretMatch>* out) {
  constexpr std::string_view kBegin = "-----BEGIN ";
  constexpr std::string_view kEnd = "-----END ";
  size_t from = 0;
  while (true) {
    const size_t at = text.find(kBegin, from);
    if (at == std::string_view::npos) {
      return;
    }
    size_t end = text.find(kEnd, at + kBegin.size());
    if (end == std::string_view::npos) {
      end = text.size();
    } else {
      const size_t terminator = text.find("-----", end + kEnd.size());
      end = terminator == std::string_view::npos ? text.size() : terminator + 5;
    }
    Add(out, at, end, ScrubRule::kPemBlock, kRedactedCertificate);
    from = end;
  }
}

// A URL token, reduced to scheme and authority. The authority is kept because
// an origin is what an observability record is allowed to carry; everything
// after it goes, and credentials embedded before the host go with it.
void ScanUrls(std::string_view text, std::vector<SecretMatch>* out) {
  constexpr std::string_view kSchemeSeparator = "://";
  size_t from = 0;
  while (true) {
    const size_t separator = text.find(kSchemeSeparator, from);
    if (separator == std::string_view::npos) {
      return;
    }

    // Walk back over the scheme.
    size_t begin = separator;
    while (begin > 0 && (base::IsAsciiAlphaNumeric(text[begin - 1]) ||
                         text[begin - 1] == '+' || text[begin - 1] == '-' ||
                         text[begin - 1] == '.')) {
      --begin;
    }

    size_t end = separator + kSchemeSeparator.size();
    while (end < text.size() && IsUrlCharacter(text[end])) {
      ++end;
    }
    // Trailing punctuation belongs to the sentence, not to the URL.
    while (end > separator + kSchemeSeparator.size() &&
           (text[end - 1] == '.' || text[end - 1] == ',' ||
            text[end - 1] == ';' || text[end - 1] == ')')) {
      --end;
    }

    const size_t authority_begin = separator + kSchemeSeparator.size();
    size_t authority_end = authority_begin;
    while (authority_end < end && text[authority_end] != '/' &&
           text[authority_end] != '?' && text[authority_end] != '#') {
      ++authority_end;
    }

    const std::string_view authority =
        text.substr(authority_begin, authority_end - authority_begin);
    const size_t at_sign = authority.find('@');

    if (at_sign != std::string_view::npos) {
      // Credentials in the authority. The whole URL goes: the host after the
      // credentials is not something a log needs badly enough to risk keeping
      // the shape of a phishing target.
      Add(out, begin, end, ScrubRule::kUrlUserInfo, kRedactedUrl);
    } else if (authority_end < end) {
      // There is a path, a query or a fragment. Keep the origin, drop the
      // rest.
      Add(out, authority_end, end, ScrubRule::kUrlReducedToOrigin, kRedacted);
    }
    from = end > from ? end : from + 1;
  }
}

// "Bearer <token>" and "Authorization: <scheme> <token>".
void ScanBearerTokens(std::string_view text, std::vector<SecretMatch>* out) {
  constexpr std::string_view kBearer = "bearer";
  const std::string lowered_storage = base::ToLowerASCII(text);
  const std::string_view lowered(lowered_storage);
  size_t from = 0;
  while (true) {
    const size_t at = lowered.find(kBearer, from);
    if (at == std::string_view::npos) {
      return;
    }
    size_t cursor = at + kBearer.size();
    size_t spaces = 0;
    while (cursor < text.size() && (text[cursor] == ' ' || text[cursor] == ':' ||
                                    text[cursor] == '\t')) {
      ++cursor;
      ++spaces;
    }
    const size_t token_begin = cursor;
    while (cursor < text.size() && IsBase64UrlCharacter(text[cursor])) {
      ++cursor;
    }
    if (spaces > 0 && cursor - token_begin >= 8) {
      Add(out, token_begin, cursor, ScrubRule::kBearerToken, kRedacted);
    }
    from = cursor > from ? cursor : from + 1;
  }
}

// Three base64url segments separated by dots, the first beginning "eyJ" — the
// encoding of a JSON object's opening brace and quote. Precise enough that a
// match is a token and not a coincidence.
void ScanJsonWebTokens(std::string_view text, std::vector<SecretMatch>* out) {
  constexpr std::string_view kHeaderPrefix = "eyJ";
  size_t from = 0;
  while (true) {
    const size_t at = text.find(kHeaderPrefix, from);
    if (at == std::string_view::npos) {
      return;
    }
    size_t cursor = at;
    int dots = 0;
    while (cursor < text.size() &&
           (IsBase64UrlCharacter(text[cursor]) || text[cursor] == '.')) {
      if (text[cursor] == '.') {
        ++dots;
      }
      ++cursor;
    }
    if (dots >= 2 && cursor - at >= 20) {
      Add(out, at, cursor, ScrubRule::kJsonWebToken, kRedacted);
    }
    from = cursor > from ? cursor : from + 1;
  }
}

// name=value and "name": "value", where the name says the value is sensitive.
void ScanSensitiveParameters(std::string_view text,
                             std::vector<SecretMatch>* out) {
  size_t index = 0;
  while (index < text.size()) {
    if (!IsNameCharacter(text[index])) {
      ++index;
      continue;
    }
    const size_t name_begin = index;
    while (index < text.size() && IsNameCharacter(text[index])) {
      ++index;
    }
    const std::string_view name =
        text.substr(name_begin, index - name_begin);

    // Skip an optional closing quote, then the separator, then whitespace and
    // an optional opening quote.
    size_t cursor = index;
    if (cursor < text.size() && (text[cursor] == '"' || text[cursor] == '\'')) {
      ++cursor;
    }
    while (cursor < text.size() &&
           (text[cursor] == ' ' || text[cursor] == '\t')) {
      ++cursor;
    }
    if (cursor >= text.size() || (text[cursor] != '=' && text[cursor] != ':')) {
      continue;
    }
    ++cursor;
    while (cursor < text.size() &&
           (text[cursor] == ' ' || text[cursor] == '\t')) {
      ++cursor;
    }
    char quote = 0;
    if (cursor < text.size() && (text[cursor] == '"' || text[cursor] == '\'')) {
      quote = text[cursor];
      ++cursor;
    }

    const size_t value_begin = cursor;
    while (cursor < text.size()) {
      const char c = text[cursor];
      if (quote != 0) {
        if (c == quote) {
          break;
        }
      } else if (base::IsAsciiWhitespace(c) || c == '&' || c == ';' ||
                 c == ',' || c == '}' || c == ')') {
        break;
      }
      ++cursor;
    }

    if (IsSensitiveValueName(name) && cursor > value_begin &&
        !IsRedactionPlaceholder(
            text.substr(value_begin, cursor - value_begin))) {
      Add(out, value_begin, cursor, ScrubRule::kSensitiveParameterValue,
          kRedacted);
    }
    index = std::max(index, cursor);
  }
}

// A long run from the base64 or hexadecimal alphabet carrying upper case,
// lower case and digits. The catch-all, and narrow on purpose: a lowercase
// hexadecimal digest has no upper case and does not match, which is what keeps
// this component's own content digests out of the redaction.
void ScanHighEntropyRuns(std::string_view text, std::vector<SecretMatch>* out) {
  size_t index = 0;
  while (index < text.size()) {
    if (!IsBase64UrlCharacter(text[index])) {
      ++index;
      continue;
    }
    const size_t begin = index;
    bool has_upper = false;
    bool has_lower = false;
    bool has_digit = false;
    while (index < text.size() && IsBase64UrlCharacter(text[index])) {
      const char c = text[index];
      has_upper = has_upper || base::IsAsciiUpper(c);
      has_lower = has_lower || base::IsAsciiLower(c);
      has_digit = has_digit || base::IsAsciiDigit(c);
      ++index;
    }
    if (index - begin >= kHighEntropyMinimumLength && has_upper && has_lower &&
        has_digit) {
      Add(out, begin, index, ScrubRule::kHighEntropyRun, kRedacted);
    }
  }
}

// A digit run of card length that passes the Luhn check, separators allowed.
void ScanPaymentCardNumbers(std::string_view text,
                            std::vector<SecretMatch>* out) {
  size_t index = 0;
  while (index < text.size()) {
    if (!base::IsAsciiDigit(text[index])) {
      ++index;
      continue;
    }
    const size_t begin = index;
    size_t digits = 0;
    while (index < text.size() &&
           (base::IsAsciiDigit(text[index]) || text[index] == ' ' ||
            text[index] == '-')) {
      if (base::IsAsciiDigit(text[index])) {
        ++digits;
      }
      ++index;
    }
    size_t end = index;
    while (end > begin && !base::IsAsciiDigit(text[end - 1])) {
      --end;
    }
    if (digits >= 13 && digits <= 19 &&
        PassesLuhnCheck(text.substr(begin, end - begin))) {
      Add(out, begin, end, ScrubRule::kPaymentCardNumber, kRedacted);
    }
  }
}

}  // namespace

void RunAllSecretShapeRules(std::string_view text,
                            std::vector<SecretMatch>* out) {
  // Structure first, entropy last. The order does not decide overlaps — the
  // precedence table does that — but running the precise rules first keeps the
  // candidate list small in the common case.
  ScanCanaries(text, out);
  ScanPemBlocks(text, out);
  ScanUrls(text, out);
  ScanBearerTokens(text, out);
  ScanJsonWebTokens(text, out);
  ScanSensitiveParameters(text, out);
  ScanPaymentCardNumbers(text, out);
  ScanHighEntropyRuns(text, out);
}

int SecretShapeRulePrecedence(ScrubRule rule) {
  switch (rule) {
    case ScrubRule::kCanaryTripwire:
      return 100;
    case ScrubRule::kPemBlock:
      return 90;
    case ScrubRule::kUrlUserInfo:
    case ScrubRule::kUrlReducedToOrigin:
      return 80;
    case ScrubRule::kBearerToken:
      return 70;
    case ScrubRule::kJsonWebToken:
      return 60;
    case ScrubRule::kSensitiveParameterValue:
      return 50;
    case ScrubRule::kPaymentCardNumber:
      return 40;
    case ScrubRule::kHighEntropyRun:
      return 10;
    case ScrubRule::kNone:
      return 0;
  }
}

}  // namespace taffy
