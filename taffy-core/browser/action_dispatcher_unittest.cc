// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/action_dispatcher.h"

#include "taffy/common/public/bip_action.h"
#include "taffy/components/intelligence/content/action_digest.h"
#include "testing/gtest/include/gtest/gtest.h"

// Two properties, both of which have to hold before a WebContents is anywhere
// near the picture:
//
//   1. The closed action and input tables fail closed. A form input is a
//      browser-owned reference or boolean of exactly the kind its operation
//      accepts; literal bytes and cross-operation rebinding are impossible.
//   2. The action digest changes when the action changes. That is the whole
//      basis of the capability's binding to one exact action: if two different
//      envelopes hashed the same, an authorized envelope could be edited into
//      a different one and still be admitted.

namespace taffy {
namespace {

AuthorizedActionEnvelope MakeEnvelope() {
  AuthorizedActionEnvelope envelope;
  envelope.schema_version = "0.1";
  envelope.dispatch_id = DispatchId{"disp_1"};
  envelope.action_id = ActionId{"act_1"};
  envelope.task_id = TaskId{"task_1"};
  envelope.action_type = ActionType::kActivate;
  envelope.target_handle.tab_id = TabId{"tab_1"};
  envelope.target_handle.frame_id = FrameId{"frame_main"};
  envelope.target_handle.page_epoch = PageEpoch{"epoch_a1"};
  envelope.target_handle.graph_revision = 3;
  envelope.target_handle.node_id = SemanticNodeId{"n_link"};
  envelope.target_handle.expected_origin.kind = OriginKind::kTuple;
  envelope.target_handle.expected_origin.serialization = "https://docs.test";
  envelope.required_graph_revision = 3;
  envelope.absolute_deadline_monotonic_ms = 12000;

  Precondition role;
  role.kind = PreconditionKind::kNodeRoleUnchanged;
  role.expected_role = 9;  // LINK, as a contract member value.
  envelope.preconditions.push_back(role);

  Postcondition committed;
  committed.kind = PostconditionKind::kCommittedNavigation;
  // The claim that makes this action verifiable at all: an origin the
  // navigation is allowed to land on (protocol section 11.6).
  committed.allowed_origins.push_back(envelope.target_handle.expected_origin);
  committed.timeout_ms = 8000;
  envelope.expected_postconditions.push_back(committed);
  return envelope;
}

TEST(ActionDispatcherTest, EveryDeclaredActionIsKnown) {
  EXPECT_TRUE(IsKnownActionType(ActionType::kSetText));
  EXPECT_TRUE(IsKnownActionType(ActionType::kSelectOption));
  EXPECT_TRUE(IsKnownActionType(ActionType::kToggle));
  EXPECT_TRUE(IsKnownActionType(ActionType::kSubmitForm));
  EXPECT_TRUE(IsKnownActionType(ActionType::kActivate));
  EXPECT_TRUE(IsKnownActionType(ActionType::kFocus));
  EXPECT_TRUE(IsKnownActionType(ActionType::kScrollIntoView));
}

TEST(ActionDispatcherTest, RendererGuardsFreezeTheJustResolvedRoleAndAction) {
  ResolvedNodeFacts facts;
  facts.role = 9u;
  facts.available_actions = {ActionType::kScrollIntoView};

  const std::vector<Precondition> guards =
      BrowserDerivedRendererGuards(ActionType::kScrollIntoView, facts);
  ASSERT_EQ(2u, guards.size());
  EXPECT_EQ(PreconditionKind::kNodeRoleUnchanged, guards[0].kind);
  EXPECT_EQ(9u, guards[0].expected_role);
  EXPECT_EQ(PreconditionKind::kNodeActionAvailable, guards[1].kind);
  EXPECT_EQ(ActionType::kScrollIntoView, guards[1].expected_action_type);
}

TEST(ActionDispatcherTest,
     RendererGuardsFreezeTheAcceptedSensitivityForEveryFormAction) {
  ResolvedNodeFacts facts;
  facts.role = 12u;
  facts.sensitivity = Sensitivity::kPersonal;

  for (ActionType action : {ActionType::kSetText, ActionType::kSelectOption,
                            ActionType::kToggle, ActionType::kSubmitForm}) {
    const std::vector<Precondition> guards =
        BrowserDerivedRendererGuards(action, facts);
    ASSERT_EQ(3u, guards.size());
    EXPECT_EQ(PreconditionKind::kNotSensitiveField, guards[2].kind);
    EXPECT_EQ(Sensitivity::kPersonal, guards[2].max_sensitivity);
  }
}

TEST(ActionDispatcherTest,
     RendererGuardsNeverTurnAnUnfillableClassIntoACeiling) {
  ResolvedNodeFacts facts;
  facts.role = 12u;
  facts.sensitivity = Sensitivity::kUnknownSensitive;

  const std::vector<Precondition> guards =
      BrowserDerivedRendererGuards(ActionType::kSetText, facts);

  ASSERT_EQ(3u, guards.size());
  EXPECT_EQ(PreconditionKind::kNotSensitiveField, guards[2].kind);
  EXPECT_EQ(Sensitivity::kNotSensitive, guards[2].max_sensitivity);
}

// The renderer's own check is handed exactly the class the browser admitted a
// scroll for, and only for that scroll (decision 0244).
TEST(ActionDispatcherTest,
     RendererGuardsHandAScrollExactlyTheChallengeAnswersClass) {
  ResolvedNodeFacts facts;
  facts.role = 12u;
  facts.sensitivity = Sensitivity::kChallengeResponse;

  const std::vector<Precondition> guards =
      BrowserDerivedRendererGuards(ActionType::kScrollIntoView, facts);
  ASSERT_EQ(3u, guards.size());
  EXPECT_EQ(PreconditionKind::kNotSensitiveField, guards[2].kind);
  EXPECT_EQ(Sensitivity::kChallengeResponse, guards[2].max_sensitivity);

  // A press or a focus on the same line keeps the renderer's default ceiling,
  // and so does a scroll to any other sensitive line.
  for (ActionType action : {ActionType::kActivate, ActionType::kFocus}) {
    EXPECT_EQ(2u, BrowserDerivedRendererGuards(action, facts).size());
  }
  facts.sensitivity = Sensitivity::kIdentity;
  EXPECT_EQ(
      2u,
      BrowserDerivedRendererGuards(ActionType::kScrollIntoView, facts).size());
}

TEST(ActionDispatcherTest, UnknownActionValueFailsClosed) {
  // Fail closed. A value the build does not know about is denied, not allowed
  // through on the theory that it is probably harmless.
  EXPECT_FALSE(IsKnownActionType(static_cast<ActionType>(200)));
  EXPECT_FALSE(
      NamedInputMatchesAction(static_cast<ActionType>(200), std::nullopt));
}

TEST(ActionDispatcherTest, FormInputsMatchOnlyTheirExactOperation) {
  ActionInput text;
  text.kind = ActionInputKind::kText;
  text.value_reference = ValueReference{"turn-4-values-1-value-0"};
  EXPECT_TRUE(NamedInputMatchesAction(ActionType::kSetText, text));
  EXPECT_FALSE(NamedInputMatchesAction(ActionType::kSelectOption, text));

  ActionInput toggle;
  toggle.kind = ActionInputKind::kToggleState;
  toggle.checked = true;
  EXPECT_TRUE(NamedInputMatchesAction(ActionType::kToggle, toggle));
  EXPECT_FALSE(NamedInputMatchesAction(ActionType::kSetText, toggle));

  ActionInput none;
  EXPECT_TRUE(NamedInputMatchesAction(ActionType::kSubmitForm, none));
  EXPECT_FALSE(NamedInputMatchesAction(ActionType::kSubmitForm, std::nullopt));

  text.text = "model-authored bytes";
  EXPECT_FALSE(NamedInputMatchesAction(ActionType::kSetText, text));
}

TEST(ActionDispatcherTest, DigestIsStableForTheSameEnvelope) {
  EXPECT_EQ(ComputeActionDigest(MakeEnvelope()).value,
            ComputeActionDigest(MakeEnvelope()).value);
}

TEST(ActionDispatcherTest, DigestChangesWhenTheTargetChanges) {
  const std::string baseline = ComputeActionDigest(MakeEnvelope()).value;

  AuthorizedActionEnvelope other = MakeEnvelope();
  other.target_handle.node_id = SemanticNodeId{"n_other"};
  EXPECT_NE(baseline, ComputeActionDigest(other).value);

  other = MakeEnvelope();
  other.target_handle.page_epoch = PageEpoch{"epoch_b2"};
  EXPECT_NE(baseline, ComputeActionDigest(other).value);

  other = MakeEnvelope();
  other.target_handle.expected_origin.serialization = "https://evil.test";
  EXPECT_NE(baseline, ComputeActionDigest(other).value);
}

TEST(ActionDispatcherTest, DigestChangesWhenTheActionOrItsGuardsChange) {
  const std::string baseline = ComputeActionDigest(MakeEnvelope()).value;

  AuthorizedActionEnvelope other = MakeEnvelope();
  other.action_type = ActionType::kFocus;
  EXPECT_NE(baseline, ComputeActionDigest(other).value);

  other = MakeEnvelope();
  other.preconditions.clear();
  EXPECT_NE(baseline, ComputeActionDigest(other).value)
      << "dropping a precondition must not preserve the digest";

  other = MakeEnvelope();
  other.expected_postconditions.clear();
  EXPECT_NE(baseline, ComputeActionDigest(other).value)
      << "dropping the declared effect must not preserve the digest";

  other = MakeEnvelope();
  other.idempotency_policy = IdempotencyPolicy::kPureRead;
  EXPECT_NE(baseline, ComputeActionDigest(other).value);

  other = MakeEnvelope();
  Precondition trust;
  trust.kind = PreconditionKind::kContentTrustAtLeast;
  trust.min_content_trust = ContentTrust::kThirdPartyEmbedded;
  other.preconditions.push_back(trust);
  EXPECT_NE(baseline, ComputeActionDigest(other).value)
      << "changing the trust condition must change capability identity";
}

TEST(ActionDispatcherTest, DigestIsLengthDelimited) {
  // Without length prefixes, moving a character between two adjacent string
  // fields would leave the encoding — and therefore the digest — unchanged.
  AuthorizedActionEnvelope left = MakeEnvelope();
  left.task_id = TaskId{"ab"};
  left.action_id = ActionId{"c"};

  AuthorizedActionEnvelope right = MakeEnvelope();
  right.task_id = TaskId{"a"};
  right.action_id = ActionId{"bc"};

  EXPECT_NE(ComputeActionDigest(left).value, ComputeActionDigest(right).value);
}

TEST(ActionDispatcherTest, DigestCoversTheNamedValueAndNeverTheValue) {
  // Which held value a field is to receive is part of what the action *is*, so
  // changing the name has to change the digest the capability was bound to.
  // The name is opaque and is not a function of the bytes, so hashing it
  // discloses nothing about them - which is the only reason it can be hashed
  // at all. The bytes themselves are never in an envelope and are never
  // hashed: a digest over a domain the size of a one-time code is the code.
  AuthorizedActionEnvelope left = MakeEnvelope();
  ActionInput input;
  input.kind = ActionInputKind::kText;
  input.sensitivity = Sensitivity::kIdentity;
  input.value_reference = ValueReference{"val_a"};
  left.input = input;

  AuthorizedActionEnvelope right = MakeEnvelope();
  input.value_reference = ValueReference{"val_b"};
  right.input = input;

  EXPECT_NE(ComputeActionDigest(left).value, ComputeActionDigest(right).value);
  EXPECT_NE(ComputeActionDigest(left).value,
            ComputeActionDigest(MakeEnvelope()).value)
      << "naming a value must not hash the same as naming none";
}

TEST(ActionDispatcherTest, ActionAndBrowserCommandDigestsDoNotCollide) {
  AuthorizedBrowserCommand command;
  command.dispatch_id = DispatchId{"disp_1"};
  command.action_id = ActionId{"act_1"};
  command.task_id = TaskId{"task_1"};
  command.tab_id = TabId{"tab_1"};
  command.command_type = BrowserCommandType::kNavigate;
  command.argument = "https://docs.test/";
  command.absolute_deadline_monotonic_ms = 12000;

  EXPECT_NE(ComputeActionDigest(MakeEnvelope()).value,
            ComputeBrowserCommandDigest(command).value);
}

TEST(ActionDispatcherTest, HistoryDirectionIsCapabilityBound) {
  AuthorizedBrowserCommand back;
  back.dispatch_id = DispatchId{"disp_1"};
  back.action_id = ActionId{"act_1"};
  back.task_id = TaskId{"task_1"};
  back.tab_id = TabId{"tab_1"};
  back.command_type = BrowserCommandType::kGoBack;
  back.absolute_deadline_monotonic_ms = 12000;

  AuthorizedBrowserCommand forward = back;
  forward.command_type = BrowserCommandType::kGoForward;

  EXPECT_NE(ComputeBrowserCommandDigest(back).value,
            ComputeBrowserCommandDigest(forward).value)
      << "back and forward must never share one grant";

  AuthorizedBrowserCommand reload = back;
  reload.command_type = BrowserCommandType::kReload;
  AuthorizedBrowserCommand stop = back;
  stop.command_type = BrowserCommandType::kStopLoading;
  EXPECT_NE(ComputeBrowserCommandDigest(back).value,
            ComputeBrowserCommandDigest(reload).value);
  EXPECT_NE(ComputeBrowserCommandDigest(forward).value,
            ComputeBrowserCommandDigest(stop).value);
  EXPECT_NE(ComputeBrowserCommandDigest(reload).value,
            ComputeBrowserCommandDigest(stop).value);
}

TEST(ActionDispatcherTest, ObservedLinkDigestBindsEverySourceHandleFact) {
  AuthorizedBrowserCommand command;
  command.dispatch_id = DispatchId{"disp_1"};
  command.action_id = ActionId{"act_1"};
  command.task_id = TaskId{"task_1"};
  command.tab_id = TabId{"tab_1"};
  command.command_type = BrowserCommandType::kOpenObservedLink;
  command.argument = "https://destination.test/path";
  command.absolute_deadline_monotonic_ms = 12000;
  NodeHandle handle;
  handle.tab_id = command.tab_id;
  handle.frame_id = FrameId{"frame_1"};
  handle.page_epoch = PageEpoch{"epoch_1"};
  handle.graph_revision = 7u;
  handle.node_id = SemanticNodeId{"node_1"};
  handle.expected_origin.kind = OriginKind::kTuple;
  handle.expected_origin.serialization = "https://source.test";
  command.source_handle = handle;
  const std::string baseline = ComputeBrowserCommandDigest(command).value;

  AuthorizedBrowserCommand changed = command;
  changed.source_handle->node_id = SemanticNodeId{"node_2"};
  EXPECT_NE(baseline, ComputeBrowserCommandDigest(changed).value);
  changed = command;
  changed.source_handle->page_epoch = PageEpoch{"epoch_2"};
  EXPECT_NE(baseline, ComputeBrowserCommandDigest(changed).value);
  changed = command;
  changed.source_handle->graph_revision = 8u;
  EXPECT_NE(baseline, ComputeBrowserCommandDigest(changed).value);
  changed = command;
  changed.source_handle.reset();
  EXPECT_NE(baseline, ComputeBrowserCommandDigest(changed).value);
}

}  // namespace
}  // namespace taffy
