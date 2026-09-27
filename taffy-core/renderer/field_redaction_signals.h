// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_RENDERER_FIELD_REDACTION_SIGNALS_H_
#define TAFFY_RENDERER_FIELD_REDACTION_SIGNALS_H_

// The signal vocabularies the field classifier matches against, and the
// matching itself.
//
// Its own translation unit because it is the part a reviewer audits: the lists
// are policy, not code, and the matcher is the only thing that reads them. The
// classifier next door decides what to do with a match; nothing here decides
// anything.
//
// Every list is matched against TOKENS, never against a raw substring, so a
// field named "discounted" cannot match a hint for "count".

#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "taffy/renderer/field_redaction.h"
#include "taffy/renderer/semantic_graph.h"

namespace taffy::field_redaction_signals {

// One hint and the category it implies.
struct CategoryHint {
  std::string_view token;
  ProhibitedCategory category;
};

// Version of the rules in this file and in field_redaction.cc. Any change to a
// token list, a category mapping, or a detector bumps it, so a persisted
// observation can be told apart from one produced by today's rules (protocol
// section 7.5, extraction_rule_version).
//
// 3: decision 0061. The identity vocabulary stopped being Anglophone, and
// "off"/"on" left the ordinary-autocomplete list — an observation recorded at
// version 2 may carry a value from a field this build refuses to read.
inline constexpr uint32_t kRedactionRuleVersion = 3;

// The vocabularies. Defined in the .cc so a change to a list is a change to
// one file.
extern const base::span<const CategoryHint> kProhibitedTokenHints;
extern const base::span<const CategoryHint> kProhibitedPhraseHints;
extern const base::span<const CategoryHint> kProhibitedAutocompleteTokens;
extern const base::span<const CategoryHint> kProhibitedControlTypes;
extern const base::span<const std::string_view> kFinancialHints;
extern const base::span<const std::string_view> kPaymentHints;
extern const base::span<const std::string_view> kIdentityHints;
extern const base::span<const std::string_view> kHealthHints;
extern const base::span<const std::string_view> kLegalHints;
extern const base::span<const std::string_view> kPrivateCommunicationHints;
extern const base::span<const std::string_view> kAdministrationHints;
extern const base::span<const std::string_view> kContactHints;
extern const base::span<const std::string_view> kAccountHints;
extern const base::span<const std::string_view> kOrdinaryAutocompleteTokens;
extern const base::span<const std::string_view> kOrdinaryControlTypes;

// Splits authored text into lowercase alphanumeric tokens.
std::vector<std::string> Tokenize(std::string_view text);

// Tokens plus the joined forms, so a multi-word hint can be matched.
std::vector<std::string> TokensAndJoins(std::string_view text);

// Whether any candidate token appears in the hint list.
bool ContainsAnyHint(const std::vector<std::string>& candidates,
                     base::span<const std::string_view> hints);

// The category the candidates imply, or kNone.
ProhibitedCategory MatchCategory(const std::vector<std::string>& candidates,
                                 base::span<const CategoryHint> hints);

// How restrictive a sensitivity class is. Higher is stricter.
int StrictnessRank(Sensitivity s);

// The stricter of what is already known and what was just observed.
Sensitivity AccumulateEvidence(Sensitivity current, Sensitivity evidence);

}  // namespace taffy::field_redaction_signals

#endif  // TAFFY_RENDERER_FIELD_REDACTION_SIGNALS_H_
