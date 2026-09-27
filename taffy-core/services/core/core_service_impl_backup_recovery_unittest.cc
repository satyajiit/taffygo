// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "taffy/services/core/core_service_impl.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {

class CoreServiceImplBackupRecoveryTestPeer {
 public:
  static void SetAdmissionState(CoreServiceImpl& service,
                                bool ready,
                                bool shutdown,
                                uint64_t generation) {
    service.ready_ = ready;
    service.shutdown_started_ = shutdown;
    service.generation_ = generation;
  }

  static void CompleteReply(
      CoreServiceImpl& service,
      core_service::mojom::OperationEnvelopePtr operation,
      CoreServiceImpl::InspectBackupRestoreRecoveryCallback callback,
      core_service::mojom::BackupRestoreRecoveryInspectionResultPtr result) {
    service.OnBackupRecoveryInspectionReply(
        std::move(operation), std::move(callback), std::move(result));
  }
};

namespace {

namespace mojom = core_service::mojom;
using Peer = CoreServiceImplBackupRecoveryTestPeer;
constexpr uint64_t kGeneration = 7u;

mojom::OperationEnvelopePtr Operation() {
  return mojom::OperationEnvelope::New("inspect-recovery", kGeneration, 0u,
                                       60'000u, "inspect-once");
}

enum class AdmissionRefusal {
  kNotReady,
  kShutdown,
  kWrongGeneration,
  kMissingRequest,
  kMissingOperation,
};

class CoreServiceImplBackupRecoveryAdmissionTest
    : public testing::TestWithParam<AdmissionRefusal> {
 protected:
  CoreServiceImplBackupRecoveryAdmissionTest()
      : impl_(remote_.BindNewPipeAndPassReceiver()) {}

  base::test::TaskEnvironment environment_;
  mojo::Remote<mojom::TaffyCoreService> remote_;
  CoreServiceImpl impl_;
};

TEST_P(CoreServiceImplBackupRecoveryAdmissionTest,
       RefusalIsSynchronousAndHasNoClassification) {
  Peer::SetAdmissionState(impl_, GetParam() != AdmissionRefusal::kNotReady,
                          GetParam() == AdmissionRefusal::kShutdown,
                          kGeneration);
  auto request = mojom::BackupRestoreRecoveryInspectionRequest::New();
  request->operation = Operation();
  if (GetParam() == AdmissionRefusal::kWrongGeneration) {
    ++request->operation->service_generation;
  }
  if (GetParam() == AdmissionRefusal::kMissingOperation) {
    request->operation.reset();
  }
  if (GetParam() == AdmissionRefusal::kMissingRequest) {
    request.reset();
  }
  auto expected = request && request->operation
                      ? request->operation.Clone()
                      : mojom::OperationEnvelope::New();
  base::test::TestFuture<mojom::BackupRestoreRecoveryInspectionResultPtr>
      future;
  // The implementation's overrides are private; access control follows the
  // static type, so the admitting call is made through the interface that
  // declares it, as the sibling backup suite does.
  auto& session = static_cast<mojom::CoreSession&>(impl_);
  session.InspectBackupRestoreRecovery(std::move(request),
                                       future.GetCallback());
  ASSERT_TRUE(future.IsReady());
  const auto& result = future.Get();
  ASSERT_TRUE(result && result->operation);
  EXPECT_TRUE(result->operation->Equals(*expected));
  EXPECT_EQ(mojom::BackupRestoreRecoveryInspectionStatus::kUnavailable,
            result->status);
  EXPECT_FALSE(result->classification);
  EXPECT_FALSE(result->failure);
}

INSTANTIATE_TEST_SUITE_P(Admission,
                         CoreServiceImplBackupRecoveryAdmissionTest,
                         testing::Values(AdmissionRefusal::kNotReady,
                                         AdmissionRefusal::kShutdown,
                                         AdmissionRefusal::kWrongGeneration,
                                         AdmissionRefusal::kMissingRequest,
                                         AdmissionRefusal::kMissingOperation));

class CoreServiceImplBackupRecoveryReplyTest : public testing::Test {
 protected:
  CoreServiceImplBackupRecoveryReplyTest()
      : impl_(remote_.BindNewPipeAndPassReceiver()) {
    Peer::SetAdmissionState(impl_, true, false, kGeneration);
  }

  auto SuccessfulReply() {
    return mojom::BackupRestoreRecoveryInspectionResult::New(
        Operation(), mojom::BackupRestoreRecoveryInspectionStatus::kSucceeded,
        mojom::BackupRestoreRecoveryClassification::New(
            mojom::BackupRestoreRecoveryClassificationKind::kReconcileRequired,
            mojom::BackupRestoreRecoveryReconciliation::New(
                "physical-intent",
                mojom::BackupRestorePhysicalIntent::kCommitCandidate)),
        nullptr);
  }

  auto Complete(mojom::BackupRestoreRecoveryInspectionResultPtr result) {
    base::test::TestFuture<mojom::BackupRestoreRecoveryInspectionResultPtr>
        future;
    Peer::CompleteReply(impl_, Operation(), future.GetCallback(),
                        std::move(result));
    EXPECT_TRUE(future.IsReady());
    return future.Take();
  }

  void ExpectWithdrawn(
      const mojom::BackupRestoreRecoveryInspectionResultPtr& result) {
    ASSERT_TRUE(result && result->operation);
    EXPECT_TRUE(result->operation->Equals(*Operation()));
    EXPECT_EQ(mojom::BackupRestoreRecoveryInspectionStatus::kUnavailable,
              result->status);
    EXPECT_FALSE(result->classification);
    EXPECT_FALSE(result->failure);
  }

  base::test::TaskEnvironment environment_;
  mojo::Remote<mojom::TaffyCoreService> remote_;
  CoreServiceImpl impl_;
};

TEST_F(CoreServiceImplBackupRecoveryReplyTest,
       FailedConversionIsClosedRefusal) {
  ExpectWithdrawn(Complete(nullptr));
}

TEST_F(CoreServiceImplBackupRecoveryReplyTest,
       EveryOperationFieldMustMatchTheAdmittedRequest) {
  for (int field = 0; field < 6; ++field) {
    auto result = SuccessfulReply();
    switch (field) {
      case 0:
        result->operation->operation_id = "other";
        break;
      case 1:
        ++result->operation->service_generation;
        break;
      case 2:
        ++result->operation->task_revision;
        break;
      case 3:
        ++result->operation->deadline_monotonic_ms;
        break;
      case 4:
        result->operation->idempotency_key = "other";
        break;
      case 5:
        result->operation.reset();
        break;
    }
    ExpectWithdrawn(Complete(std::move(result)));
  }
}

TEST_F(CoreServiceImplBackupRecoveryReplyTest,
       ShutdownWithdrawsAnAlreadyConvertedClassification) {
  Peer::SetAdmissionState(impl_, true, true, kGeneration);
  ExpectWithdrawn(Complete(SuccessfulReply()));
}

TEST_F(CoreServiceImplBackupRecoveryReplyTest,
       LostReadinessWithdrawsAnAlreadyConvertedClassification) {
  Peer::SetAdmissionState(impl_, false, false, kGeneration);
  ExpectWithdrawn(Complete(SuccessfulReply()));
}

TEST_F(CoreServiceImplBackupRecoveryReplyTest,
       NewGenerationCannotReceiveAnOldInspectionReply) {
  Peer::SetAdmissionState(impl_, true, false, kGeneration + 1u);
  ExpectWithdrawn(Complete(SuccessfulReply()));
}

TEST_F(CoreServiceImplBackupRecoveryReplyTest,
       ExactLiveClassificationPassesThroughWithoutIssuingAuthority) {
  auto expected = SuccessfulReply();
  auto result = Complete(expected.Clone());
  ASSERT_TRUE(result);
  EXPECT_TRUE(result->Equals(*expected));
}

TEST_F(CoreServiceImplBackupRecoveryReplyTest,
       ExactInvalidHistoryKeepsItsClosedFailure) {
  auto expected = mojom::BackupRestoreRecoveryInspectionResult::New(
      Operation(),
      mojom::BackupRestoreRecoveryInspectionStatus::kInvalidHistory, nullptr,
      mojom::BackupRestoreRecoveryFailure::New(
          mojom::BackupRestoreRecoveryError::kBindingChanged));
  auto result = Complete(expected.Clone());
  ASSERT_TRUE(result);
  EXPECT_TRUE(result->Equals(*expected));
}

}  // namespace
}  // namespace taffy
