// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The field classifier: what a form field is, how sensitive it is, and what a
// prohibited field is replaced with.
//
// The vocabularies it matches against, and the matching itself, are in
// field_redaction_signals.h. That split is deliberate: the lists are policy a
// reviewer audits, and this file is the decision made from them.

#include "taffy/renderer/field_redaction.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "base/strings/string_util.h"
#include "taffy/renderer/field_redaction_signals.h"
#include "taffy/renderer/high_risk_pattern_detector.h"

namespace taffy {

using field_redaction_signals::AccumulateEvidence;
using field_redaction_signals::CategoryHint;
using field_redaction_signals::ContainsAnyHint;
using field_redaction_signals::MatchCategory;
using field_redaction_signals::StrictnessRank;
using field_redaction_signals::TokensAndJoins;
using field_redaction_signals::kAccountHints;
using field_redaction_signals::kAdministrationHints;
using field_redaction_signals::kContactHints;
using field_redaction_signals::kFinancialHints;
using field_redaction_signals::kHealthHints;
using field_redaction_signals::kIdentityHints;
using field_redaction_signals::kLegalHints;
using field_redaction_signals::kOrdinaryAutocompleteTokens;
using field_redaction_signals::kOrdinaryControlTypes;
using field_redaction_signals::kPaymentHints;
using field_redaction_signals::kPrivateCommunicationHints;
using field_redaction_signals::kProhibitedAutocompleteTokens;
using field_redaction_signals::kProhibitedControlTypes;
using field_redaction_signals::kProhibitedPhraseHints;
using field_redaction_signals::kProhibitedTokenHints;
using field_redaction_signals::kRedactionRuleVersion;


FieldDescriptor::FieldDescriptor() = default;
FieldDescriptor::FieldDescriptor(const FieldDescriptor&) = default;
FieldDescriptor::FieldDescriptor(FieldDescriptor&&) = default;
FieldDescriptor& FieldDescriptor::operator=(const FieldDescriptor&) = default;
FieldDescriptor& FieldDescriptor::operator=(FieldDescriptor&&) = default;
FieldDescriptor::~FieldDescriptor() = default;

ProhibitedValueFilter::ProhibitedValueFilter(const ObservationLimits& limits)
    : limits_(limits) {}

ProhibitedValueFilter::~ProhibitedValueFilter() = default;

// static
uint32_t ProhibitedValueFilter::rule_version() {
  return kRedactionRuleVersion;
}

ProhibitedCategory ProhibitedValueFilter::CategoryOf(
    const FieldDescriptor& field) const {
  // Signals Blink or the browser produced come first. They are not
  // page-authored, and a page that disguises a password field as `type=text`
  // is exactly what they exist to catch.
  if (field.blink_reports_password_field) {
    return ProhibitedCategory::kPassword;
  }
  if (field.has_password_manager_suggestion) {
    return ProhibitedCategory::kPasswordManagerSuggestion;
  }
  if (field.has_credential_manager_affordance) {
    return ProhibitedCategory::kPasskeyOrPrivateKey;
  }

  const std::vector<std::string> control_type_tokens =
      TokensAndJoins(field.control_type);
  ProhibitedCategory category =
      MatchCategory(control_type_tokens, kProhibitedControlTypes);
  if (category != ProhibitedCategory::kNone) {
    return category;
  }

  // The authored autocomplete token is compared whole, because these are
  // platform-defined values rather than free text.
  const std::string autocomplete =
      base::ToLowerASCII(base::TrimWhitespaceASCII(field.autocomplete_token,
                                                   base::TRIM_ALL));
  for (const CategoryHint& hint : kProhibitedAutocompleteTokens) {
    // A section-scoped or billing-scoped token still ends in the field name,
    // e.g. "section-blue billing cc-csc".
    if (autocomplete == hint.token ||
        autocomplete.ends_with(std::string(" ") + std::string(hint.token))) {
      return hint.category;
    }
  }

  // Page-authored text can only ADD prohibitions. Nothing below can clear
  // one, and nothing anywhere can clear one.
  for (const std::string& text :
       {field.authored_name, field.label_text, field.aria_role,
        field.form_semantics}) {
    const std::vector<std::string> candidates = TokensAndJoins(text);
    category = MatchCategory(candidates, kProhibitedPhraseHints);
    if (category != ProhibitedCategory::kNone) {
      return category;
    }
    category = MatchCategory(candidates, kProhibitedTokenHints);
    if (category != ProhibitedCategory::kNone) {
      return category;
    }
  }

  // A hidden control whose name mentions a token or a state parameter is the
  // shape of a cross-site request forgery token and of an OAuth state value.
  // Neither is something a model needs and both are authentication material.
  if (field.hidden) {
    const std::vector<std::string> candidates =
        TokensAndJoins(field.authored_name);
    static constexpr auto kHiddenTokenNames =
        std::to_array<std::string_view>({"token", "state", "auth", "verify",
                                         "signature", "sig", "hmac", "key"});
    if (ContainsAnyHint(candidates, kHiddenTokenNames)) {
      return ProhibitedCategory::kAuthenticationToken;
    }
  }

  return ProhibitedCategory::kNone;
}

SemanticNode ProhibitedValueFilter::BuildStructuralPlaceholder(
    SemanticNodeId id,
    FrameId frame_id,
    const FieldDescriptor& field) const {
  SemanticNode node;
  node.node_id = id;
  node.frame_id = frame_id;
  node.role = SemanticRole::kTextField;
  const ProhibitedCategory category = CategoryOf(field);
  node.sensitivity = category == ProhibitedCategory::kOneTimeCode
                         ? Sensitivity::kOneTimeCode
                         : Sensitivity::kCredential;
  node.challenge_kind = category == ProhibitedCategory::kOneTimeCode
                            ? ChallengeKind::kOneTimeCode
                            : ChallengeKind::kNone;

  // No name, no description, no text runs. The node says "a control of this
  // class is here" and nothing else.
  ValueDescriptor value;
  value.kind = ValueKind::kSecretWithheld;
  // False, not "whatever the control has". Reporting presence would still be
  // reporting something about the secret, and this code never looked.
  value.present = false;
  value.redacted = true;
  node.value_descriptor = std::move(value);

  // The class of control is safe to state: it is what the browser's own UI
  // already shows the user, and protocol section 9.1 permits exactly this
  // structural statement. The authored name and label are not carried.
  node.attributes.emplace_back(AttributeKey::kInputType, field.control_type);

  // No actions. Focusing a credential control on an assistant's behalf is a
  // separate question that nothing before the M5 exit review may answer.
  node.confidence = 1.0;
  node.sources.push_back(SourceKind::kAdapter);
  node.evidence.emplace_back(SemanticField::kValueDescriptor,
                             SourceKind::kAdapter, "field-redaction",
                             kRedactionRuleVersion, Transformation::kRedacted);
  node.evidence.emplace_back(SemanticField::kSensitivity,
                             SourceKind::kAdapter, "field-redaction",
                             kRedactionRuleVersion, Transformation::kRedacted);
  return node;
}

HighRiskPatternKind ProhibitedValueFilter::DetectHighRiskPattern(
    std::string_view text) const {
  // Delegated so that the "what is a secret-shaped field" rules and the
  // "what is a secret-shaped string" rules do not share a file. They answer
  // different questions from different evidence, and the day one of them
  // grows a special case is the day sharing a file makes the other one
  // harder to review.
  return HighRiskPatternDetector(*limits_).Detect(text);
}

SensitivityClassifier::SensitivityClassifier() = default;
SensitivityClassifier::~SensitivityClassifier() = default;

Sensitivity SensitivityClassifier::Classify(
    const FieldDescriptor& field,
    ProhibitedCategory category) const {
  // A prohibited field is not a sensitivity level of its own: it is
  // kCredential whose value this code never read. The caller passes in what
  // the filter already decided, so there is no way to reach this function
  // with a field nobody asked the filter about.
  if (category != ProhibitedCategory::kNone) {
    return category == ProhibitedCategory::kOneTimeCode
               ? Sensitivity::kOneTimeCode
               : Sensitivity::kCredential;
  }

  Sensitivity result = Sensitivity::kUnknownSensitive;

  // Every page-authored signal protocol section 9.2 lists, in one pass. All
  // of them can raise; none of them can lower.
  const std::vector<std::string> signals = [&field] {
    std::vector<std::string> all;
    for (std::string_view text :
         {std::string_view(field.autocomplete_token),
          std::string_view(field.authored_name),
          std::string_view(field.label_text),
          std::string_view(field.aria_role),
          std::string_view(field.form_semantics)}) {
      for (std::string& token : TokensAndJoins(text)) {
        all.push_back(std::move(token));
      }
    }
    return all;
  }();

  if (ContainsAnyHint(signals, kPaymentHints)) {
    result = AccumulateEvidence(result, Sensitivity::kPayment);
  }
  if (ContainsAnyHint(signals, kFinancialHints)) {
    result = AccumulateEvidence(result, Sensitivity::kFinancial);
  }
  if (ContainsAnyHint(signals, kIdentityHints)) {
    result = AccumulateEvidence(result, Sensitivity::kIdentity);
  }
  if (ContainsAnyHint(signals, kHealthHints)) {
    result = AccumulateEvidence(result, Sensitivity::kHealth);
  }
  if (ContainsAnyHint(signals, kLegalHints)) {
    result = AccumulateEvidence(result, Sensitivity::kLegal);
  }
  if (ContainsAnyHint(signals, kPrivateCommunicationHints)) {
    result = AccumulateEvidence(result, Sensitivity::kPrivateCommunication);
  }
  if (ContainsAnyHint(signals, kAdministrationHints)) {
    result = AccumulateEvidence(result, Sensitivity::kAdministration);
  }
  if (ContainsAnyHint(signals, kContactHints)) {
    result = AccumulateEvidence(result, Sensitivity::kPersonal);
  }
  if (ContainsAnyHint(signals, kAccountHints)) {
    result = AccumulateEvidence(result, Sensitivity::kAccount);
  }

  // "Ordinary" is a conclusion, and it needs a positive statement rather than
  // an absence. Only an explicit ordinary autocomplete token or a control
  // type with no personal semantics can produce it, only when no hint above
  // fired, and only when no context signal is present.
  if (result == Sensitivity::kUnknownSensitive) {
    const std::string autocomplete = base::ToLowerASCII(
        base::TrimWhitespaceASCII(field.autocomplete_token, base::TRIM_ALL));
    const std::string control_type = base::ToLowerASCII(field.control_type);
    const bool ordinary_autocomplete =
        std::ranges::find(kOrdinaryAutocompleteTokens, autocomplete) !=
        std::ranges::end(kOrdinaryAutocompleteTokens);
    const bool ordinary_control_type =
        std::ranges::find(kOrdinaryControlTypes, control_type) !=
        std::ranges::end(kOrdinaryControlTypes);
    if (ordinary_autocomplete || ordinary_control_type) {
      result = Sensitivity::kNotSensitive;
    }
  }

  // Context raises, never lowers. A field in a cross-origin frame, an
  // obscured field, a hidden field, or any field on a page that is not a
  // secure context may not be concluded ordinary - but a field that a signal
  // already classified keeps its specific class, because losing it would hand
  // every later layer a graph in which nothing is classified.
  if (field.in_cross_origin_frame || field.obscured || field.hidden ||
      field.insecure_context) {
    if (result == Sensitivity::kNotSensitive) {
      result = Sensitivity::kUnknownSensitive;
    }
  }

  // The browser's floor is applied last and is the only signal that did not
  // come from the page. It can only raise.
  return Stricter(result, field.policy_floor);
}

Sensitivity SensitivityClassifier::ClassifyControlName(
    bool in_cross_origin_frame,
    Sensitivity policy_floor) const {
  // Classifying a control by the words of its own name withheld every label
  // on an identity portal: on 2026-09-18 the myAadhaar home page crossed 62
  // withheld labels, among them the one link the errand needed, and the model
  // invented a number for it ten turns running. The words name a service. A
  // value in the name is still caught by shape, below this, in the adapter
  // and again in the browser.
  const Sensitivity result = in_cross_origin_frame
                                 ? Sensitivity::kUnknownSensitive
                                 : Sensitivity::kNotSensitive;
  return Stricter(result, policy_floor);
}

Sensitivity SensitivityClassifier::ClassifyContentRegion(
    std::string_view region_label,
    bool in_cross_origin_frame,
    Sensitivity policy_floor) const {
  const std::vector<std::string> signals = TokensAndJoins(region_label);
  Sensitivity result = Sensitivity::kNotSensitive;
  if (ContainsAnyHint(signals, kPaymentHints)) {
    result = AccumulateEvidence(result, Sensitivity::kPayment);
  }
  if (ContainsAnyHint(signals, kFinancialHints)) {
    result = AccumulateEvidence(result, Sensitivity::kFinancial);
  }
  if (ContainsAnyHint(signals, kIdentityHints)) {
    result = AccumulateEvidence(result, Sensitivity::kIdentity);
  }
  if (ContainsAnyHint(signals, kHealthHints)) {
    result = AccumulateEvidence(result, Sensitivity::kHealth);
  }
  if (ContainsAnyHint(signals, kLegalHints)) {
    result = AccumulateEvidence(result, Sensitivity::kLegal);
  }
  if (ContainsAnyHint(signals, kPrivateCommunicationHints)) {
    result = AccumulateEvidence(result, Sensitivity::kPrivateCommunication);
  }
  if (ContainsAnyHint(signals, kAdministrationHints)) {
    result = AccumulateEvidence(result, Sensitivity::kAdministration);
  }
  if (in_cross_origin_frame && result == Sensitivity::kNotSensitive) {
    result = Sensitivity::kUnknownSensitive;
  }
  return Stricter(result, policy_floor);
}

// static
Sensitivity SensitivityClassifier::Stricter(Sensitivity a, Sensitivity b) {
  return StrictnessRank(a) >= StrictnessRank(b) ? a : b;
}

bool MayObserveValue(Sensitivity sensitivity) {
  switch (sensitivity) {
    case Sensitivity::kNotSensitive:
      return true;
    case Sensitivity::kPersonal:
    case Sensitivity::kAccount:
    case Sensitivity::kPayment:
    case Sensitivity::kIdentity:
    case Sensitivity::kHealth:
    case Sensitivity::kFinancial:
    case Sensitivity::kLegal:
    case Sensitivity::kPrivateCommunication:
    case Sensitivity::kAdministration:
    case Sensitivity::kCredential:
    case Sensitivity::kOneTimeCode:
    case Sensitivity::kChallengeResponse:
    case Sensitivity::kUnknownSensitive:
      // Everything that is not demonstrably ordinary is refused here. The
      // browser broker may still narrow further; it can never widen this.
      return false;
  }
}

bool MayEmitText(Sensitivity sensitivity, NodeTextClass text_class) {
  switch (text_class) {
    case NodeTextClass::kControlValue:
      return MayObserveValue(sensitivity);
    case NodeTextClass::kControlLabel:
      // What the user reads off the screen next to the control. A payment
      // form's "Card number" label is not the card number, and a schema with
      // no labels is not a schema. Credential controls are the exception, and
      // they carry no label anyway: their placeholder is built without one.
      return sensitivity != Sensitivity::kCredential &&
             sensitivity != Sensitivity::kOneTimeCode &&
             sensitivity != Sensitivity::kChallengeResponse;
    case NodeTextClass::kContent:
      // Content travels with its marking. Protocol section 9.3 puts
      // minimisation in the layers that know the destination, and says the
      // local core projection is normally rich; stripping
      // here would make the marking pointless and the observation useless.
      // Content this layer classified as credential material still does not
      // travel, and the bounded detectors have already dropped anything that
      // looked like an identifier.
      return sensitivity != Sensitivity::kCredential &&
             sensitivity != Sensitivity::kOneTimeCode &&
             sensitivity != Sensitivity::kChallengeResponse;
  }
}

}  // namespace taffy
