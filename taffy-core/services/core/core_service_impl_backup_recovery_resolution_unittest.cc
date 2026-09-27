// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <cstdint>
#include <tuple>
#include <utility>

#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "taffy/services/core/core_service_impl.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {

class CoreServiceImplBackupRecoveryResolutionTestPeer {
 public:
  static void SetAdmissionState(CoreServiceImpl& service,
                                bool ready,
                                bool shutdown,
                                uint64_t generation) {
    service.ready_ = ready;
    service.shutdown_started_ = shutdown;
    service.generation_ = generation;
  }

  static void CompleteAuthorization(
      CoreServiceImpl& service,
      core_service::mojom::OperationEnvelopePtr operation,
      CoreServiceImpl::ChooseBackupRestoreRecoveryResolutionCallback callback,
      core_service::mojom::BackupRestoreRecoveryResolutionAuthorizationResultPtr
          result) {
    service.OnBackupProtocolReply<
        core_service::mojom::
            BackupRestoreRecoveryResolutionAuthorizationResult>(
        std::move(operation), std::move(callback), std::move(result));
  }

  static void CompleteReport(
      CoreServiceImpl& service,
      core_service::mojom::OperationEnvelopePtr operation,
      CoreServiceImpl::ReportBackupRestoreRecoveryResolutionOutcomeCallback
          callback,
      core_service::mojom::BackupRestoreProtocolResultPtr result) {
    service.OnBackupProtocolReply<
        core_service::mojom::BackupRestoreProtocolResult>(
        std::move(operation), std::move(callback), std::move(result));
  }
};

namespace {

namespace mojom = core_service::mojom;
using Peer = CoreServiceImplBackupRecoveryResolutionTestPeer;
constexpr uint64_t kGeneration = 7u;

mojom::OperationEnvelopePtr Operation() {
  return mojom::OperationEnvelope::New("recovery-resolution", kGeneration, 0u,
                                       60'000u, "resolution-once");
}

enum class Method { kChoose, kReport };
enum class Refusal {
  kNotReady,
  kShutdown,
  kWrongGeneration,
  kMissingRequest,
  kMissingOperation,
};

class CoreServiceImplBackupRecoveryResolutionAdmissionTest
    : public testing::TestWithParam<std::tuple<Method, Refusal>> {
 protected:
  CoreServiceImplBackupRecoveryResolutionAdmissionTest()
      : impl_(remote_.BindNewPipeAndPassReceiver()) {}

  base::test::TaskEnvironment environment_;
  mojo::Remote<mojom::TaffyCoreService> remote_;
  CoreServiceImpl impl_;
};

TEST_P(CoreServiceImplBackupRecoveryResolutionAdmissionTest,
       RefusalIsSynchronousAndCarriesNoAuthority) {
  const auto [method, refusal] = GetParam();
  Peer::SetAdmissionState(impl_, refusal != Refusal::kNotReady,
                          refusal == Refusal::kShutdown, kGeneration);
  // The implementation's overrides are private; access control follows the
  // static type, so the admitting calls are made through the interface that
  // declares them, as the sibling backup suite does.
  auto& session = static_cast<mojom::CoreSession&>(impl_);
  if (method == Method::kChoose) {
    auto request = mojom::BackupRestoreRecoveryResolutionRequest::New();
    request->operation = Operation();
    if (refusal == Refusal::kWrongGeneration) {
      ++request->operation->service_generation;
    } else if (refusal == Refusal::kMissingRequest) {
      request.reset();
    } else if (refusal == Refusal::kMissingOperation) {
      request->operation.reset();
    }
    base::test::TestFuture<
        mojom::BackupRestoreRecoveryResolutionAuthorizationResultPtr>
        future;
    session.ChooseBackupRestoreRecoveryResolution(std::move(request),
                                                  future.GetCallback());
    ASSERT_TRUE(future.IsReady());
    ASSERT_TRUE(future.Get());
    EXPECT_EQ(mojom::BackupRestoreProtocolStatus::kUnavailable,
              future.Get()->status);
    EXPECT_FALSE(future.Get()->authorization);
    return;
  }

  auto report = mojom::BackupRestoreRecoveryResolutionOutcomeReport::New();
  report->operation = Operation();
  if (refusal == Refusal::kWrongGeneration) {
    ++report->operation->service_generation;
  } else if (refusal == Refusal::kMissingRequest) {
    report.reset();
  } else if (refusal == Refusal::kMissingOperation) {
    report->operation.reset();
  }
  base::test::TestFuture<mojom::BackupRestoreProtocolResultPtr> future;
  session.ReportBackupRestoreRecoveryResolutionOutcome(std::move(report),
                                                       future.GetCallback());
  ASSERT_TRUE(future.IsReady());
  ASSERT_TRUE(future.Get());
  EXPECT_EQ(mojom::BackupRestoreProtocolStatus::kUnavailable,
            future.Get()->status);
}

INSTANTIATE_TEST_SUITE_P(
    Admission,
    CoreServiceImplBackupRecoveryResolutionAdmissionTest,
    testing::Combine(testing::Values(Method::kChoose, Method::kReport),
                     testing::Values(Refusal::kNotReady,
                                     Refusal::kShutdown,
                                     Refusal::kWrongGeneration,
                                     Refusal::kMissingRequest,
                                     Refusal::kMissingOperation)));

class CoreServiceImplBackupRecoveryResolutionReplyTest : public testing::Test {
 protected:
  CoreServiceImplBackupRecoveryResolutionReplyTest()
      : impl_(remote_.BindNewPipeAndPassReceiver()) {
    Peer::SetAdmissionState(impl_, true, false, kGeneration);
  }

  base::test::TaskEnvironment environment_;
  mojo::Remote<mojom::TaffyCoreService> remote_;
  CoreServiceImpl impl_;
};

TEST_F(CoreServiceImplBackupRecoveryResolutionReplyTest,
       FailedAuthorizationConversionIsClosedRefusal) {
  base::test::TestFuture<
      mojom::BackupRestoreRecoveryResolutionAuthorizationResultPtr>
      future;
  Peer::CompleteAuthorization(impl_, Operation(), future.GetCallback(),
                              nullptr);
  ASSERT_TRUE(future.IsReady() && future.Get());
  EXPECT_EQ(mojom::BackupRestoreProtocolStatus::kUnavailable,
            future.Get()->status);
  EXPECT_FALSE(future.Get()->authorization);
}

TEST_F(CoreServiceImplBackupRecoveryResolutionReplyTest,
       LateShutdownOrOperationDriftWithdrawsReply) {
  for (bool shutdown : {false, true}) {
    SCOPED_TRACE(shutdown);
    Peer::SetAdmissionState(impl_, true, shutdown, kGeneration);
    auto result = mojom::BackupRestoreProtocolResult::New(
        Operation(), mojom::BackupRestoreProtocolStatus::kSucceeded);
    if (!shutdown) {
      result->operation->idempotency_key = "different";
    }
    base::test::TestFuture<mojom::BackupRestoreProtocolResultPtr> future;
    Peer::CompleteReport(impl_, Operation(), future.GetCallback(),
                         std::move(result));
    ASSERT_TRUE(future.IsReady() && future.Get());
    EXPECT_EQ(mojom::BackupRestoreProtocolStatus::kUnavailable,
              future.Get()->status);
  }
}

}  // namespace
}  // namespace taffy
