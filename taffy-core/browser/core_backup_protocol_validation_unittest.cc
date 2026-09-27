// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/core_backup_protocol_validation.h"

#include <cstdint>
#include <utility>
#include <vector>

#include "taffy/browser/core_backup_planning_validation.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
constexpr uint64_t kGeneration = 7u;
constexpr uint64_t kNow = 100u;

mojom::OperationEnvelopePtr Operation(uint64_t deadline = kNow + 1u) {
  return mojom::OperationEnvelope::New("decision", kGeneration, 0u, deadline,
                                       "decision-once");
}

std::vector<uint8_t> Digest() {
  std::vector<uint8_t> digest(32u, 0u);
  digest.back() = 1u;
  return digest;
}

mojom::BackupRestoreBindingPtr Binding() {
  // The retained planning RPC has expired; it remains immutable identity.
  auto planning = Operation(1u);
  planning->operation_id = "plan";
  planning->idempotency_key = "plan-once";
  return mojom::BackupRestoreBinding::New(
      std::move(planning), "source-profile",
      mojom::BackupRestoreTarget::New(
          mojom::BackupRestoreTargetKind::kNewRegularProfile, "target-profile"),
      "backup", Digest(), Digest());
}

template <typename Request, typename Validator>
void ExpectFreshRpcRequired(Request request, Validator validate) {
  EXPECT_TRUE(validate(*request, kGeneration, kNow));
  auto changed = request.Clone();
  changed->operation.reset();
  EXPECT_FALSE(validate(*changed, kGeneration, kNow));
  changed = request.Clone();
  changed->operation->deadline_monotonic_ms = kNow;
  EXPECT_FALSE(validate(*changed, kGeneration, kNow));
  changed = request.Clone();
  changed->operation->service_generation++;
  EXPECT_FALSE(validate(*changed, kGeneration, kNow));
  changed = request.Clone();
  changed->operation->task_revision = 1u;
  EXPECT_FALSE(validate(*changed, kGeneration, kNow));
  EXPECT_FALSE(validate(*request, 0u, kNow));
}

TEST(CoreBackupProtocolValidationTest, EachLaterCallNeedsItsOwnLiveOperation) {
  ExpectFreshRpcRequired(mojom::BackupRestorePlanConfirmationRequest::New(
                             Operation(), Binding(), Digest()),
                         IsValidBackupRestorePlanConfirmationRequest);
  ExpectFreshRpcRequired(
      mojom::BackupRestoreStageVerificationRequest::New(
          Operation(),
          mojom::BackupRestoreStageAuthorization::New(Binding(), Operation(2u)),
          Digest(), std::vector<mojom::SkillRecordPtr>{}),
      IsValidBackupRestoreStageVerificationRequest);
  ExpectFreshRpcRequired(mojom::BackupRestoreCommitOutcomeReport::New(
                             Operation(),
                             mojom::BackupRestoreCommitAuthorization::New(
                                 Binding(), Operation(2u)),
                             mojom::BackupRestoreCommitOutcome::kCommitted),
                         IsValidBackupRestoreCommitOutcomeReport);
  ExpectFreshRpcRequired(
      mojom::BackupRestoreResolutionRequest::New(
          Operation(), Binding(),
          mojom::BackupRestoreResolutionChoice::kAcceptCandidate),
      IsValidBackupRestoreResolutionRequest);
  ExpectFreshRpcRequired(
      mojom::BackupRestoreResolutionOutcomeReport::New(
          Operation(),
          mojom::BackupRestoreResolutionAuthorization::New(
              Binding(), Operation(2u),
              mojom::BackupRestoreResolutionChoice::kDiscardCandidate),
          mojom::BackupRestoreResolutionOutcome::kCompleted),
      IsValidBackupRestoreResolutionOutcomeReport);
  ExpectFreshRpcRequired(
      mojom::BackupRestoreCancellationRequest::New(Operation(), Binding()),
      IsValidBackupRestoreCancellationRequest);
}

TEST(CoreBackupProtocolValidationTest, BindingIsCompleteAndGenerationBound) {
  EXPECT_TRUE(IsValidBackupRestoreBinding(Binding().get(), kGeneration));
  EXPECT_FALSE(IsValidBackupRestoreBinding(nullptr, kGeneration));
  EXPECT_FALSE(IsValidBackupRestoreBinding(Binding().get(), kGeneration + 1u));
  for (int mutation = 0; mutation < 10; ++mutation) {
    SCOPED_TRACE(mutation);
    auto changed = Binding();
    switch (mutation) {
      case 0:
        changed->planning_operation.reset();
        break;
      case 1:
        changed->target.reset();
        break;
      case 2:
        changed->owner_profile_id.clear();
        break;
      case 3:
        changed->target->profile_id = changed->owner_profile_id;
        break;
      case 4:
        changed->snapshot_sha256.resize(31u);
        break;
      case 5:
        changed->confirmation_sha256.assign(32u, 0u);
        break;
      case 6:
        changed->backup_id = "bad\nidentity";
        break;
      case 7:
        changed->planning_operation->deadline_monotonic_ms = 0u;
        break;
      case 8:
        changed->planning_operation->task_revision = 1u;
        break;
      case 9:
        changed->target->kind =
            static_cast<mojom::BackupRestoreTargetKind>(255);
        break;
    }
    EXPECT_FALSE(IsValidBackupRestoreBinding(changed.get(), kGeneration));
  }
}

TEST(CoreBackupProtocolValidationTest, GenerationZeroIsNeverLive) {
  auto operation = Operation();
  operation->service_generation = 0u;
  EXPECT_FALSE(IsLiveBackupOperation(operation.get(), 0u, kNow));
}

TEST(CoreBackupProtocolValidationTest, ExactBindingIncludesEveryIdentityField) {
  const auto expected = Binding();
  EXPECT_TRUE(IsExactBackupRestoreBinding(expected.get(), expected.get()));
  EXPECT_FALSE(IsExactBackupRestoreBinding(expected.get(), nullptr));
  for (int mutation = 0; mutation < 11; ++mutation) {
    SCOPED_TRACE(mutation);
    auto changed = expected.Clone();
    switch (mutation) {
      case 0:
        changed->planning_operation->operation_id += "other";
        break;
      case 1:
        changed->planning_operation->service_generation++;
        break;
      case 2:
        changed->planning_operation->task_revision++;
        break;
      case 3:
        changed->planning_operation->deadline_monotonic_ms++;
        break;
      case 4:
        changed->planning_operation->idempotency_key += "other";
        break;
      case 5:
        changed->owner_profile_id += "other";
        break;
      case 6:
        changed->target->profile_id += "other";
        break;
      case 7:
        changed->target->kind =
            static_cast<mojom::BackupRestoreTargetKind>(255);
        break;
      case 8:
        changed->backup_id += "other";
        break;
      case 9:
        changed->snapshot_sha256.front() = 2u;
        break;
      case 10:
        changed->confirmation_sha256.front() = 2u;
        break;
    }
    EXPECT_FALSE(IsExactBackupRestoreBinding(expected.get(), changed.get()));
  }
}

template <typename Result, typename Validator>
void ExpectClosedAuthorityShape(Result result, Validator validate) {
  EXPECT_TRUE(validate(*result));
  auto changed = result.Clone();
  changed->authorization.reset();
  EXPECT_FALSE(validate(*changed));
  changed->status = mojom::BackupRestoreProtocolStatus::kUnavailable;
  EXPECT_TRUE(validate(*changed));
  changed->status = static_cast<mojom::BackupRestoreProtocolStatus>(255);
  EXPECT_FALSE(validate(*changed));
  changed = result.Clone();
  changed->status = mojom::BackupRestoreProtocolStatus::kUnavailable;
  EXPECT_FALSE(validate(*changed));
  changed = result.Clone();
  changed->operation->deadline_monotonic_ms++;
  EXPECT_FALSE(validate(*changed));
  changed = result.Clone();
  changed->authorization->decision_operation->idempotency_key += "other";
  EXPECT_FALSE(validate(*changed));
  changed = result.Clone();
  changed->authorization->binding->target->profile_id += "other";
  EXPECT_FALSE(validate(*changed));
}

TEST(CoreBackupProtocolValidationTest, StageReplyHasOnlyExactLiveAuthority) {
  const auto expected = Operation();
  const auto binding = Binding();
  ExpectClosedAuthorityShape(
      mojom::BackupRestoreStageAuthorizationResult::New(
          expected.Clone(), mojom::BackupRestoreProtocolStatus::kSucceeded,
          mojom::BackupRestoreStageAuthorization::New(binding.Clone(),
                                                      expected.Clone())),
      [&](const auto& result) {
        return IsValidBackupRestoreStageAuthorizationResult(*expected, *binding,
                                                            result);
      });
}

TEST(CoreBackupProtocolValidationTest, CommitReplyHasOnlyExactLiveAuthority) {
  const auto expected = Operation();
  const auto binding = Binding();
  ExpectClosedAuthorityShape(
      mojom::BackupRestoreCommitAuthorizationResult::New(
          expected.Clone(), mojom::BackupRestoreProtocolStatus::kSucceeded,
          mojom::BackupRestoreCommitAuthorization::New(binding.Clone(),
                                                       expected.Clone())),
      [&](const auto& result) {
        return IsValidBackupRestoreCommitAuthorizationResult(*expected,
                                                             *binding, result);
      });
}

TEST(CoreBackupProtocolValidationTest,
     ResolutionCannotChangeTheReviewedChoice) {
  const auto expected = Operation();
  const auto binding = Binding();
  const auto choice = mojom::BackupRestoreResolutionChoice::kDiscardCandidate;
  auto result = mojom::BackupRestoreResolutionAuthorizationResult::New(
      expected.Clone(), mojom::BackupRestoreProtocolStatus::kSucceeded,
      mojom::BackupRestoreResolutionAuthorization::New(
          binding.Clone(), expected.Clone(), choice));
  auto validate = [&](const auto& received) {
    return IsValidBackupRestoreResolutionAuthorizationResult(
        *expected, *binding, choice, received);
  };
  ExpectClosedAuthorityShape(result.Clone(), validate);
  result->authorization->choice =
      mojom::BackupRestoreResolutionChoice::kAcceptCandidate;
  EXPECT_FALSE(validate(*result));
}

TEST(CoreBackupProtocolValidationTest,
     CommitMustStartBeforeItsDecisionExpires) {
  auto authorization =
      mojom::BackupRestoreCommitAuthorization::New(Binding(), Operation());
  EXPECT_TRUE(IsLiveBackupRestoreCommitAuthorization(authorization.get(),
                                                     kGeneration, kNow));
  EXPECT_FALSE(IsLiveBackupRestoreCommitAuthorization(authorization.get(),
                                                      kGeneration, kNow + 1u));
  EXPECT_FALSE(IsLiveBackupRestoreCommitAuthorization(authorization.get(),
                                                      kGeneration + 1u, kNow));
  authorization->decision_operation.reset();
  EXPECT_FALSE(IsLiveBackupRestoreCommitAuthorization(authorization.get(),
                                                      kGeneration, kNow));
}

TEST(CoreBackupProtocolValidationTest,
     ResolutionMustStartBeforeItsDecisionExpires) {
  auto authorization = mojom::BackupRestoreResolutionAuthorization::New(
      Binding(), Operation(),
      mojom::BackupRestoreResolutionChoice::kAcceptCandidate);
  EXPECT_TRUE(IsLiveBackupRestoreResolutionAuthorization(authorization.get(),
                                                         kGeneration, kNow));
  EXPECT_FALSE(IsLiveBackupRestoreResolutionAuthorization(
      authorization.get(), kGeneration, kNow + 1u));
  authorization->choice =
      static_cast<mojom::BackupRestoreResolutionChoice>(255);
  EXPECT_FALSE(IsLiveBackupRestoreResolutionAuthorization(authorization.get(),
                                                          kGeneration, kNow));
}

TEST(CoreBackupProtocolValidationTest,
     StagedProcedurePayloadIsBoundedBeforeIpc) {
  auto request = mojom::BackupRestoreStageVerificationRequest::New(
      Operation(),
      mojom::BackupRestoreStageAuthorization::New(Binding(), Operation(2u)),
      Digest(), std::vector<mojom::SkillRecordPtr>{});
  auto skill = mojom::SkillRecord::New("selected-skill", "https://example.test",
                                       mojom::SkillProvenance::kAuthored,
                                       mojom::SkillStatus::kDisabled, 1u,
                                       std::vector<uint8_t>{1u}, 1u, 7u, 9u);
  request->skills.push_back(skill.Clone());
  EXPECT_TRUE(IsValidBackupRestoreStageVerificationRequest(*request,
                                                           kGeneration, kNow));
  request->skills[0]->definition.resize(mojom::kMaxSkillDefinitionBytes + 1u);
  EXPECT_FALSE(IsValidBackupRestoreStageVerificationRequest(*request,
                                                            kGeneration, kNow));
  request->skills[0] = skill.Clone();
  request->skills[0]->status = static_cast<mojom::SkillStatus>(256u);
  EXPECT_FALSE(IsValidBackupRestoreStageVerificationRequest(*request,
                                                            kGeneration, kNow));
  request->skills[0].reset();
  EXPECT_FALSE(IsValidBackupRestoreStageVerificationRequest(*request,
                                                            kGeneration, kNow));
  request->skills.clear();
  for (size_t i = 0u; i <= mojom::kMaxSkillsPerProfile; ++i) {
    request->skills.push_back(skill.Clone());
  }
  EXPECT_FALSE(IsValidBackupRestoreStageVerificationRequest(*request,
                                                            kGeneration, kNow));
}

}  // namespace
}  // namespace taffy
