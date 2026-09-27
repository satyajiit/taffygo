// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/high_risk_pattern_detector.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "base/strings/string_split.h"
#include "base/strings/string_util.h"

namespace taffy {

namespace {

bool IsHexDigit(char c) {
  return base::IsAsciiDigit(c) || (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

bool LuhnValid(const std::string& digits) {
  if (digits.size() < 13 || digits.size() > 19) {
    return false;
  }
  int sum = 0;
  bool double_it = false;
  for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
    int value = *it - '0';
    if (double_it) {
      value *= 2;
      if (value > 9) {
        value -= 9;
      }
    }
    sum += value;
    double_it = !double_it;
  }
  return sum % 10 == 0;
}

std::string_view TrimTokenPunctuation(std::string_view token) {
  while (!token.empty() && !base::IsAsciiAlphaNumeric(token.front())) {
    token.remove_prefix(1);
  }
  while (!token.empty() && !base::IsAsciiAlphaNumeric(token.back())) {
    token.remove_suffix(1);
  }
  return token;
}

}  // namespace

HighRiskPatternDetector::HighRiskPatternDetector(
    const ObservationLimits& limits)
    : limits_(limits) {}

HighRiskPatternDetector::~HighRiskPatternDetector() = default;

HighRiskPatternKind HighRiskPatternDetector::Detect(
    std::string_view text) const {
  const RedactionLimits& limits = limits_->redaction();

  // Bounded (protocol section 9.2 asks for bounded pattern detectors). The
  // scan looks at a prefix rather than the whole string because this is the
  // second line of defence and not the first: the first is that a prohibited
  // value is never read.
  if (text.size() > limits.max_text_scan_bytes()) {
    text = text.substr(0, limits.max_text_scan_bytes());
  }

  const std::vector<std::string_view> raw_tokens = base::SplitStringPiece(
      text, base::kWhitespaceASCII, base::TRIM_WHITESPACE,
      base::SPLIT_WANT_NONEMPTY);

  // Pass 1: digit groups. A card or account number written with separators -
  // "4111 1111 1111 1111", "4111-1111-1111-1111" - is several tokens, so the
  // run is accumulated across adjacent digit-only tokens rather than being
  // required to be one token.
  std::string digit_run;
  size_t separators = 0;
  bool saw_payment_card = false;
  bool saw_long_digit_run = false;
  auto close_digit_run = [&]() {
    if (digit_run.size() >= 13 && LuhnValid(digit_run)) {
      saw_payment_card = true;
    }
    if (digit_run.size() >= limits.min_digit_run() &&
        separators <= limits.max_digit_separators()) {
      saw_long_digit_run = true;
    }
    digit_run.clear();
    separators = 0;
  };

  for (std::string_view raw : raw_tokens) {
    const std::string_view token = TrimTokenPunctuation(raw);
    if (token.empty()) {
      close_digit_run();
      continue;
    }

    bool digits_only = true;
    size_t token_separators = 0;
    std::string token_digits;
    for (char c : token) {
      if (base::IsAsciiDigit(c)) {
        token_digits.push_back(c);
      } else if (c == '-' || c == '.' || c == '/') {
        ++token_separators;
      } else {
        digits_only = false;
        break;
      }
    }
    if (digits_only && !token_digits.empty()) {
      if (!digit_run.empty()) {
        ++separators;  // the whitespace between two digit groups
      }
      digit_run += token_digits;
      separators += token_separators;
      continue;
    }
    close_digit_run();

    // Pass 2: key material and opaque tokens, per token.
    if (token.size() >= limits.min_hex_run() &&
        std::ranges::all_of(token, &IsHexDigit)) {
      return HighRiskPatternKind::kHexKeyMaterial;
    }
    if (token.size() >= limits.min_token_run()) {
      bool has_digit = false;
      bool has_lower = false;
      bool has_upper = false;
      bool has_token_punctuation = false;
      bool only_token_characters = true;
      for (char c : token) {
        if (base::IsAsciiDigit(c)) {
          has_digit = true;
        } else if (base::IsAsciiLower(c)) {
          has_lower = true;
        } else if (base::IsAsciiUpper(c)) {
          has_upper = true;
        } else if (c == '-' || c == '_' || c == '.' || c == '+' || c == '/' ||
                   c == '=') {
          has_token_punctuation = true;
        } else {
          only_token_characters = false;
          break;
        }
      }
      // A long word is not a token. A long run that mixes digits with letters
      // and either mixes case or carries token punctuation is the shape every
      // API key, session identifier, and signed assertion shares.
      if (only_token_characters && has_digit &&
          (has_lower || has_upper || has_token_punctuation) &&
          ((has_lower && has_upper) || has_token_punctuation)) {
        return HighRiskPatternKind::kOpaqueToken;
      }
    }
  }
  close_digit_run();

  if (saw_payment_card) {
    return HighRiskPatternKind::kPaymentCardNumber;
  }
  if (saw_long_digit_run) {
    return HighRiskPatternKind::kLongDigitRun;
  }

  // Pass 3: seed phrases. Deliberately a WHOLE-STRING test rather than a scan
  // for a run inside prose. A run of short lowercase words is also what
  // ordinary English looks like, so a scanning detector would delete
  // paragraphs; a recovery phrase, by contrast, sits alone in its own field
  // or its own element. Precision beats reach here because this is the second
  // line and the first line already covers the field case.
  if (raw_tokens.size() >= limits.min_seed_phrase_words() &&
      raw_tokens.size() <= 32) {
    const bool every_token_is_a_short_lowercase_word =
        std::ranges::all_of(raw_tokens, [](std::string_view token) {
          return token.size() >= 3 && token.size() <= 8 &&
                 std::ranges::all_of(token, [](char c) {
                   return base::IsAsciiLower(c);
                 });
        });
    if (every_token_is_a_short_lowercase_word) {
      return HighRiskPatternKind::kSeedPhrase;
    }
  }

  return HighRiskPatternKind::kNone;
}

}  // namespace taffy
