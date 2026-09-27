// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/action_precondition_evaluator.h"

#include <optional>
#include <string>
#include <utility>

#include "taffy/components/intelligence/content/bip_mojom_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

Origin Tuple(std::string serialization) {
  Origin origin;
  origin.kind = OriginKind::kTuple;
  origin.serialization = std::move(serialization);
  return origin;
}

Destination DestinationFor(std::string address) {
  Destination destination;
  const size_t path = address.find('/', std::string("https://").size());
  destination.url_metadata.origin =
      Tuple(path == std::string::npos ? address : address.substr(0, path));
  destination.url_metadata.disclosure = UrlDisclosure::kFullUrl;
  destination.url_metadata.url = std::move(address);
  return destination;
}

AuthorizedActionEnvelope Envelope() {
  AuthorizedActionEnvelope envelope;
  envelope.action_type = ActionType::kActivate;
  envelope.target_handle.tab_id = TabId{"tab_1"};
  envelope.target_handle.frame_id = FrameId{"frame_1"};
  envelope.target_handle.page_epoch = PageEpoch{"epoch_1"};
  envelope.target_handle.graph_revision = 7u;
  envelope.target_handle.node_id = SemanticNodeId{"node_1"};
  envelope.target_handle.expected_origin = Tuple("https://source.example");
  envelope.required_graph_revision = 7u;
  return envelope;
}

ResolvedNodeFacts Facts() {
  ResolvedNodeFacts facts;
  facts.node_id = SemanticNodeId{"node_1"};
  facts.observed_at_revision = 7u;
  facts.role = 9u;
  facts.available_actions = {ActionType::kActivate};
  facts.sensitivity = Sensitivity::kNotSensitive;
  facts.content_trust = ContentTrust::kFirstPartyDocument;
  return facts;
}

Precondition Condition(PreconditionKind kind) {
  Precondition precondition;
  precondition.kind = kind;
  return precondition;
}

ActionPreconditionFailure FailureFor(const AuthorizedActionEnvelope& envelope,
                                     const ResolvedNodeFacts& facts,
                                     bool lease_valid = true) {
  const std::optional<ActionPreconditionFailure> failure =
      EvaluateBrowserActionPreconditions(envelope, facts, lease_valid);
  EXPECT_TRUE(failure.has_value());
  return failure.value_or(ActionPreconditionFailure{
      PreconditionKind::kNodeExists, ActionResultCode::kInternalError});
}

TEST(ActionPreconditionEvaluatorTest, ExactBrowserWitnessesPassTogether) {
  AuthorizedActionEnvelope envelope = Envelope();
  Precondition epoch = Condition(PreconditionKind::kExactPageEpoch);
  epoch.page_epoch = envelope.target_handle.page_epoch;
  Precondition revision =
      Condition(PreconditionKind::kAcceptableGraphRevision);
  revision.min_graph_revision = envelope.required_graph_revision;
  Precondition origin = Condition(PreconditionKind::kExactOrigin);
  origin.origin = envelope.target_handle.expected_origin;
  Precondition role = Condition(PreconditionKind::kNodeRoleUnchanged);
  role.expected_role = 9u;
  Precondition action = Condition(PreconditionKind::kNodeActionAvailable);
  action.expected_action_type = ActionType::kActivate;
  Precondition sensitivity = Condition(PreconditionKind::kNotSensitiveField);
  sensitivity.max_sensitivity = Sensitivity::kNotSensitive;
  envelope.preconditions = {
      epoch,
      revision,
      origin,
      Condition(PreconditionKind::kDocumentActive),
      Condition(PreconditionKind::kNodeExists),
      role,
      action,
      sensitivity,
      Condition(PreconditionKind::kNoUserInteractionSinceLease),
  };
  EXPECT_FALSE(
      EvaluateBrowserActionPreconditions(envelope, Facts(), true).has_value());
}

TEST(ActionPreconditionEvaluatorTest, RedirectSetNeedsANetworkAuthority) {
  AuthorizedActionEnvelope envelope = Envelope();
  envelope.preconditions = {Condition(PreconditionKind::kAllowedRedirectSet)};
  EXPECT_EQ(FailureFor(envelope, Facts()),
            ActionPreconditionFailure(
                PreconditionKind::kAllowedRedirectSet,
                ActionResultCode::kEgressNotAuthorized));
}

TEST(ActionPreconditionEvaluatorTest, LeaseIsReadAgainBesideNodeFacts) {
  AuthorizedActionEnvelope envelope = Envelope();
  envelope.preconditions = {
      Condition(PreconditionKind::kNoUserInteractionSinceLease)};
  EXPECT_FALSE(
      EvaluateBrowserActionPreconditions(envelope, Facts(), true).has_value());
  EXPECT_EQ(FailureFor(envelope, Facts(), false),
            ActionPreconditionFailure(
                PreconditionKind::kNoUserInteractionSinceLease,
                ActionResultCode::kCancelledByUser));
}

TEST(ActionPreconditionEvaluatorTest, UnknownBudgetIsNeverRemaining) {
  AuthorizedActionEnvelope envelope = Envelope();
  envelope.preconditions = {Condition(PreconditionKind::kBudgetRemaining)};
  EXPECT_EQ(FailureFor(envelope, Facts()),
            ActionPreconditionFailure(PreconditionKind::kBudgetRemaining,
                                      ActionResultCode::kBudgetExceeded));
}

TEST(ActionPreconditionEvaluatorTest,
     DestinationClassUsesTheExactFreshDestination) {
  AuthorizedActionEnvelope envelope = Envelope();
  ResolvedNodeFacts facts = Facts();
  Precondition expected = Condition(PreconditionKind::kExpectedDestination);
  expected.expected_destination =
      DestinationFor("https://mail.google.com/inbox");
  facts.destination = expected.expected_destination;
  envelope.preconditions = {
      expected, Condition(PreconditionKind::kDestinationClassAllowed)};
  EXPECT_EQ(FailureFor(envelope, facts),
            ActionPreconditionFailure(
                PreconditionKind::kDestinationClassAllowed,
                ActionResultCode::kDestinationClassRestricted));

  expected.expected_destination = DestinationFor("https://example.com/read");
  facts.destination = expected.expected_destination;
  envelope.preconditions.front() = expected;
  EXPECT_FALSE(
      EvaluateBrowserActionPreconditions(envelope, facts, true).has_value());
}

TEST(ActionPreconditionEvaluatorTest,
     DestinationClassWithoutAnExactBindingRefuses) {
  AuthorizedActionEnvelope envelope = Envelope();
  ResolvedNodeFacts facts = Facts();
  facts.destination = DestinationFor("https://example.com/read");
  envelope.preconditions = {
      Condition(PreconditionKind::kDestinationClassAllowed)};
  EXPECT_EQ(FailureFor(envelope, facts),
            ActionPreconditionFailure(
                PreconditionKind::kDestinationClassAllowed,
                ActionResultCode::kUnsupported));
}

TEST(ActionPreconditionEvaluatorTest, ContentTrustIsFreshAndFailClosed) {
  AuthorizedActionEnvelope envelope = Envelope();
  Precondition trust = Condition(PreconditionKind::kContentTrustAtLeast);
  trust.min_content_trust = ContentTrust::kThirdPartyEmbedded;
  envelope.preconditions = {trust};
  ResolvedNodeFacts facts = Facts();
  EXPECT_FALSE(
      EvaluateBrowserActionPreconditions(envelope, facts, true).has_value());

  facts.content_trust = ContentTrust::kThirdPartyEmbedded;
  EXPECT_EQ(FailureFor(envelope, facts),
            ActionPreconditionFailure(
                PreconditionKind::kContentTrustAtLeast,
                ActionResultCode::kUntrustedContentOrigin));
  facts.content_trust = ContentTrust::kUnknownUntrusted;
  EXPECT_EQ(FailureFor(envelope, facts).second,
            ActionResultCode::kUntrustedContentOrigin);

  envelope.preconditions.front().min_content_trust.reset();
  EXPECT_EQ(FailureFor(envelope, facts).second, ActionResultCode::kUnsupported);
}

TEST(ActionPreconditionEvaluatorTest, RendererCannotMintPrivilegedTrust) {
  mojom::ResolvedNodePtr node = mojom::ResolvedNode::New();
  node->content_trust = mojom::ContentTrust::kTaffyAuthored;
  EXPECT_EQ(FromMojom(*node).content_trust,
            ContentTrust::kUnknownUntrusted);
}

TEST(ActionPreconditionEvaluatorTest,
     BrowserForwardsOnlyNarrowingDestinationAndTrustGuards) {
  Precondition trust = Condition(PreconditionKind::kContentTrustAtLeast);
  trust.min_content_trust = ContentTrust::kThirdPartyEmbedded;
  mojom::PreconditionPtr trust_wire = ToRendererPrecondition(trust);
  ASSERT_TRUE(trust_wire);
  EXPECT_EQ(trust_wire->min_content_trust,
            mojom::ContentTrust::kThirdPartyEmbedded);

  Precondition destination = Condition(PreconditionKind::kExpectedDestination);
  destination.expected_destination =
      DestinationFor("https://example.com/read");
  mojom::PreconditionPtr destination_wire =
      ToRendererPrecondition(destination);
  ASSERT_TRUE(destination_wire);
  ASSERT_TRUE(destination_wire->expected_destination);
  ASSERT_TRUE(destination_wire->expected_destination->url_metadata);
  EXPECT_EQ(destination_wire->expected_destination->url_metadata->url,
            "https://example.com/read");
}

TEST(ActionPreconditionEvaluatorTest, BlockedOwnersReturnSpecificRefusals) {
  AuthorizedActionEnvelope envelope = Envelope();
  envelope.preconditions = {
      Condition(PreconditionKind::kPreparedEffectUnchanged)};
  EXPECT_EQ(FailureFor(envelope, Facts()),
            ActionPreconditionFailure(
                PreconditionKind::kPreparedEffectUnchanged,
                ActionResultCode::kUnsupported));

  envelope.preconditions = {Condition(PreconditionKind::kNoUndeclaredEgress)};
  EXPECT_EQ(FailureFor(envelope, Facts()),
            ActionPreconditionFailure(
                PreconditionKind::kNoUndeclaredEgress,
                ActionResultCode::kEgressNotAuthorized));
}

TEST(ActionPreconditionEvaluatorTest, MissingOperandsNeverPassByOmission) {
  for (PreconditionKind kind : {
           PreconditionKind::kExactPageEpoch,
           PreconditionKind::kAcceptableGraphRevision,
           PreconditionKind::kExactOrigin,
           PreconditionKind::kNotSensitiveField,
       }) {
    AuthorizedActionEnvelope envelope = Envelope();
    envelope.preconditions = {Condition(kind)};
    EXPECT_EQ(FailureFor(envelope, Facts()),
              ActionPreconditionFailure(kind, ActionResultCode::kUnsupported));
  }
}

// A scroll may bring a challenge's answer into view, so the sheet can copy
// the challenge's picture; nothing else reaches a sensitive line that way,
// and a press or a focus on that same line is still refused (decision 0244).
TEST(ActionPreconditionEvaluatorTest,
     AScrollMayReachAChallengeAnswerAndNoOtherSensitiveLine) {
  AuthorizedActionEnvelope envelope = Envelope();
  envelope.action_type = ActionType::kScrollIntoView;
  ResolvedNodeFacts facts = Facts();
  facts.available_actions = {ActionType::kScrollIntoView, ActionType::kActivate,
                             ActionType::kFocus};
  facts.sensitivity = Sensitivity::kChallengeResponse;
  EXPECT_FALSE(
      EvaluateBrowserActionPreconditions(envelope, facts, true).has_value());

  const ActionPreconditionFailure sensitive(
      PreconditionKind::kNotSensitiveField, ActionResultCode::kSensitiveField);
  for (ActionType action : {ActionType::kActivate, ActionType::kFocus}) {
    envelope.action_type = action;
    EXPECT_EQ(sensitive, FailureFor(envelope, facts));
  }
  envelope.action_type = ActionType::kScrollIntoView;
  for (Sensitivity other : {Sensitivity::kIdentity, Sensitivity::kOneTimeCode,
                            Sensitivity::kCredential, Sensitivity::kPayment,
                            Sensitivity::kUnknownSensitive}) {
    facts.sensitivity = other;
    EXPECT_EQ(sensitive, FailureFor(envelope, facts));
  }
}

}  // namespace
}  // namespace taffy
