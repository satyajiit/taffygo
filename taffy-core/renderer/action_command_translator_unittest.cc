// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/action_command_translator.h"

#include <utility>

#include "testing/gtest/include/gtest/gtest.h"

// Protocol section 11. Every test here is the same claim from a different
// angle: a command this endpoint cannot fully evaluate is refused, and a
// command whose parts do not agree with each other is refused rather than
// reconciled into one that does.

namespace taffy {
namespace {

mojom::RendererActionCommandPtr ActivateCommand() {
  auto command = mojom::RendererActionCommand::New();
  command->schema_version = "0.1";
  command->command_id = "cmd-1";
  command->node_id = "n7";
  command->page_epoch = "epoch_a1";
  command->required_graph_revision = 4;
  command->operation = mojom::ActionType::kActivate;

  auto role = mojom::Precondition::New();
  role->kind = mojom::PreconditionKind::kNodeRoleUnchanged;
  role->expected_role = mojom::SemanticRole::kLink;
  command->renderer_preconditions.push_back(std::move(role));
  return command;
}

TEST(ActionCommandTranslatorTest, TranslatesAWellFormedActivation) {
  const ActionCommandTranslator::Result result =
      ActionCommandTranslator::Translate(*ActivateCommand());
  ASSERT_TRUE(result.request.has_value());
  EXPECT_EQ(result.request->action, ActionKind::kActivate);
  EXPECT_EQ(result.request->precondition.node_id, SemanticNodeId("n7"));
  EXPECT_EQ(result.request->precondition.expected_page_epoch,
            PageEpoch("epoch_a1"));
  ASSERT_TRUE(result.request->precondition.expected_role.has_value());
  EXPECT_EQ(result.request->precondition.expected_role.value(),
            SemanticRole::kLink);
}

mojom::RendererActionCommandPtr SetTextCommand() {
  mojom::RendererActionCommandPtr command = ActivateCommand();
  command->operation = mojom::ActionType::kSetText;
  command->renderer_preconditions.front()->expected_role =
      mojom::SemanticRole::kTextField;
  command->input = mojom::ActionInput::New();
  command->input->kind = mojom::ActionInputKind::kText;
  command->input->sensitivity = mojom::Sensitivity::kPersonal;
  command->input->text = "Shrewsbury";
  return command;
}

TEST(ActionCommandTranslatorTest, TranslatesAWriteAndCarriesItsValue) {
  // Protocol 0.8 specified these four rather than reserving them, so the
  // translator's job changed from refusing them outright to reading them
  // exactly. What still refuses a write is upstream of here: the milestone
  // that owns the tool, the action class no milestone authorizes, the browser
  // dispatcher, and the node's own advertised actions[].
  const ActionCommandTranslator::Result result =
      ActionCommandTranslator::Translate(*SetTextCommand());
  ASSERT_TRUE(result.request.has_value());
  EXPECT_EQ(result.request->action, ActionKind::kSetText);
  ASSERT_TRUE(result.request->value.text.has_value());
  EXPECT_EQ(result.request->value.text.value(), "Shrewsbury");
  EXPECT_FALSE(result.request->value.checked.has_value());
}

TEST(ActionCommandTranslatorTest, AValueReferenceIsRefused) {
  // The browser resolves a reference and spends it before it builds a
  // command, so one arriving in a sandboxed process is a name that outlived
  // its use - and a reference that outlives its use is a capability wearing a
  // different name. Note that this command is otherwise perfectly executable:
  // ignoring the extra field is exactly the outcome refused, because a field
  // a consumer ignores is a field nobody notices is being sent.
  mojom::RendererActionCommandPtr command = SetTextCommand();
  command->input->value_reference = "val_shipping_city";
  const ActionCommandTranslator::Result result =
      ActionCommandTranslator::Translate(*command);
  EXPECT_FALSE(result.request.has_value());
  EXPECT_EQ(result.refusal, mojom::RendererActionOutcome::kUnsupported);
}

TEST(ActionCommandTranslatorTest, AnInputThatDoesNotMatchItsOperationIsRefused) {
  // A command whose operation and whose payload disagree is contradicting
  // itself, and a self-contradictory command is not one to execute the
  // agreeable half of.
  {
    mojom::RendererActionCommandPtr command = SetTextCommand();
    command->input->kind = mojom::ActionInputKind::kToggleState;
    command->input->text.reset();
    command->input->checked = true;
    EXPECT_FALSE(
        ActionCommandTranslator::Translate(*command).request.has_value());
  }
  {
    // The right kind, with a second operand riding along.
    mojom::RendererActionCommandPtr command = SetTextCommand();
    command->input->checked = true;
    EXPECT_FALSE(
        ActionCommandTranslator::Translate(*command).request.has_value());
  }
  {
    // A read-oriented operation handed an input at all.
    mojom::RendererActionCommandPtr command = ActivateCommand();
    command->input = mojom::ActionInput::New();
    command->input->kind = mojom::ActionInputKind::kText;
    command->input->text = "Shrewsbury";
    EXPECT_FALSE(
        ActionCommandTranslator::Translate(*command).request.has_value());
  }
  {
    // A write handed no input at all. Only the browser builds these, so this
    // is a defect on the far side of the boundary and the honest answer is a
    // refusal rather than a smaller version of what was asked for.
    mojom::RendererActionCommandPtr command = SetTextCommand();
    command->input.reset();
    EXPECT_FALSE(
        ActionCommandTranslator::Translate(*command).request.has_value());
  }
}

TEST(ActionCommandTranslatorTest, ASubmissionTakesNoInput) {
  // The target of a submission is the control the page offered, so there is
  // nothing left to carry. A submission arriving with a payload is describing
  // something this contract cannot express.
  mojom::RendererActionCommandPtr command = ActivateCommand();
  command->operation = mojom::ActionType::kSubmitForm;
  command->renderer_preconditions.front()->expected_role =
      mojom::SemanticRole::kButton;
  ASSERT_TRUE(ActionCommandTranslator::Translate(*command).request.has_value());

  command->input = mojom::ActionInput::New();
  command->input->kind = mojom::ActionInputKind::kText;
  command->input->text = "submit";
  EXPECT_FALSE(
      ActionCommandTranslator::Translate(*command).request.has_value());
}

TEST(ActionCommandTranslatorTest, UnevaluablePreconditionsAreRefused) {
  // A precondition this endpoint cannot check is a refusal, never an
  // assumption that it holds. Redirect policy and budgets are browser-owned;
  // no adapter reads a value, so no digest exists to compare; and whether the
  // user has interacted since the lease was issued is knowledge a renderer
  // must not be believed about.
  for (mojom::PreconditionKind kind :
       {mojom::PreconditionKind::kAllowedRedirectSet,
        mojom::PreconditionKind::kExpectedValueDigest,
        mojom::PreconditionKind::kNoUserInteractionSinceLease,
        mojom::PreconditionKind::kBudgetRemaining}) {
    mojom::RendererActionCommandPtr command = ActivateCommand();
    auto precondition = mojom::Precondition::New();
    precondition->kind = kind;
    command->renderer_preconditions.push_back(std::move(precondition));

    const ActionCommandTranslator::Result result =
        ActionCommandTranslator::Translate(*command);
    EXPECT_FALSE(result.request.has_value());
    EXPECT_EQ(result.refusal, mojom::RendererActionOutcome::kUnsupported);
    EXPECT_EQ(result.failed_precondition, kind);
  }
}

TEST(ActionCommandTranslatorTest, ContentTrustGuardIsNarrowedAndRequired) {
  mojom::RendererActionCommandPtr command = ActivateCommand();
  auto trust = mojom::Precondition::New();
  trust->kind = mojom::PreconditionKind::kContentTrustAtLeast;
  trust->min_content_trust = mojom::ContentTrust::kThirdPartyEmbedded;
  command->renderer_preconditions.push_back(std::move(trust));

  ActionCommandTranslator::Result result =
      ActionCommandTranslator::Translate(*command);
  ASSERT_TRUE(result.request.has_value());
  EXPECT_TRUE(result.request->precondition.content_trust_check_declared);
  EXPECT_EQ(result.request->precondition.forbidden_content_trust,
            RendererContentTrust::kThirdPartyEmbedded);

  command->renderer_preconditions.back()->min_content_trust.reset();
  result = ActionCommandTranslator::Translate(*command);
  EXPECT_FALSE(result.request.has_value());
  EXPECT_EQ(result.failed_precondition,
            mojom::PreconditionKind::kContentTrustAtLeast);
}

TEST(ActionCommandTranslatorTest, PrivilegedTrustFloorCannotBeMintedLocally) {
  mojom::RendererActionCommandPtr command = ActivateCommand();
  auto trust = mojom::Precondition::New();
  trust->kind = mojom::PreconditionKind::kContentTrustAtLeast;
  trust->min_content_trust = mojom::ContentTrust::kTaffyAuthored;
  command->renderer_preconditions.push_back(std::move(trust));

  const ActionCommandTranslator::Result result =
      ActionCommandTranslator::Translate(*command);
  ASSERT_TRUE(result.request.has_value());
  EXPECT_TRUE(result.request->precondition.content_trust_check_declared);
  EXPECT_FALSE(
      result.request->precondition.forbidden_content_trust.has_value());
}

TEST(ActionCommandTranslatorTest, MinimizedDestinationCannotBeCompared) {
  // Comparing origins and calling it a destination match is exactly the swap
  // this precondition exists to catch.
  mojom::RendererActionCommandPtr command = ActivateCommand();
  auto precondition = mojom::Precondition::New();
  precondition->kind = mojom::PreconditionKind::kExpectedDestination;
  precondition->expected_destination = mojom::Destination::New();
  precondition->expected_destination->url_metadata = mojom::UrlMetadata::New();
  precondition->expected_destination->url_metadata->origin =
      mojom::Origin::New();
  precondition->expected_destination->url_metadata->disclosure =
      mojom::UrlDisclosure::kOriginOnly;
  command->renderer_preconditions.push_back(std::move(precondition));

  const ActionCommandTranslator::Result result =
      ActionCommandTranslator::Translate(*command);
  EXPECT_FALSE(result.request.has_value());
  EXPECT_EQ(result.failed_precondition,
            mojom::PreconditionKind::kExpectedDestination);
}

TEST(ActionCommandTranslatorTest, SelfContradictoryCommandIsRefused) {
  mojom::RendererActionCommandPtr command = ActivateCommand();
  auto precondition = mojom::Precondition::New();
  precondition->kind = mojom::PreconditionKind::kNodeActionAvailable;
  precondition->expected_action_type = mojom::ActionType::kFocus;
  command->renderer_preconditions.push_back(std::move(precondition));

  const ActionCommandTranslator::Result result =
      ActionCommandTranslator::Translate(*command);
  EXPECT_FALSE(result.request.has_value());
  EXPECT_EQ(result.failed_precondition,
            mojom::PreconditionKind::kNodeActionAvailable);
}

TEST(ActionCommandTranslatorTest, ConsequentialActionMustDeclareARole) {
  // "The browser forgot to send one" must not become "the renderer acted on
  // whatever is there now".
  mojom::RendererActionCommandPtr command = ActivateCommand();
  command->renderer_preconditions.clear();
  const ActionCommandTranslator::Result result =
      ActionCommandTranslator::Translate(*command);
  EXPECT_FALSE(result.request.has_value());
  EXPECT_EQ(result.refusal,
            mojom::RendererActionOutcome::kPreconditionFailed);
  EXPECT_EQ(result.failed_precondition,
            mojom::PreconditionKind::kNodeRoleUnchanged);
}

TEST(ActionCommandTranslatorTest, ScrollAlsoRequiresAnExactRoleDeclaration) {
  // Moving the viewport is non-consequential, but allowing a replacement node
  // would still widen the exact target the browser admitted.
  mojom::RendererActionCommandPtr command = ActivateCommand();
  command->operation = mojom::ActionType::kScrollIntoView;
  command->renderer_preconditions.clear();
  const ActionCommandTranslator::Result result =
      ActionCommandTranslator::Translate(*command);
  EXPECT_FALSE(result.request.has_value());
  EXPECT_EQ(result.refusal,
            mojom::RendererActionOutcome::kPreconditionFailed);
  EXPECT_EQ(result.failed_precondition,
            mojom::PreconditionKind::kNodeRoleUnchanged);
}

TEST(ActionCommandTranslatorTest, StatePreconditionsArriveInBothLists) {
  mojom::RendererActionCommandPtr command = ActivateCommand();

  auto asserted = mojom::Precondition::New();
  asserted->kind = mojom::PreconditionKind::kNodeStateAsserted;
  asserted->node_state = mojom::NodeState::kVisible;
  command->renderer_preconditions.push_back(std::move(asserted));

  auto absent = mojom::Precondition::New();
  absent->kind = mojom::PreconditionKind::kNodeStateAbsent;
  absent->node_state = mojom::NodeState::kObscured;
  command->renderer_preconditions.push_back(std::move(absent));

  const ActionCommandTranslator::Result result =
      ActionCommandTranslator::Translate(*command);
  ASSERT_TRUE(result.request.has_value());
  EXPECT_EQ(result.request->precondition.required_states.size(), 1u);
  EXPECT_EQ(result.request->precondition.required_states[0],
            NodeState::kVisible);
  EXPECT_EQ(result.request->precondition.forbidden_states.size(), 1u);
  EXPECT_EQ(result.request->precondition.forbidden_states[0],
            NodeState::kObscured);
}

TEST(ActionCommandTranslatorTest, OpaqueOriginIsNotCompared) {
  // Every opaque origin serializes alike, so an opaque origin precondition
  // sets no expectation here at all. The browser's nonce comparison is the
  // one that means something.
  mojom::RendererActionCommandPtr command = ActivateCommand();
  auto precondition = mojom::Precondition::New();
  precondition->kind = mojom::PreconditionKind::kExactOrigin;
  precondition->origin = mojom::Origin::New();
  precondition->origin->kind = mojom::OriginKind::kOpaque;
  command->renderer_preconditions.push_back(std::move(precondition));

  const ActionCommandTranslator::Result result =
      ActionCommandTranslator::Translate(*command);
  ASSERT_TRUE(result.request.has_value());
  EXPECT_FALSE(
      result.request->precondition.expected_origin_serialization.has_value());
}

}  // namespace
}  // namespace taffy
