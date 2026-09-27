// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/secret_shape_scanner.h"

#include <algorithm>

#include "base/strings/string_util.h"
#include "taffy/components/intelligence/content/secret_shape_rules.h"

namespace taffy {

const char kCanaryTokenPrefix[] = "TAFFYGO-CANARY-";


bool PassesLuhnCheck(std::string_view digits) {
  int sum = 0;
  int count = 0;
  for (size_t i = digits.size(); i > 0; --i) {
    const char c = digits[i - 1];
    if (!base::IsAsciiDigit(c)) {
      continue;
    }
    int value = c - '0';
    if (count % 2 == 1) {
      value *= 2;
      if (value > 9) {
        value -= 9;
      }
    }
    sum += value;
    ++count;
  }
  return count > 0 && sum % 10 == 0;
}

std::vector<SecretMatch> ScanForSecretShapes(std::string_view text) {
  std::vector<SecretMatch> candidates;
  RunAllSecretShapeRules(text, &candidates);

  // Resolve overlaps: the more structural rule wins, and between equals the
  // longer span wins, because a partial redaction of a secret is not a
  // redaction.
  std::sort(candidates.begin(), candidates.end(),
            [](const SecretMatch& left, const SecretMatch& right) {
              if (left.begin != right.begin) {
                return left.begin < right.begin;
              }
              const int left_rank = SecretShapeRulePrecedence(left.rule);
              const int right_rank = SecretShapeRulePrecedence(right.rule);
              if (left_rank != right_rank) {
                return left_rank > right_rank;
              }
              return left.length() > right.length();
            });

  std::vector<SecretMatch> accepted;
  for (const SecretMatch& candidate : candidates) {
    if (!accepted.empty() && candidate.begin < accepted.back().end) {
      // Overlaps something already accepted. The accepted one was earlier or
      // more structural, so its replacement text stays — but if the newcomer
      // reaches further, the accepted span grows to cover it. Keeping the
      // shorter span would leave a tail of the secret in the output, and a
      // partial redaction of a secret is not a redaction. Over-redacting costs
      // a less useful diagnostic; under-redacting costs a credential.
      SecretMatch& previous = accepted.back();
      previous.end = std::max(previous.end, candidate.end);
      continue;
    }
    accepted.push_back(candidate);
  }
  return accepted;
}

}  // namespace taffy
