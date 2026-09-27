// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The two single-result replies outside the backup surface that answer a
// refused conversion with nullptr.
//
// `RustCore::ExportPageSnapshot` and `RustCore::MatchSiteSkills` both return
// nullptr when the request will not project or the answer's shape is refused,
// and both mojom responses are non-nullable. Forwarding the nullptr fails
// receive-side validation and raises an error on the CoreSession pipe, which
// ends the whole utility generation — every task and every pending storage
// effect — rather than the one call that was refused. The guards under test
// turn that into one closed refusal instead.

#include <utility>

#include "base/functional/callback.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "taffy/services/core/core_service_impl.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {

class CoreServiceImplReplyGuardTestPeer {
 public:
  static void SetAdmissionState(CoreServiceImpl& service,
                                bool ready,
                                bool shutdown,
                                uint64_t generation) {
    service.ready_ = ready;
    service.shutdown_started_ = shutdown;
    service.generation_ = generation;
  }

  static void CompletePageSnapshotExport(
      CoreServiceImpl& service,
      core_service::mojom::OperationEnvelopePtr operation,
      core_service::mojom::PageSnapshotExportFormat format,
      base::OnceCallback<
          void(core_service::mojom::PageSnapshotExportResultPtr)> callback,
      core_service::mojom::PageSnapshotExportResultPtr result) {
    service.OnPageSnapshotExportReply(std::move(operation), format,
                                      std::move(callback), std::move(result));
  }

  static void CompleteSiteSkillMatch(
      CoreServiceImpl& service,
      core_service::mojom::OperationEnvelopePtr operation,
      base::OnceCallback<void(core_service::mojom::SiteSkillMatchResultPtr)>
          callback,
      core_service::mojom::SiteSkillMatchResultPtr result) {
    service.OnSiteSkillMatchReply(std::move(operation), std::move(callback),
                                  std::move(result));
  }
};

namespace {

namespace mojom = core_service::mojom;
constexpr uint64_t kGeneration = 11;

class CoreServiceImplReplyGuardTest : public testing::Test {
 protected:
  CoreServiceImplReplyGuardTest()
      : impl_(service_.BindNewPipeAndPassReceiver()) {
    CoreServiceImplReplyGuardTestPeer::SetAdmissionState(impl_, true, false,
                                                         kGeneration);
    operation_ = mojom::OperationEnvelope::New("reply-guard", kGeneration, 0,
                                               60'000, "guard-idempotency");
  }

  mojom::PageSnapshotExportResultPtr ExportReply(
      mojom::PageSnapshotExportFormat format,
      mojom::PageSnapshotExportResultPtr result) {
    base::test::TestFuture<mojom::PageSnapshotExportResultPtr> future;
    CoreServiceImplReplyGuardTestPeer::CompletePageSnapshotExport(
        impl_, operation_.Clone(), format, future.GetCallback(),
        std::move(result));
    EXPECT_TRUE(future.IsReady());
    return future.Take();
  }

  mojom::SiteSkillMatchResultPtr MatchReply(
      mojom::SiteSkillMatchResultPtr result) {
    base::test::TestFuture<mojom::SiteSkillMatchResultPtr> future;
    CoreServiceImplReplyGuardTestPeer::CompleteSiteSkillMatch(
        impl_, operation_.Clone(), future.GetCallback(), std::move(result));
    EXPECT_TRUE(future.IsReady());
    return future.Take();
  }

  base::test::TaskEnvironment task_environment_;
  mojo::Remote<mojom::TaffyCoreService> service_;
  CoreServiceImpl impl_;
  mojom::OperationEnvelopePtr operation_;
};

TEST_F(CoreServiceImplReplyGuardTest, NullExportIsAClosedRefusal) {
  auto result = ExportReply(mojom::PageSnapshotExportFormat::kMarkdown,
                            mojom::PageSnapshotExportResultPtr());
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->operation);
  EXPECT_TRUE(result->operation->Equals(*operation_));
  EXPECT_EQ(result->status, mojom::PageSnapshotExportStatus::kUnavailable);
  // The refusal reports the format that was asked for, so the surface that
  // receives it can still say which export failed.
  EXPECT_EQ(result->format, mojom::PageSnapshotExportFormat::kMarkdown);
}

TEST_F(CoreServiceImplReplyGuardTest, ExportOperationMustMatch) {
  auto drifted = mojom::PageSnapshotExportResult::New();
  drifted->operation = operation_.Clone();
  ++drifted->operation->deadline_monotonic_ms;
  drifted->status = mojom::PageSnapshotExportStatus::kExported;
  auto result = ExportReply(mojom::PageSnapshotExportFormat::kMarkdown,
                            std::move(drifted));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, mojom::PageSnapshotExportStatus::kUnavailable);
}

TEST_F(CoreServiceImplReplyGuardTest, LiveExportReplyIsKept) {
  auto converted = mojom::PageSnapshotExportResult::New();
  converted->operation = operation_.Clone();
  converted->status = mojom::PageSnapshotExportStatus::kExported;
  converted->format = mojom::PageSnapshotExportFormat::kMarkdown;
  auto expected = converted.Clone();
  auto result = ExportReply(mojom::PageSnapshotExportFormat::kMarkdown,
                            std::move(converted));
  ASSERT_TRUE(result);
  EXPECT_TRUE(result->Equals(*expected));
}

TEST_F(CoreServiceImplReplyGuardTest, ShutdownWithdrawsAQueuedExport) {
  auto converted = mojom::PageSnapshotExportResult::New();
  converted->operation = operation_.Clone();
  converted->status = mojom::PageSnapshotExportStatus::kExported;
  CoreServiceImplReplyGuardTestPeer::SetAdmissionState(impl_, true, true,
                                                       kGeneration);
  auto result = ExportReply(mojom::PageSnapshotExportFormat::kMarkdown,
                            std::move(converted));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, mojom::PageSnapshotExportStatus::kUnavailable);
}

TEST_F(CoreServiceImplReplyGuardTest, NullSkillMatchIsAClosedRefusal) {
  auto result = MatchReply(mojom::SiteSkillMatchResultPtr());
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->operation);
  EXPECT_TRUE(result->operation->Equals(*operation_));
  EXPECT_EQ(result->status, mojom::SiteSkillMatchStatus::kUnavailable);
  EXPECT_TRUE(result->offers.empty());
}

TEST_F(CoreServiceImplReplyGuardTest, LiveSkillMatchReplyIsKept) {
  auto converted = mojom::SiteSkillMatchResult::New();
  converted->operation = operation_.Clone();
  converted->status = mojom::SiteSkillMatchStatus::kAvailable;
  auto expected = converted.Clone();
  auto result = MatchReply(std::move(converted));
  ASSERT_TRUE(result);
  EXPECT_TRUE(result->Equals(*expected));
}

TEST_F(CoreServiceImplReplyGuardTest, AnotherGenerationCannotReceiveAMatch) {
  auto converted = mojom::SiteSkillMatchResult::New();
  converted->operation = operation_.Clone();
  converted->status = mojom::SiteSkillMatchStatus::kAvailable;
  CoreServiceImplReplyGuardTestPeer::SetAdmissionState(impl_, true, false,
                                                       kGeneration + 1);
  auto result = MatchReply(std::move(converted));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->status, mojom::SiteSkillMatchStatus::kUnavailable);
}

}  // namespace
}  // namespace taffy
