// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_backup.h"
#include "taffy/services/core/service_bridge_backup_ffi.rs.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace bridge = core_bridge;
namespace internal = core_service_internal;
namespace mojom = core_service::mojom;

constexpr uint64_t kGeneration = 41u;
constexpr uint64_t kRevision = 7u;
constexpr uint64_t kDeadline = 9'001u;

std::vector<uint8_t> Digest(uint8_t value, size_t width = 32u) {
  return std::vector<uint8_t>(width, value);
}

mojom::OperationEnvelopePtr Operation(const char* id) {
  return mojom::OperationEnvelope::New(id, kGeneration, kRevision, kDeadline,
                                       std::string(id) + "-once");
}

mojom::BackupRestoreBindingPtr Binding() {
  auto binding = mojom::BackupRestoreBinding::New();
  binding->planning_operation = Operation("plan");
  binding->owner_profile_id = "owner-profile";
  binding->target = mojom::BackupRestoreTarget::New(
      mojom::BackupRestoreTargetKind::kNewRegularProfile, "target-profile");
  binding->backup_id = "backup-1";
  binding->snapshot_sha256 = Digest(0x11u);
  binding->confirmation_sha256 = Digest(0x22u);
  return binding;
}

bridge::BridgeBackupOperation BridgeOperation(const char* id) {
  bridge::BridgeBackupOperation operation;
  operation.operation_id = id;
  operation.service_generation = kGeneration;
  operation.task_revision = kRevision;
  operation.deadline_monotonic_ms = kDeadline;
  operation.idempotency_key = std::string(id) + "-once";
  return operation;
}

bridge::BridgeBackupRestoreBinding BridgeBinding() {
  bridge::BridgeBackupRestoreBinding binding;
  binding.planning_operation = BridgeOperation("plan");
  binding.owner_profile_id = "owner-profile";
  binding.target_kind =
      static_cast<uint8_t>(mojom::BackupRestoreTargetKind::kNewRegularProfile);
  binding.target_profile_id = "target-profile";
  binding.backup_id = "backup-1";
  binding.snapshot_sha256.fill(0x11u);
  binding.confirmation_sha256.fill(0x22u);
  return binding;
}

void ExpectOperation(const mojom::OperationEnvelope& operation,
                     const char* id) {
  EXPECT_EQ(id, operation.operation_id);
  EXPECT_EQ(kGeneration, operation.service_generation);
  EXPECT_EQ(kRevision, operation.task_revision);
  EXPECT_EQ(kDeadline, operation.deadline_monotonic_ms);
  EXPECT_EQ(std::string(id) + "-once", operation.idempotency_key);
}

void ExpectBinding(const mojom::BackupRestoreBinding& binding) {
  ASSERT_TRUE(binding.planning_operation);
  ExpectOperation(*binding.planning_operation, "plan");
  EXPECT_EQ("owner-profile", binding.owner_profile_id);
  ASSERT_TRUE(binding.target);
  EXPECT_EQ(mojom::BackupRestoreTargetKind::kNewRegularProfile,
            binding.target->kind);
  EXPECT_EQ("target-profile", binding.target->profile_id);
  EXPECT_EQ("backup-1", binding.backup_id);
  EXPECT_EQ(Digest(0x11u), binding.snapshot_sha256);
  EXPECT_EQ(Digest(0x22u), binding.confirmation_sha256);
}

mojom::BackupRestorePlanConfirmationRequestPtr ConfirmationRequest() {
  return mojom::BackupRestorePlanConfirmationRequest::New(
      Operation("confirm"), Binding(), Digest(0x22u));
}

mojom::BackupRestoreStageVerificationRequestPtr VerificationRequest() {
  return mojom::BackupRestoreStageVerificationRequest::New(
      Operation("verify"),
      mojom::BackupRestoreStageAuthorization::New(Binding(),
                                                  Operation("stage-decision")),
      Digest(0x33u), std::vector<mojom::SkillRecordPtr>{});
}

mojom::BackupRestoreCommitOutcomeReportPtr CommitReport() {
  return mojom::BackupRestoreCommitOutcomeReport::New(
      Operation("commit-report"),
      mojom::BackupRestoreCommitAuthorization::New(
          Binding(), Operation("commit-decision")),
      mojom::BackupRestoreCommitOutcome::kCommitted);
}

mojom::BackupRestoreResolutionRequestPtr ResolutionRequest() {
  return mojom::BackupRestoreResolutionRequest::New(
      Operation("resolve"), Binding(),
      mojom::BackupRestoreResolutionChoice::kAcceptCandidate);
}

mojom::BackupRestoreResolutionOutcomeReportPtr ResolutionReport() {
  return mojom::BackupRestoreResolutionOutcomeReport::New(
      Operation("resolution-report"),
      mojom::BackupRestoreResolutionAuthorization::New(
          Binding(), Operation("resolution-decision"),
          mojom::BackupRestoreResolutionChoice::kAcceptCandidate),
      mojom::BackupRestoreResolutionOutcome::kCompleted);
}

mojom::BackupRestoreCancellationRequestPtr CancellationRequest() {
  return mojom::BackupRestoreCancellationRequest::New(Operation("cancel"),
                                                      Binding());
}

template <typename Result>
Result AuthorizationResult(mojom::BackupRestoreProtocolStatus status,
                           bool has_authorization,
                           const char* operation_id) {
  Result result;
  result.operation = BridgeOperation(operation_id);
  result.status = static_cast<uint8_t>(status);
  result.has_authorization = has_authorization;
  if (has_authorization) {
    result.authorization.binding = BridgeBinding();
    result.authorization.decision_operation = BridgeOperation(operation_id);
  }
  return result;
}

bridge::BridgeBackupRestoreStageAuthorizationResult StageResult(
    mojom::BackupRestoreProtocolStatus status,
    bool has_authorization) {
  return AuthorizationResult<
      bridge::BridgeBackupRestoreStageAuthorizationResult>(
      status, has_authorization, "stage-decision");
}

bridge::BridgeBackupRestoreCommitAuthorizationResult CommitResult(
    mojom::BackupRestoreProtocolStatus status,
    bool has_authorization) {
  return AuthorizationResult<
      bridge::BridgeBackupRestoreCommitAuthorizationResult>(
      status, has_authorization, "commit-decision");
}

bridge::BridgeBackupRestoreResolutionAuthorizationResult ResolutionResult(
    mojom::BackupRestoreProtocolStatus status,
    bool has_authorization) {
  auto result = AuthorizationResult<
      bridge::BridgeBackupRestoreResolutionAuthorizationResult>(
      status, has_authorization, "resolution-decision");
  if (has_authorization) {
    result.authorization.choice = static_cast<uint8_t>(
        mojom::BackupRestoreResolutionChoice::kAcceptCandidate);
  }
  return result;
}

TEST(RustCoreBackupProtocolTest, ValidInputsPreserveExactAuthorityAndEchoes) {
  auto confirmation = ConfirmationRequest();
  const auto projected_confirmation =
      internal::ToBridgeBackupRestorePlanConfirmationRequest(*confirmation);
  ASSERT_TRUE(projected_confirmation);
  EXPECT_EQ("confirm",
            std::string(projected_confirmation->operation.operation_id));
  EXPECT_EQ(Digest(0x11u),
            std::vector<uint8_t>(
                projected_confirmation->binding.snapshot_sha256.begin(),
                projected_confirmation->binding.snapshot_sha256.end()));
  EXPECT_EQ(Digest(0x22u), std::vector<uint8_t>(
                               projected_confirmation->confirmed_sha256.begin(),
                               projected_confirmation->confirmed_sha256.end()));

  auto verification = VerificationRequest();
  const auto projected_verification =
      internal::ToBridgeBackupRestoreStageVerificationRequest(*verification);
  ASSERT_TRUE(projected_verification);
  EXPECT_EQ("stage-decision",
            std::string(projected_verification->authorization.decision_operation
                            .operation_id));
  EXPECT_EQ(0x33u, projected_verification->staged_snapshot_sha256.front());

  auto commit = CommitReport();
  const auto projected_commit =
      internal::ToBridgeBackupRestoreCommitOutcomeReport(*commit);
  ASSERT_TRUE(projected_commit);
  EXPECT_EQ(static_cast<uint8_t>(mojom::BackupRestoreCommitOutcome::kCommitted),
            projected_commit->outcome);

  auto resolution = ResolutionRequest();
  const auto projected_resolution =
      internal::ToBridgeBackupRestoreResolutionRequest(*resolution);
  ASSERT_TRUE(projected_resolution);
  EXPECT_EQ(static_cast<uint8_t>(
                mojom::BackupRestoreResolutionChoice::kAcceptCandidate),
            projected_resolution->choice);

  auto report = ResolutionReport();
  const auto projected_report =
      internal::ToBridgeBackupRestoreResolutionOutcomeReport(*report);
  ASSERT_TRUE(projected_report);
  EXPECT_EQ(
      static_cast<uint8_t>(mojom::BackupRestoreResolutionOutcome::kCompleted),
      projected_report->outcome);

  auto cancellation = CancellationRequest();
  const auto projected_cancellation =
      internal::ToBridgeBackupRestoreCancellationRequest(*cancellation);
  ASSERT_TRUE(projected_cancellation);
}

TEST(RustCoreBackupProtocolTest,
     EveryInputDigestRequiresExactlyThirtyTwoNonZeroBytes) {
  for (const size_t width : std::array<size_t, 2u>{31u, 33u}) {
    SCOPED_TRACE(width);
    auto confirmation = ConfirmationRequest();
    confirmation->binding->snapshot_sha256 = Digest(0x11u, width);
    EXPECT_FALSE(
        internal::ToBridgeBackupRestorePlanConfirmationRequest(*confirmation));
    confirmation = ConfirmationRequest();
    confirmation->binding->confirmation_sha256 = Digest(0x22u, width);
    EXPECT_FALSE(
        internal::ToBridgeBackupRestorePlanConfirmationRequest(*confirmation));
    confirmation = ConfirmationRequest();
    confirmation->confirmed_sha256 = Digest(0x22u, width);
    EXPECT_FALSE(
        internal::ToBridgeBackupRestorePlanConfirmationRequest(*confirmation));
    auto verification = VerificationRequest();
    verification->staged_snapshot_sha256 = Digest(0x33u, width);
    EXPECT_FALSE(
        internal::ToBridgeBackupRestoreStageVerificationRequest(*verification));
  }
  auto confirmation = ConfirmationRequest();
  confirmation->binding->snapshot_sha256 = Digest(0u);
  EXPECT_FALSE(
      internal::ToBridgeBackupRestorePlanConfirmationRequest(*confirmation));
  confirmation = ConfirmationRequest();
  confirmation->binding->confirmation_sha256 = Digest(0u);
  EXPECT_FALSE(
      internal::ToBridgeBackupRestorePlanConfirmationRequest(*confirmation));
  confirmation = ConfirmationRequest();
  confirmation->confirmed_sha256 = Digest(0u);
  EXPECT_FALSE(
      internal::ToBridgeBackupRestorePlanConfirmationRequest(*confirmation));
  auto verification = VerificationRequest();
  verification->staged_snapshot_sha256 = Digest(0u);
  EXPECT_FALSE(
      internal::ToBridgeBackupRestoreStageVerificationRequest(*verification));
}

TEST(RustCoreBackupProtocolTest, MissingInputBindingOrAuthorizationIsRejected) {
  auto confirmation = ConfirmationRequest();
  confirmation->binding.reset();
  EXPECT_FALSE(
      internal::ToBridgeBackupRestorePlanConfirmationRequest(*confirmation));
  auto verification = VerificationRequest();
  verification->authorization.reset();
  EXPECT_FALSE(
      internal::ToBridgeBackupRestoreStageVerificationRequest(*verification));
  verification = VerificationRequest();
  verification->authorization->binding.reset();
  EXPECT_FALSE(
      internal::ToBridgeBackupRestoreStageVerificationRequest(*verification));
  verification = VerificationRequest();
  verification->authorization->decision_operation.reset();
  EXPECT_FALSE(
      internal::ToBridgeBackupRestoreStageVerificationRequest(*verification));
  auto commit = CommitReport();
  commit->authorization.reset();
  EXPECT_FALSE(internal::ToBridgeBackupRestoreCommitOutcomeReport(*commit));
  commit = CommitReport();
  commit->authorization->binding.reset();
  EXPECT_FALSE(internal::ToBridgeBackupRestoreCommitOutcomeReport(*commit));
  auto resolution = ResolutionRequest();
  resolution->binding.reset();
  EXPECT_FALSE(internal::ToBridgeBackupRestoreResolutionRequest(*resolution));
  auto report = ResolutionReport();
  report->authorization.reset();
  EXPECT_FALSE(internal::ToBridgeBackupRestoreResolutionOutcomeReport(*report));
  report = ResolutionReport();
  report->authorization->binding.reset();
  EXPECT_FALSE(internal::ToBridgeBackupRestoreResolutionOutcomeReport(*report));
  auto cancellation = CancellationRequest();
  cancellation->binding.reset();
  EXPECT_FALSE(
      internal::ToBridgeBackupRestoreCancellationRequest(*cancellation));
}

TEST(RustCoreBackupProtocolTest, UnknownInputChoicesAndOutcomesAreRejected) {
  auto resolution = ResolutionRequest();
  resolution->choice = static_cast<mojom::BackupRestoreResolutionChoice>(0xffu);
  EXPECT_FALSE(internal::ToBridgeBackupRestoreResolutionRequest(*resolution));
  auto commit = CommitReport();
  commit->outcome = static_cast<mojom::BackupRestoreCommitOutcome>(0xffu);
  EXPECT_FALSE(internal::ToBridgeBackupRestoreCommitOutcomeReport(*commit));
  auto report = ResolutionReport();
  report->authorization->choice =
      static_cast<mojom::BackupRestoreResolutionChoice>(0xffu);
  EXPECT_FALSE(internal::ToBridgeBackupRestoreResolutionOutcomeReport(*report));
  report = ResolutionReport();
  report->outcome = static_cast<mojom::BackupRestoreResolutionOutcome>(0xffu);
  EXPECT_FALSE(internal::ToBridgeBackupRestoreResolutionOutcomeReport(*report));
}

TEST(RustCoreBackupProtocolTest,
     SuccessfulResultsRequireAndPreserveExactAuthority) {
  auto stage = internal::ToMojoBackupRestoreStageAuthorizationResult(
      StageResult(mojom::BackupRestoreProtocolStatus::kSucceeded, true));
  ASSERT_TRUE(stage);
  ASSERT_TRUE(stage->operation);
  ASSERT_TRUE(stage->authorization);
  ASSERT_TRUE(stage->authorization->binding);
  ASSERT_TRUE(stage->authorization->decision_operation);
  ExpectOperation(*stage->operation, "stage-decision");
  ExpectBinding(*stage->authorization->binding);
  ExpectOperation(*stage->authorization->decision_operation, "stage-decision");

  auto commit = internal::ToMojoBackupRestoreCommitAuthorizationResult(
      CommitResult(mojom::BackupRestoreProtocolStatus::kSucceeded, true));
  ASSERT_TRUE(commit);
  ASSERT_TRUE(commit->authorization);

  auto resolution = internal::ToMojoBackupRestoreResolutionAuthorizationResult(
      ResolutionResult(mojom::BackupRestoreProtocolStatus::kSucceeded, true));
  ASSERT_TRUE(resolution);
  ASSERT_TRUE(resolution->authorization);
  EXPECT_EQ(mojom::BackupRestoreResolutionChoice::kAcceptCandidate,
            resolution->authorization->choice);
}

TEST(RustCoreBackupProtocolTest,
     SuccessWithoutAuthorityAndMismatchedAuthorityAreRejected) {
  EXPECT_FALSE(internal::ToMojoBackupRestoreStageAuthorizationResult(
      StageResult(mojom::BackupRestoreProtocolStatus::kSucceeded, false)));
  EXPECT_FALSE(internal::ToMojoBackupRestoreCommitAuthorizationResult(
      CommitResult(mojom::BackupRestoreProtocolStatus::kSucceeded, false)));
  EXPECT_FALSE(internal::ToMojoBackupRestoreResolutionAuthorizationResult(
      ResolutionResult(mojom::BackupRestoreProtocolStatus::kSucceeded, false)));

  auto stage =
      StageResult(mojom::BackupRestoreProtocolStatus::kSucceeded, true);
  stage.authorization.decision_operation.task_revision++;
  EXPECT_FALSE(
      internal::ToMojoBackupRestoreStageAuthorizationResult(std::move(stage)));
  auto commit =
      CommitResult(mojom::BackupRestoreProtocolStatus::kSucceeded, true);
  commit.authorization.decision_operation.service_generation++;
  EXPECT_FALSE(internal::ToMojoBackupRestoreCommitAuthorizationResult(
      std::move(commit)));
  auto resolution =
      ResolutionResult(mojom::BackupRestoreProtocolStatus::kSucceeded, true);
  resolution.authorization.decision_operation.idempotency_key = "different";
  EXPECT_FALSE(internal::ToMojoBackupRestoreResolutionAuthorizationResult(
      std::move(resolution)));
}

TEST(RustCoreBackupProtocolTest,
     FailureResultsHaveNoAuthorityAndCannotSmuggleOne) {
  auto stage = internal::ToMojoBackupRestoreStageAuthorizationResult(
      StageResult(mojom::BackupRestoreProtocolStatus::kUnavailable, false));
  ASSERT_TRUE(stage);
  EXPECT_FALSE(stage->authorization);
  auto commit = internal::ToMojoBackupRestoreCommitAuthorizationResult(
      CommitResult(mojom::BackupRestoreProtocolStatus::kUnavailable, false));
  ASSERT_TRUE(commit);
  EXPECT_FALSE(commit->authorization);
  auto resolution = internal::ToMojoBackupRestoreResolutionAuthorizationResult(
      ResolutionResult(mojom::BackupRestoreProtocolStatus::kUnavailable,
                       false));
  ASSERT_TRUE(resolution);
  EXPECT_FALSE(resolution->authorization);

  EXPECT_FALSE(internal::ToMojoBackupRestoreStageAuthorizationResult(
      StageResult(mojom::BackupRestoreProtocolStatus::kUnavailable, true)));
  EXPECT_FALSE(internal::ToMojoBackupRestoreCommitAuthorizationResult(
      CommitResult(mojom::BackupRestoreProtocolStatus::kUnavailable, true)));
  EXPECT_FALSE(internal::ToMojoBackupRestoreResolutionAuthorizationResult(
      ResolutionResult(mojom::BackupRestoreProtocolStatus::kUnavailable,
                       true)));
}

TEST(RustCoreBackupProtocolTest, UnknownOutputStatusAndChoiceAreRejected) {
  auto stage =
      StageResult(mojom::BackupRestoreProtocolStatus::kSucceeded, true);
  stage.status = 0xffu;
  EXPECT_FALSE(
      internal::ToMojoBackupRestoreStageAuthorizationResult(std::move(stage)));
  auto commit =
      CommitResult(mojom::BackupRestoreProtocolStatus::kSucceeded, true);
  commit.status = 0xffu;
  EXPECT_FALSE(internal::ToMojoBackupRestoreCommitAuthorizationResult(
      std::move(commit)));
  auto resolution =
      ResolutionResult(mojom::BackupRestoreProtocolStatus::kSucceeded, true);
  resolution.status = 0xffu;
  EXPECT_FALSE(internal::ToMojoBackupRestoreResolutionAuthorizationResult(
      std::move(resolution)));
  resolution =
      ResolutionResult(mojom::BackupRestoreProtocolStatus::kSucceeded, true);
  resolution.authorization.choice = 0xffu;
  EXPECT_FALSE(internal::ToMojoBackupRestoreResolutionAuthorizationResult(
      std::move(resolution)));
}

TEST(RustCoreBackupProtocolTest, ProtocolResultEchoesOnlyKnownStatus) {
  bridge::BridgeBackupRestoreProtocolResult input;
  input.operation = BridgeOperation("protocol-result");
  input.status =
      static_cast<uint8_t>(mojom::BackupRestoreProtocolStatus::kWrongPhase);
  auto output = internal::ToMojoBackupRestoreProtocolResult(std::move(input));
  ASSERT_TRUE(output);
  ASSERT_TRUE(output->operation);
  ExpectOperation(*output->operation, "protocol-result");
  EXPECT_EQ(mojom::BackupRestoreProtocolStatus::kWrongPhase, output->status);

  input.operation = BridgeOperation("unknown-status");
  input.status = 0xffu;
  EXPECT_FALSE(internal::ToMojoBackupRestoreProtocolResult(std::move(input)));
}

}  // namespace
}  // namespace taffy
