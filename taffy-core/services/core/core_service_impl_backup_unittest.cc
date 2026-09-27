// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "base/functional/callback.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/bindings/struct_ptr.h"
#include "taffy/services/core/core_service_impl.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {

class CoreServiceImplBackupTestPeer {
 public:
  static void SetAdmissionState(CoreServiceImpl& service,
                                bool ready,
                                bool shutdown,
                                uint64_t generation) {
    service.ready_ = ready;
    service.shutdown_started_ = shutdown;
    service.generation_ = generation;
  }

  template <typename Result>
  static void CompleteReply(
      CoreServiceImpl& service,
      core_service::mojom::OperationEnvelopePtr operation,
      base::OnceCallback<void(mojo::StructPtr<Result>)> callback,
      mojo::StructPtr<Result> result) {
    service.OnBackupProtocolReply<Result>(
        std::move(operation), std::move(callback), std::move(result));
  }

  template <typename Result>
  static void CompletePlanningReply(
      CoreServiceImpl& service,
      core_service::mojom::OperationEnvelopePtr operation,
      base::OnceCallback<void(mojo::StructPtr<Result>)> callback,
      mojo::StructPtr<Result> result) {
    service.OnBackupPlanningReply<Result>(
        std::move(operation), std::move(callback), std::move(result));
  }

  static void CompletePlanReply(
      CoreServiceImpl& service,
      core_service::mojom::OperationEnvelopePtr operation,
      core_service::mojom::BackupRestoreTargetKind target_kind,
      base::OnceCallback<
          void(core_service::mojom::BackupRestorePlanResultPtr)> callback,
      core_service::mojom::BackupRestorePlanResultPtr result) {
    service.OnBackupRestorePlanReply(std::move(operation), target_kind,
                                     std::move(callback), std::move(result));
  }
};

namespace {

namespace mojom = core_service::mojom;
constexpr uint64_t kGeneration = 7;

enum class RefusalCase {
  kNotReady,
  kShutdown,
  kWrongGeneration,
  kMissingRequest,
  kMissingOperation,
};

class CoreServiceImplBackupTest : public testing::TestWithParam<RefusalCase> {
 protected:
  CoreServiceImplBackupTest() : impl_(service_.BindNewPipeAndPassReceiver()) {}

  template <typename Request, typename Result>
  void CheckRefused(void (mojom::CoreSession::*method)(
      mojo::StructPtr<Request>,
      base::OnceCallback<void(mojo::StructPtr<Result>)>)) {
    CoreServiceImplBackupTestPeer::SetAdmissionState(
        impl_, GetParam() != RefusalCase::kNotReady,
        GetParam() == RefusalCase::kShutdown, kGeneration);
    auto request = Request::New();
    request->operation = mojom::OperationEnvelope::New(
        "restore-decision", kGeneration, 0, 60'000, "restore-idempotency");
    if (GetParam() == RefusalCase::kWrongGeneration) {
      request->operation->service_generation = kGeneration + 1;
    }
    if (GetParam() == RefusalCase::kMissingOperation) {
      request->operation.reset();
    }
    if (GetParam() == RefusalCase::kMissingRequest) {
      request.reset();
    }
    const auto expected_operation = request && request->operation
                                        ? request->operation.Clone()
                                        : mojom::OperationEnvelope::New();
    base::test::TestFuture<mojo::StructPtr<Result>> future;
    auto& session = static_cast<mojom::CoreSession&>(impl_);
    (session.*method)(std::move(request), future.GetCallback());
    // Every service-boundary refusal is synchronous: it does not queue a call
    // into an absent or shutting-down Rust authority.
    ASSERT_TRUE(future.IsReady());
    const auto& result = future.Get();
    ASSERT_TRUE(result);
    ASSERT_TRUE(result->operation);
    EXPECT_TRUE(result->operation->Equals(*expected_operation));
    EXPECT_EQ(result->status, mojom::BackupRestoreProtocolStatus::kUnavailable);
    if constexpr (requires(Result& value) { value.authorization; }) {
      EXPECT_FALSE(result->authorization);
    }
  }

  base::test::TaskEnvironment task_environment_;
  mojo::Remote<mojom::TaffyCoreService> service_;
  CoreServiceImpl impl_;
};

TEST_P(CoreServiceImplBackupTest,
       RefusalEchoesOperationAndNeverCarriesAuthority) {
  CheckRefused(&mojom::CoreSession::ConfirmBackupRestorePlan);
  CheckRefused(&mojom::CoreSession::ReportBackupRestoreStageVerified);
  CheckRefused(&mojom::CoreSession::ReportBackupRestoreCommitOutcome);
  CheckRefused(&mojom::CoreSession::ChooseBackupRestoreResolution);
  CheckRefused(&mojom::CoreSession::ReportBackupRestoreResolutionOutcome);
  CheckRefused(&mojom::CoreSession::CancelBackupRestoreBeforeCommit);
}

INSTANTIATE_TEST_SUITE_P(Admission,
                         CoreServiceImplBackupTest,
                         testing::Values(RefusalCase::kNotReady,
                                         RefusalCase::kShutdown,
                                         RefusalCase::kWrongGeneration,
                                         RefusalCase::kMissingRequest,
                                         RefusalCase::kMissingOperation));

class CoreServiceImplBackupReplyTest : public testing::Test {
 protected:
  CoreServiceImplBackupReplyTest()
      : impl_(service_.BindNewPipeAndPassReceiver()) {
    CoreServiceImplBackupTestPeer::SetAdmissionState(impl_, true, false,
                                                     kGeneration);
    operation_ = mojom::OperationEnvelope::New(
        "restore-decision", kGeneration, 0, 60'000, "restore-idempotency");
  }

  auto SuccessfulReply() {
    auto result = mojom::BackupRestoreCommitAuthorizationResult::New();
    result->operation = operation_.Clone();
    result->status = mojom::BackupRestoreProtocolStatus::kSucceeded;
    result->authorization = mojom::BackupRestoreCommitAuthorization::New();
    result->authorization->decision_operation = operation_.Clone();
    result->authorization->binding = mojom::BackupRestoreBinding::New();
    result->authorization->binding->backup_id = "opaque-test-binding";
    return result;
  }

  auto Complete(mojom::BackupRestoreCommitAuthorizationResultPtr result) {
    base::test::TestFuture<mojom::BackupRestoreCommitAuthorizationResultPtr>
        future;
    CoreServiceImplBackupTestPeer::CompleteReply(
        impl_, operation_.Clone(), future.GetCallback(), std::move(result));
    EXPECT_TRUE(future.IsReady());
    return future.Take();
  }

  void ExpectWithdrawn(
      const mojom::BackupRestoreCommitAuthorizationResultPtr& result) {
    ASSERT_TRUE(result);
    ASSERT_TRUE(result->operation);
    EXPECT_TRUE(result->operation->Equals(*operation_));
    EXPECT_EQ(result->status, mojom::BackupRestoreProtocolStatus::kUnavailable);
    EXPECT_FALSE(result->authorization);
  }

  base::test::TaskEnvironment task_environment_;
  mojo::Remote<mojom::TaffyCoreService> service_;
  CoreServiceImpl impl_;
  mojom::OperationEnvelopePtr operation_;
};

TEST_F(CoreServiceImplBackupReplyTest, NullConversionIsAClosedRefusal) {
  ExpectWithdrawn(Complete(nullptr));
}

TEST_F(CoreServiceImplBackupReplyTest, CompleteOperationIdentityMustMatch) {
  auto deadline_drift = SuccessfulReply();
  ++deadline_drift->operation->deadline_monotonic_ms;
  ExpectWithdrawn(Complete(std::move(deadline_drift)));
  auto revision_drift = SuccessfulReply();
  ++revision_drift->operation->task_revision;
  ExpectWithdrawn(Complete(std::move(revision_drift)));
  auto missing_operation = SuccessfulReply();
  missing_operation->operation.reset();
  ExpectWithdrawn(Complete(std::move(missing_operation)));
}

TEST_F(CoreServiceImplBackupReplyTest, ShutdownWithdrawsQueuedAuthority) {
  CoreServiceImplBackupTestPeer::SetAdmissionState(impl_, true, true,
                                                   kGeneration);
  ExpectWithdrawn(Complete(SuccessfulReply()));
}

TEST_F(CoreServiceImplBackupReplyTest, LostReadinessWithdrawsQueuedAuthority) {
  CoreServiceImplBackupTestPeer::SetAdmissionState(impl_, false, false,
                                                   kGeneration);
  ExpectWithdrawn(Complete(SuccessfulReply()));
}

TEST_F(CoreServiceImplBackupReplyTest,
       AnotherGenerationCannotReceiveAuthority) {
  CoreServiceImplBackupTestPeer::SetAdmissionState(impl_, true, false,
                                                   kGeneration + 1);
  ExpectWithdrawn(Complete(SuccessfulReply()));
}

TEST_F(CoreServiceImplBackupReplyTest, LiveExactReplyKeepsTheConvertedValue) {
  auto converted = SuccessfulReply();
  auto expected = converted.Clone();
  auto result = Complete(std::move(converted));
  ASSERT_TRUE(result);
  EXPECT_TRUE(result->Equals(*expected));
}

// The three planning entries answer with `BackupPlanningStatus`, which is a
// different closed enum from the protocol half above, so they have their own
// guard and need their own coverage. What is under test is the same claim: a
// RustCore refusal arrives as nullptr, the response is non-nullable, and
// forwarding the nullptr would end the whole utility generation rather than
// this one request.
class CoreServiceImplBackupPlanningReplyTest : public testing::Test {
 protected:
  CoreServiceImplBackupPlanningReplyTest()
      : impl_(service_.BindNewPipeAndPassReceiver()) {
    CoreServiceImplBackupTestPeer::SetAdmissionState(impl_, true, false,
                                                     kGeneration);
    operation_ = mojom::OperationEnvelope::New(
        "restore-planning", kGeneration, 0, 60'000, "planning-idempotency");
  }

  template <typename Result>
  mojo::StructPtr<Result> Complete(mojo::StructPtr<Result> result) {
    base::test::TestFuture<mojo::StructPtr<Result>> future;
    CoreServiceImplBackupTestPeer::CompletePlanningReply<Result>(
        impl_, operation_.Clone(), future.GetCallback(), std::move(result));
    EXPECT_TRUE(future.IsReady());
    return future.Take();
  }

  mojom::BackupRestorePlanResultPtr CompletePlan(
      mojom::BackupRestoreTargetKind target_kind,
      mojom::BackupRestorePlanResultPtr result) {
    base::test::TestFuture<mojom::BackupRestorePlanResultPtr> future;
    CoreServiceImplBackupTestPeer::CompletePlanReply(
        impl_, operation_.Clone(), target_kind, future.GetCallback(),
        std::move(result));
    EXPECT_TRUE(future.IsReady());
    return future.Take();
  }

  template <typename Result>
  void ExpectRefused(const mojo::StructPtr<Result>& result) {
    ASSERT_TRUE(result);
    ASSERT_TRUE(result->operation);
    EXPECT_TRUE(result->operation->Equals(*operation_));
    EXPECT_EQ(result->status, mojom::BackupPlanningStatus::kUnavailable);
  }

  base::test::TaskEnvironment task_environment_;
  mojo::Remote<mojom::TaffyCoreService> service_;
  CoreServiceImpl impl_;
  mojom::OperationEnvelopePtr operation_;
};

TEST_F(CoreServiceImplBackupPlanningReplyTest, NullPrepareIsAClosedRefusal) {
  ExpectRefused<mojom::BackupManifestPrepareResult>(
      Complete(mojom::BackupManifestPrepareResultPtr()));
}

TEST_F(CoreServiceImplBackupPlanningReplyTest, NullInspectIsAClosedRefusal) {
  ExpectRefused<mojom::BackupManifestInspectResult>(
      Complete(mojom::BackupManifestInspectResultPtr()));
}

TEST_F(CoreServiceImplBackupPlanningReplyTest, PlanningOperationMustMatch) {
  auto drifted = mojom::BackupManifestInspectResult::New();
  drifted->operation = operation_.Clone();
  ++drifted->operation->deadline_monotonic_ms;
  drifted->status = mojom::BackupPlanningStatus::kSucceeded;
  ExpectRefused<mojom::BackupManifestInspectResult>(Complete(
      std::move(drifted)));
}

TEST_F(CoreServiceImplBackupPlanningReplyTest, LivePlanningReplyIsKept) {
  auto converted = mojom::BackupManifestPrepareResult::New();
  converted->operation = operation_.Clone();
  converted->status = mojom::BackupPlanningStatus::kSucceeded;
  auto expected = converted.Clone();
  auto result = Complete(std::move(converted));
  ASSERT_TRUE(result);
  EXPECT_TRUE(result->Equals(*expected));
}

// `BackupRestorePlanResult::target` is the one other non-nullable struct
// member in the backup surface, so a refusal that left it null would be as
// unsendable as the nullptr it replaced.
TEST_F(CoreServiceImplBackupPlanningReplyTest, NullPlanKeepsTheRequestedKind) {
  auto result = CompletePlan(mojom::BackupRestoreTargetKind::kExistingRegularProfile,
                             mojom::BackupRestorePlanResultPtr());
  ExpectRefused<mojom::BackupRestorePlanResult>(result);
  ASSERT_TRUE(result->target);
  EXPECT_EQ(result->target->kind,
            mojom::BackupRestoreTargetKind::kExistingRegularProfile);
}

TEST_F(CoreServiceImplBackupPlanningReplyTest, PlanWithoutATargetIsRefused) {
  auto missing_target = mojom::BackupRestorePlanResult::New();
  missing_target->operation = operation_.Clone();
  missing_target->status = mojom::BackupPlanningStatus::kSucceeded;
  auto result = CompletePlan(mojom::BackupRestoreTargetKind::kNewRegularProfile,
                             std::move(missing_target));
  ExpectRefused<mojom::BackupRestorePlanResult>(result);
  ASSERT_TRUE(result->target);
  EXPECT_EQ(result->target->kind,
            mojom::BackupRestoreTargetKind::kNewRegularProfile);
}

}  // namespace
}  // namespace taffy
