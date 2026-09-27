// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/intelligence/content/action_value_resolution.h"

#include <string>

#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "content/public/test/browser_task_environment.h"
#include "taffy/common/public/bip_action.h"
#include "testing/gtest/include/gtest/gtest.h"

// The one crossing between "a value the browser holds" and "the bytes in a
// message", and the properties that make it one-way.
//
//   * an envelope that already carries bytes is refused, because the only
//     author upstream of here is the assistant runtime;
//   * a named value becomes the bytes on the command and the name does not
//     travel with them;
//   * the reference is spent by the crossing, so the same envelope replayed
//     resolves to nothing;
//   * the classification that decides whether the field may be written is the
//     one the browser re-read, never the one the envelope asserted;
//   * every refusal maps onto a code that reads as a failure downstream.

namespace taffy {
namespace {

class ActionValueResolutionTest : public testing::Test {
 protected:
  void SetUp() override { vault_.BeginGeneration("profile_1", 1u); }

  base::TimeTicks Now() const { return task_environment_.NowTicks(); }

  ValueReference MintFor(Sensitivity field_class, std::string value) {
    const std::optional<FillClearance> clearance =
        FillClearance::For(field_class);
    CHECK(clearance.has_value());
    return vault_.Mint(task_, *clearance, std::move(value),
                       Now() + base::Seconds(30));
  }

  // An envelope that names a value for a text field.
  AuthorizedActionEnvelope NamingEnvelope(const ValueReference& reference) {
    AuthorizedActionEnvelope envelope;
    envelope.task_id = task_;
    envelope.action_type = ActionType::kSetText;
    ActionInput input;
    input.kind = ActionInputKind::kText;
    // What the proposal believed about the field. Deliberately the mildest
    // classification there is, so that a test asserting the browser's own
    // reading wins cannot pass by accident.
    input.sensitivity = Sensitivity::kNotSensitive;
    input.value_reference = reference;
    envelope.input = std::move(input);
    return envelope;
  }

  ResolvedNodeFacts FactsClassified(Sensitivity sensitivity) {
    ResolvedNodeFacts facts;
    facts.node_id = SemanticNodeId{"n_field"};
    facts.observed_at_revision = 4u;
    facts.sensitivity = sensitivity;
    return facts;
  }

  content::BrowserTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  ValueReferenceVault vault_;
  const TaskId task_{"task_1"};
};

TEST_F(ActionValueResolutionTest, ANamedValueBecomesTheBytesAndLosesItsName) {
  const ValueReference reference =
      MintFor(Sensitivity::kIdentity, "1234 5678 9012");
  const AuthorizedActionEnvelope envelope = NamingEnvelope(reference);

  ResolvedActionInput resolved = ResolveActionInput(
      envelope, FactsClassified(Sensitivity::kIdentity), &vault_, Now());
  ASSERT_FALSE(resolved.refusal.has_value());
  ASSERT_TRUE(resolved.input.has_value());
  EXPECT_EQ("1234 5678 9012", resolved.input->text.value_or(std::string()));
  // The name does not travel with the bytes. A renderer holding both could
  // present the name again.
  EXPECT_FALSE(resolved.input->value_reference.has_value());
  EXPECT_TRUE(IsResolvedValueInputShape(*resolved.input));

  // And the wire form the browser actually sends carries no name either.
  const mojom::ActionInputPtr wire = ToMojom(*resolved.input);
  ASSERT_TRUE(wire);
  EXPECT_FALSE(wire->value_reference.has_value());
  EXPECT_EQ("1234 5678 9012", wire->text.value_or(std::string()));
}

TEST_F(ActionValueResolutionTest, TheCrossingSpendsTheReference) {
  const ValueReference reference =
      MintFor(Sensitivity::kIdentity, "1234 5678 9012");
  const AuthorizedActionEnvelope envelope = NamingEnvelope(reference);

  ResolvedActionInput first = ResolveActionInput(
      envelope, FactsClassified(Sensitivity::kIdentity), &vault_, Now());
  ASSERT_TRUE(first.input.has_value());

  // The same envelope again - a replay, a retry after an ambiguous dispatch,
  // or a second dispatch of a journalled intent. It resolves to nothing.
  ResolvedActionInput second = ResolveActionInput(
      envelope, FactsClassified(Sensitivity::kIdentity), &vault_, Now());
  EXPECT_FALSE(second.input.has_value());
  ASSERT_TRUE(second.refusal.has_value());
  EXPECT_EQ(ActionResultCode::kValueReferenceUnknown, *second.refusal);
}

TEST_F(ActionValueResolutionTest, AnEnvelopeCarryingBytesIsRefused) {
  AuthorizedActionEnvelope envelope;
  envelope.task_id = task_;
  envelope.action_type = ActionType::kSetText;
  ActionInput input;
  input.kind = ActionInputKind::kText;
  input.sensitivity = Sensitivity::kNotSensitive;
  // The model wrote out what it wanted typed. There is no branch that uses it.
  input.text = "whatever the model decided";
  envelope.input = std::move(input);

  ResolvedActionInput resolved = ResolveActionInput(
      envelope, FactsClassified(Sensitivity::kNotSensitive), &vault_, Now());
  EXPECT_FALSE(resolved.input.has_value());
  ASSERT_TRUE(resolved.refusal.has_value());
  EXPECT_EQ(ActionResultCode::kDeniedByPolicy, *resolved.refusal);
}

TEST_F(ActionValueResolutionTest, AnEnvelopeNamingAndCarryingIsRefused) {
  const ValueReference reference =
      MintFor(Sensitivity::kIdentity, "1234 5678 9012");
  AuthorizedActionEnvelope envelope = NamingEnvelope(reference);
  // Both halves at once, which is the shape that would let a carried value
  // ride in behind a legitimate name.
  envelope.input->text = "and also this";

  ResolvedActionInput resolved = ResolveActionInput(
      envelope, FactsClassified(Sensitivity::kIdentity), &vault_, Now());
  EXPECT_FALSE(resolved.input.has_value());
  ASSERT_TRUE(resolved.refusal.has_value());
  EXPECT_EQ(ActionResultCode::kDeniedByPolicy, *resolved.refusal);
  // Refusing did not spend the person's value.
  EXPECT_TRUE(vault_.Holds(reference));
}

TEST_F(ActionValueResolutionTest, ACredentialFieldRefusesTheAction) {
  const ValueReference reference =
      MintFor(Sensitivity::kIdentity, "1234 5678 9012");
  const AuthorizedActionEnvelope envelope = NamingEnvelope(reference);

  ResolvedActionInput resolved = ResolveActionInput(
      envelope, FactsClassified(Sensitivity::kCredential), &vault_, Now());
  EXPECT_FALSE(resolved.input.has_value());
  ASSERT_TRUE(resolved.refusal.has_value());
  EXPECT_EQ(ActionResultCode::kSensitiveField, *resolved.refusal);
  EXPECT_TRUE(vault_.Holds(reference));
}

TEST_F(ActionValueResolutionTest, TheBrowsersOwnReadingDecides) {
  // The envelope says the field is ordinary. The browser's re-read of the node
  // says it is a credential field. The envelope's claim is the claim of the
  // least trusted author on the path, and it loses.
  const ValueReference reference =
      MintFor(Sensitivity::kIdentity, "1234 5678 9012");
  AuthorizedActionEnvelope envelope = NamingEnvelope(reference);
  ASSERT_EQ(Sensitivity::kNotSensitive, envelope.input->sensitivity);

  ResolvedActionInput resolved = ResolveActionInput(
      envelope, FactsClassified(Sensitivity::kCredential), &vault_, Now());
  ASSERT_TRUE(resolved.refusal.has_value());
  EXPECT_EQ(ActionResultCode::kSensitiveField, *resolved.refusal);
}

TEST_F(ActionValueResolutionTest, AResolvedInputCarriesTheClassTheBrowserRead) {
  const ValueReference reference = MintFor(Sensitivity::kPersonal, "Kolkata");
  const AuthorizedActionEnvelope envelope = NamingEnvelope(reference);

  ResolvedActionInput resolved = ResolveActionInput(
      envelope, FactsClassified(Sensitivity::kPersonal), &vault_, Now());
  ASSERT_TRUE(resolved.input.has_value());
  EXPECT_EQ(Sensitivity::kPersonal, resolved.input->sensitivity);
}

TEST_F(ActionValueResolutionTest, NoVaultResolvesNothing) {
  const ValueReference reference =
      MintFor(Sensitivity::kIdentity, "1234 5678 9012");
  const AuthorizedActionEnvelope envelope = NamingEnvelope(reference);

  ResolvedActionInput resolved = ResolveActionInput(
      envelope, FactsClassified(Sensitivity::kIdentity), nullptr, Now());
  EXPECT_FALSE(resolved.input.has_value());
  ASSERT_TRUE(resolved.refusal.has_value());
  EXPECT_EQ(ActionResultCode::kValueReferenceUnknown, *resolved.refusal);
}

TEST_F(ActionValueResolutionTest, AnotherTasksValueDoesNotResolve) {
  const ValueReference reference =
      MintFor(Sensitivity::kIdentity, "1234 5678 9012");
  AuthorizedActionEnvelope envelope = NamingEnvelope(reference);
  envelope.task_id = TaskId{"task_2"};

  ResolvedActionInput resolved = ResolveActionInput(
      envelope, FactsClassified(Sensitivity::kIdentity), &vault_, Now());
  ASSERT_TRUE(resolved.refusal.has_value());
  EXPECT_EQ(ActionResultCode::kValueReferenceUnknown, *resolved.refusal);
  EXPECT_TRUE(vault_.Holds(reference));
}

TEST_F(ActionValueResolutionTest, AnEnvelopeWithNoInputResolvesToNoInput) {
  AuthorizedActionEnvelope envelope;
  envelope.task_id = task_;
  envelope.action_type = ActionType::kActivate;

  ResolvedActionInput resolved = ResolveActionInput(
      envelope, FactsClassified(Sensitivity::kNotSensitive), &vault_, Now());
  EXPECT_FALSE(resolved.refusal.has_value());
  EXPECT_FALSE(resolved.input.has_value());
}

TEST_F(ActionValueResolutionTest, AToggleStateIsCarriedRatherThanNamed) {
  // Two possible values, both of which the page already names. Nothing about
  // a toggle state is content authored for the page, so there is no reference
  // to resolve and none is required.
  AuthorizedActionEnvelope envelope;
  envelope.task_id = task_;
  envelope.action_type = ActionType::kToggle;
  ActionInput input;
  input.kind = ActionInputKind::kToggleState;
  input.sensitivity = Sensitivity::kNotSensitive;
  input.checked = true;
  envelope.input = std::move(input);

  ResolvedActionInput resolved = ResolveActionInput(
      envelope, FactsClassified(Sensitivity::kNotSensitive), &vault_, Now());
  ASSERT_TRUE(resolved.input.has_value());
  EXPECT_TRUE(resolved.input->checked.value_or(false));
  EXPECT_FALSE(resolved.input->value_reference.has_value());

  // A toggle that also names a value is contradicting itself.
  envelope.input->value_reference = ValueReference{"val_something"};
  ResolvedActionInput contradictory = ResolveActionInput(
      envelope, FactsClassified(Sensitivity::kNotSensitive), &vault_, Now());
  EXPECT_FALSE(contradictory.input.has_value());
  EXPECT_TRUE(contradictory.refusal.has_value());
}

TEST_F(ActionValueResolutionTest, EveryResolutionFailureFailsClosed) {
  for (const ValueResolution verdict :
       {ValueResolution::kFieldMayNotBeFilled,
        ValueResolution::kUnknownReference, ValueResolution::kNotThisTask,
        ValueResolution::kExpired, ValueResolution::kFieldClassMismatch}) {
    EXPECT_FALSE(IsActionSuccess(ResolutionToResultCode(verdict)));
  }
  EXPECT_EQ(ActionResultCode::kSensitiveField,
            ResolutionToResultCode(ValueResolution::kFieldMayNotBeFilled));
  for (const ValueResolution verdict :
       {ValueResolution::kUnknownReference, ValueResolution::kNotThisTask,
        ValueResolution::kExpired, ValueResolution::kFieldClassMismatch}) {
    EXPECT_EQ(ActionResultCode::kValueReferenceUnknown,
              ResolutionToResultCode(verdict));
  }
}

// --- the shape rules, on their own ------------------------------------------

TEST(ActionInputShapeTest, AnEnvelopeMayNameAndMayNotCarry) {
  ActionInput named;
  named.kind = ActionInputKind::kText;
  named.value_reference = ValueReference{"val_1"};
  EXPECT_TRUE(IsNamedValueInputShape(named));
  EXPECT_FALSE(IsResolvedValueInputShape(named));

  ActionInput carried;
  carried.kind = ActionInputKind::kText;
  carried.text = "typed by the model";
  EXPECT_FALSE(IsNamedValueInputShape(carried));
  EXPECT_TRUE(IsResolvedValueInputShape(carried));

  ActionInput both;
  both.kind = ActionInputKind::kText;
  both.value_reference = ValueReference{"val_1"};
  both.text = "typed by the model";
  EXPECT_FALSE(IsNamedValueInputShape(both));
  EXPECT_FALSE(IsResolvedValueInputShape(both));

  ActionInput neither;
  neither.kind = ActionInputKind::kText;
  EXPECT_FALSE(IsNamedValueInputShape(neither));
  EXPECT_FALSE(IsResolvedValueInputShape(neither));
}

TEST(ActionInputShapeTest, AnEmptyReferenceIsNotAReference) {
  ActionInput named;
  named.kind = ActionInputKind::kText;
  named.value_reference = ValueReference{};
  EXPECT_FALSE(IsNamedValueInputShape(named));
}

TEST(ActionInputShapeTest, AnOperationTakingNoInputCarriesNothing) {
  ActionInput none;
  none.kind = ActionInputKind::kNone;
  EXPECT_TRUE(IsNamedValueInputShape(none));
  EXPECT_TRUE(IsResolvedValueInputShape(none));

  none.value_reference = ValueReference{"val_1"};
  EXPECT_FALSE(IsNamedValueInputShape(none));
  EXPECT_FALSE(IsResolvedValueInputShape(none));
}

}  // namespace
}  // namespace taffy
