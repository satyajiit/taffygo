// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "base/test/bind.h"
#include "content/public/test/browser_task_environment.h"
#include "taffy/browser/field_value_request_coordinator_test_support.h"
#include "testing/gtest/include/gtest/gtest.h"

// What this suite holds down, sentence by sentence from decision 0088 and
// from the coordinator's own header.
//
//   * a field whose classification has no clearance is refused **by name**,
//     never dropped quietly — the person can only retype it into the page
//     themselves, and they cannot do that if nothing says which one it was;
//   * a mint the vault refuses is handled rather than assumed to have worked,
//     including the case that has nothing to do with the field at all: no
//     active profile generation;
//   * the number that crosses to the isolated core is the complete confirmed
//     sequence or zero, never a selectable prefix;
//   * a request is answered once, and a second answer to the same one is
//     refused rather than minted under names the first answer already took;
//   * the name the browser mints under is the one the task engine derives.

namespace taffy::field_value_request_coordinator_test {

FieldValueRequestCoordinatorTest::FieldValueRequestCoordinatorTest()
    : task_environment_(std::make_unique<content::BrowserTaskEnvironment>()) {}
FieldValueRequestCoordinatorTest::~FieldValueRequestCoordinatorTest() = default;

}  // namespace taffy::field_value_request_coordinator_test

namespace taffy {
namespace {

namespace surface_mojom = browser::field_values::mojom;
using namespace field_value_request_coordinator_test;

TEST_F(FieldValueRequestCoordinatorTest, AFieldWithNoClearanceIsRefusedByName) {
  // A password box. Nothing Taffy holds may ever enter one, and the person is
  // told which field it was rather than left with a sheet that swallowed what
  // they typed.
  sensitivity_ = Sensitivity::kCredential;
  OpenRequest();

  const SupplyOutcome outcome = Supply({"hunter2"});
  ASSERT_TRUE(outcome.answered);
  EXPECT_EQ(surface_mojom::FieldValueSupplyVerdict::kAccepted, outcome.verdict);
  ASSERT_EQ(1u, outcome.refused.size());
  EXPECT_EQ(kNodeId, outcome.refused[0]->field_id);
  EXPECT_EQ(surface_mojom::FieldValueRefusalReason::kFieldMayNotBeFilled,
            outcome.refused[0]->reason);

  // Nothing was held, and the count says so.
  EXPECT_EQ(0u, vault_.HeldCount());
  ASSERT_EQ(1u, reported_.size());
  EXPECT_EQ(kRequestId, reported_[0].first);
  EXPECT_EQ(0u, reported_[0].second);
  EXPECT_EQ(closed_request_ids_, std::vector<std::string>({kRequestId}));
}

TEST_F(FieldValueRequestCoordinatorTest,
       EveryNeverFillableClassIsRefusedTheSameWay) {
  for (const Sensitivity refused :
       {Sensitivity::kCredential, Sensitivity::kUnknownSensitive,
        Sensitivity::kPayment, Sensitivity::kHealth, Sensitivity::kFinancial,
        Sensitivity::kLegal, Sensitivity::kPrivateCommunication,
        Sensitivity::kAdministration}) {
    sensitivity_ = refused;
    OpenRequest();
    const SupplyOutcome outcome = Supply({"typed"});
    ASSERT_EQ(1u, outcome.refused.size()) << static_cast<int>(refused);
    EXPECT_EQ(surface_mojom::FieldValueRefusalReason::kFieldMayNotBeFilled,
              outcome.refused[0]->reason);
    EXPECT_EQ(0u, vault_.HeldCount());
  }
}

TEST_F(FieldValueRequestCoordinatorTest, AMintWithNoActiveGenerationIsHandled) {
  // Mint answers an empty reference rather than an error when there is no
  // generation, so the only way to notice is to look. A coordinator that
  // assumed success would report a count naming values the vault never took.
  vault_.RevokeGeneration(1u);
  OpenRequest();

  const SupplyOutcome outcome = Supply({"1234 5678 9012"});
  ASSERT_TRUE(outcome.answered);
  ASSERT_EQ(1u, outcome.refused.size());
  EXPECT_EQ(kNodeId, outcome.refused[0]->field_id);
  EXPECT_EQ(surface_mojom::FieldValueRefusalReason::kNotHeld,
            outcome.refused[0]->reason);
  EXPECT_EQ(0u, vault_.HeldCount());
  ASSERT_EQ(1u, reported_.size());
  EXPECT_EQ(0u, reported_[0].second);
}

TEST_F(FieldValueRequestCoordinatorTest, ATargetThatWentAwayIsRefusedByName) {
  OpenRequest();
  // The page changed while the person was typing. The re-read that decides is
  // the one taken now, not the one the sheet was drawn from.
  node_gone_ = true;

  const SupplyOutcome outcome = Supply({"1234 5678 9012"});
  ASSERT_EQ(1u, outcome.refused.size());
  EXPECT_EQ(surface_mojom::FieldValueRefusalReason::kFieldGone,
            outcome.refused[0]->reason);
  EXPECT_EQ(0u, vault_.HeldCount());
}

TEST_F(FieldValueRequestCoordinatorTest, TheCountIsTheNumberOfSuccessfulMints) {
  OpenRequest();
  ASSERT_EQ(2, resolves_);

  const SupplyOutcome outcome = Supply({"1234 5678 9012"});
  ASSERT_TRUE(outcome.answered);
  EXPECT_EQ(surface_mojom::FieldValueSupplyVerdict::kAccepted, outcome.verdict);
  EXPECT_TRUE(outcome.refused.empty());
  // The root, its candidate row, and the answer-time classification are three
  // independent reads. The last one is the class that authorizes the mint.
  EXPECT_EQ(3, resolves_);

  ASSERT_EQ(1u, reported_.size());
  EXPECT_EQ(1u, reported_[0].second);
  EXPECT_EQ(1u, vault_.HeldCount());

  // And it is held under the name the task engine derives, which is the whole
  // reason a count is enough to say.
  const ValueReference derived{DerivedValueReference(kRequestId, 0u)};
  EXPECT_EQ("turn-4-values-1-value-0", derived.value);
  EXPECT_TRUE(vault_.Holds(derived));
  std::string out;
  EXPECT_EQ(ValueResolution::kResolved,
            vault_.Resolve(derived, TaskId{kTaskId}, Sensitivity::kIdentity,
                           base::TimeTicks::Now(), out));
  EXPECT_EQ("1234 5678 9012", out);
}

TEST_F(FieldValueRequestCoordinatorTest, ASecondAnswerToOneRequestIsRefused) {
  OpenRequest();
  ASSERT_EQ(surface_mojom::FieldValueSupplyVerdict::kAccepted,
            Supply({"1234 5678 9012"}).verdict);

  // The request is closed by the answer, so the second one names nothing. A
  // value that could be presented twice is a value rather than a reference.
  const SupplyOutcome second = Supply({"9999"});
  ASSERT_TRUE(second.answered);
  EXPECT_EQ(surface_mojom::FieldValueSupplyVerdict::kUnknownRequest,
            second.verdict);
  EXPECT_TRUE(second.refused.empty());
  // One report, one held value: the second answer minted nothing.
  EXPECT_EQ(1u, reported_.size());
  EXPECT_EQ(1u, vault_.HeldCount());
}

TEST_F(FieldValueRequestCoordinatorTest, MoreAnswersThanRowsIsRefusedWhole) {
  OpenRequest();
  const SupplyOutcome outcome = Supply({"one", "two"});
  ASSERT_TRUE(outcome.answered);
  EXPECT_EQ(surface_mojom::FieldValueSupplyVerdict::kMalformed,
            outcome.verdict);
  EXPECT_EQ(0u, vault_.HeldCount());
  // Refused whole rather than truncated, and the request is still open: the
  // person can answer it properly.
  EXPECT_TRUE(reported_.empty());
  EXPECT_EQ(1u, coordinator_->open_request_count());
}

TEST_F(FieldValueRequestCoordinatorTest, FewerAnswersThanRowsIsRefusedWhole) {
  ResolvedNodeFacts form;
  form.node_id = SemanticNodeId{kNodeId};
  form.observed_at_revision = 12u;
  form.form_field_node_ids = {SemanticNodeId{"field-a"},
                              SemanticNodeId{"field-b"}};
  node_facts_[kNodeId] = form;
  node_facts_["field-a"] = EditableField("field-a", Sensitivity::kPersonal);
  node_facts_["field-b"] = EditableField("field-b", Sensitivity::kAccount);
  OpenRequest();

  const SupplyOutcome outcome = Supply({"only-one"});
  EXPECT_EQ(surface_mojom::FieldValueSupplyVerdict::kMalformed,
            outcome.verdict);
  EXPECT_EQ(0u, vault_.HeldCount());
  EXPECT_EQ(1u, coordinator_->open_request_count());
  EXPECT_EQ(0u, coordinator_->preapproval_count_for_testing());
}

TEST_F(FieldValueRequestCoordinatorTest, DismissingIsAnAnswerOfNothing) {
  OpenRequest();
  coordinator_->Dismiss(kRequestId);
  ASSERT_EQ(1u, reported_.size());
  EXPECT_EQ(0u, reported_[0].second);
  EXPECT_EQ(0u, coordinator_->open_request_count());
  // And a supply afterwards names nothing.
  EXPECT_EQ(surface_mojom::FieldValueSupplyVerdict::kUnknownRequest,
            Supply({"late"}).verdict);
  EXPECT_EQ(closed_request_ids_, std::vector<std::string>({kRequestId}));
}

TEST_F(FieldValueRequestCoordinatorTest, AFormWithNothingToTypeIntoIsNotAnAsk) {
  // The rule the sheet is built from: a row a person types into that cannot
  // receive what they typed wastes their time, so there is no sheet at all.
  ResolvedNodeFacts read_only = EditableField(Sensitivity::kPersonal);
  read_only.asserted_states = {NodeState::kVisible, NodeState::kReadOnly};
  EXPECT_FALSE(FieldNeedsAPerson(read_only));

  ResolvedNodeFacts disabled = EditableField(Sensitivity::kPersonal);
  disabled.asserted_states = {NodeState::kEditable, NodeState::kDisabled};
  EXPECT_FALSE(FieldNeedsAPerson(disabled));

  ResolvedNodeFacts button = EditableField(Sensitivity::kPersonal);
  button.available_actions = {ActionType::kActivate};
  EXPECT_FALSE(FieldNeedsAPerson(button));

  EXPECT_TRUE(FieldNeedsAPerson(EditableField(Sensitivity::kPersonal)));
}

TEST_F(FieldValueRequestCoordinatorTest,
       ACompleteBoundedFormProducesOneRowPerWritableChild) {
  ResolvedNodeFacts form;
  form.node_id = SemanticNodeId{kNodeId};
  form.observed_at_revision = 12u;
  form.form_field_node_ids = {SemanticNodeId{"field-a"},
                              SemanticNodeId{"field-b"}};
  node_facts_[kNodeId] = form;
  node_facts_["field-a"] = EditableField("field-a", Sensitivity::kPersonal);
  node_facts_["field-b"] = EditableField("field-b", Sensitivity::kAccount);

  OpenRequest();
  task_environment_->RunUntilIdle();
  ASSERT_EQ(1u, client_.opened.size());
  ASSERT_EQ(2u, client_.opened[0]->fields.size());
  EXPECT_EQ("field-a", client_.opened[0]->fields[0]->field_id);
  EXPECT_EQ("field-b", client_.opened[0]->fields[1]->field_id);

  const SupplyOutcome outcome = Supply({"Alice", "alice@example.test"});
  EXPECT_EQ(surface_mojom::FieldValueSupplyVerdict::kAccepted, outcome.verdict);
  EXPECT_EQ(2u, vault_.HeldCount());
  ASSERT_EQ(1u, reported_.size());
  EXPECT_EQ(2u, reported_[0].second);
}

TEST_F(FieldValueRequestCoordinatorTest,
       ARefusalClosesThePrefixSoTheCountCannotNameAHole) {
  ResolvedNodeFacts form;
  form.node_id = SemanticNodeId{kNodeId};
  form.observed_at_revision = 12u;
  form.form_field_node_ids = {SemanticNodeId{"field-a"},
                              SemanticNodeId{"field-b"}};
  node_facts_[kNodeId] = form;
  node_facts_["field-a"] = EditableField("field-a", Sensitivity::kCredential);
  node_facts_["field-b"] = EditableField("field-b", Sensitivity::kIdentity);
  OpenRequest();

  const SupplyOutcome outcome = Supply({"secret", "1234 5678 9012"});
  ASSERT_EQ(2u, outcome.refused.size());
  EXPECT_EQ("field-a", outcome.refused[0]->field_id);
  EXPECT_EQ(surface_mojom::FieldValueRefusalReason::kFieldMayNotBeFilled,
            outcome.refused[0]->reason);
  EXPECT_EQ("field-b", outcome.refused[1]->field_id);
  EXPECT_EQ(surface_mojom::FieldValueRefusalReason::kNotHeld,
            outcome.refused[1]->reason);
  EXPECT_EQ(0u, vault_.HeldCount());
  ASSERT_EQ(1u, reported_.size());
  EXPECT_EQ(0u, reported_[0].second);
  EXPECT_FALSE(
      vault_.Holds(ValueReference{DerivedValueReference(kRequestId, 1u)}));
}

TEST_F(FieldValueRequestCoordinatorTest,
       ARefusedSequenceRollsBackEveryEarlierMint) {
  ResolvedNodeFacts form;
  form.node_id = SemanticNodeId{kNodeId};
  form.observed_at_revision = 12u;
  form.form_field_node_ids = {SemanticNodeId{"field-a"},
                              SemanticNodeId{"field-b"},
                              SemanticNodeId{"field-c"}};
  node_facts_[kNodeId] = form;
  node_facts_["field-a"] = EditableField("field-a", Sensitivity::kIdentity);
  node_facts_["field-b"] = EditableField("field-b", Sensitivity::kCredential);
  node_facts_["field-c"] = EditableField("field-c", Sensitivity::kIdentity);
  OpenRequest();

  const SupplyOutcome outcome = Supply({"first", "never", "must-not-be-held"});
  ASSERT_EQ(2u, outcome.refused.size());
  EXPECT_EQ("field-b", outcome.refused[0]->field_id);
  EXPECT_EQ("field-c", outcome.refused[1]->field_id);
  ASSERT_EQ(1u, reported_.size());
  // A partial mint is neither exposed to the core nor retained in the vault.
  // The person approved the complete sequence, so a failed suffix invalidates
  // and scrubs its already-minted prefix too.
  EXPECT_EQ(0u, reported_[0].second);
  EXPECT_EQ(0u, coordinator_->preapproval_count_for_testing());
  EXPECT_FALSE(
      vault_.Holds(ValueReference{DerivedValueReference(kRequestId, 0u)}));
  EXPECT_FALSE(
      vault_.Holds(ValueReference{DerivedValueReference(kRequestId, 1u)}));
  EXPECT_FALSE(
      vault_.Holds(ValueReference{DerivedValueReference(kRequestId, 2u)}));
}

TEST_F(FieldValueRequestCoordinatorTest,
       AnswerTimeResolveMustReturnTheExactWritableNode) {
  OpenRequest();
  node_facts_[kNodeId] =
      EditableField("different-node", Sensitivity::kIdentity);
  SupplyOutcome outcome = Supply({"must-not-be-held"});
  ASSERT_EQ(1u, outcome.refused.size());
  EXPECT_EQ(surface_mojom::FieldValueRefusalReason::kFieldGone,
            outcome.refused[0]->reason);
  EXPECT_EQ(0u, vault_.HeldCount());

  constexpr char kSecondRequest[] = "turn-5-values-1";
  node_facts_[kNodeId] = EditableField(Sensitivity::kIdentity);
  coordinator_->OnCoreFieldValueRequest(kSecondRequest, kTaskId, kTabId,
                                        kNodeId);
  ResolvedNodeFacts read_only = EditableField(Sensitivity::kIdentity);
  read_only.asserted_states = {NodeState::kVisible, NodeState::kReadOnly};
  node_facts_[kNodeId] = std::move(read_only);
  SupplyOutcome second;
  coordinator_->Supply(
      kSecondRequest, {"also-must-not-be-held"},
      base::BindLambdaForTesting(
          [&second](surface_mojom::FieldValueSupplyVerdict verdict,
                    std::vector<surface_mojom::FieldValueRefusalPtr> refused) {
            second.answered = true;
            second.verdict = verdict;
            second.refused = std::move(refused);
          }));
  ASSERT_EQ(1u, second.refused.size());
  EXPECT_EQ(surface_mojom::FieldValueRefusalReason::kFieldGone,
            second.refused[0]->reason);
  EXPECT_EQ(0u, vault_.HeldCount());
}

TEST_F(FieldValueRequestCoordinatorTest,
       AFormLargerThanTheVaultLimitFallsThroughWhole) {
  ResolvedNodeFacts form;
  form.node_id = SemanticNodeId{kNodeId};
  form.observed_at_revision = 12u;
  for (uint32_t index = 0; index < 9u; ++index) {
    form.form_field_node_ids.emplace_back("field-" + std::to_string(index));
  }
  node_facts_[kNodeId] = std::move(form);

  OpenRequest();
  task_environment_->RunUntilIdle();
  EXPECT_TRUE(client_.opened.empty());
  EXPECT_EQ(0u, coordinator_->open_request_count());
  ASSERT_EQ(1u, reported_.size());
  EXPECT_EQ(0u, reported_[0].second);
}

TEST_F(FieldValueRequestCoordinatorTest,
       OneTimeCodeIsProjectedAndHeldForOneExactUse) {
  sensitivity_ = Sensitivity::kOneTimeCode;
  challenge_kind_ = ChallengeKind::kOneTimeCode;
  OpenRequest();
  task_environment_->RunUntilIdle();

  ASSERT_EQ(1u, client_.opened.size());
  ASSERT_EQ(1u, client_.opened[0]->fields.size());
  EXPECT_EQ(surface_mojom::FieldChallengeKind::kOneTimeCode,
            client_.opened[0]->fields[0]->challenge);
  EXPECT_TRUE(client_.opened[0]->fields[0]->masked);
  EXPECT_FALSE(client_.opened[0]->fields[0]->challenge_image.has_value());

  EXPECT_EQ(surface_mojom::FieldValueSupplyVerdict::kAccepted,
            Supply({"482901"}).verdict);
  const ValueReference reference{DerivedValueReference(kRequestId, 0u)};
  std::string value;
  EXPECT_EQ(
      ValueResolution::kResolved,
      vault_.Resolve(reference, TaskId{kTaskId}, Sensitivity::kOneTimeCode,
                     base::TimeTicks::Now(), value));
  EXPECT_EQ("482901", value);
  value.clear();
  EXPECT_EQ(
      ValueResolution::kUnknownReference,
      vault_.Resolve(reference, TaskId{kTaskId}, Sensitivity::kOneTimeCode,
                     base::TimeTicks::Now(), value));
}

TEST_F(FieldValueRequestCoordinatorTest,
       ClosingTheGenerationTellsTheCoreNothing) {
  OpenRequest();
  ASSERT_EQ(1u, coordinator_->open_request_count());

  coordinator_->CloseAllRequests();
  EXPECT_EQ(0u, coordinator_->open_request_count());
  // Deliberately silent: the generation these sheets belonged to is going, so
  // there is no core left to tell and nothing a count would mean.
  EXPECT_TRUE(reported_.empty());
  EXPECT_EQ(closed_request_ids_, std::vector<std::string>({kRequestId}));
}

TEST_F(FieldValueRequestCoordinatorTest, ARequestForADeadTaskIsClosed) {
  OpenRequest();
  coordinator_->CloseRequestsForTask("some-other-task");
  EXPECT_EQ(1u, coordinator_->open_request_count());
  coordinator_->CloseRequestsForTask(kTaskId);
  EXPECT_EQ(0u, coordinator_->open_request_count());
  EXPECT_TRUE(reported_.empty());
  EXPECT_EQ(closed_request_ids_, std::vector<std::string>({kRequestId}));
}

}  // namespace
}  // namespace taffy
