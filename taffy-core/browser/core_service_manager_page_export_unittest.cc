// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <string>
#include <utility>

#include "base/test/bind.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "taffy/browser/core_effect_broker.h"
#include "taffy/browser/core_page_observation_broker.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_test_support.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {

namespace api = core_api::mojom;
namespace service = core_service::mojom;

class CoreServiceManagerPageExportTestPeer final {
 public:
  static bool Install(CoreServiceManager& manager,
                      std::string request_id,
                      std::string operation_id,
                      CoreServicePageExportCallback callback) {
    CorePendingPageExport pending;
    pending.request_id = request_id;
    pending.operation = service::OperationEnvelope::New(
        operation_id, manager.service_generation_, 0u, 5'000u,
        "page-export-logical-key");
    pending.callback = std::move(callback);
    const auto [pending_position, pending_inserted] =
        manager.pending_page_exports_.emplace(operation_id,
                                              std::move(pending));
    const auto [request_position, request_inserted] =
        manager.active_page_export_requests_.emplace(request_id, operation_id);
    static_cast<void>(pending_position);
    static_cast<void>(request_position);
    return pending_inserted && request_inserted;
  }

  static void Finish(CoreServiceManager& manager,
                     const std::string& operation_id,
                     api::PageSnapshotExportAvailability availability) {
    auto result = api::PageSnapshotExportResult::New();
    result->availability = availability;
    manager.FinishPageExport(operation_id, std::move(result));
  }

  static bool HasAttempt(const CoreServiceManager& manager,
                         const std::string& operation_id) {
    return manager.pending_page_exports_.contains(operation_id);
  }

  static bool HasRequest(const CoreServiceManager& manager,
                         const std::string& request_id) {
    return manager.active_page_export_requests_.contains(request_id);
  }
};

namespace {

constexpr uint64_t kGeneration = 1u;

std::unique_ptr<CoreServiceManager> MakeManager(
    test::QuietManagerTail& tail,
    content::TestBrowserContext* context) {
  auto tools = std::make_unique<ProfileToolSupervisor>(
      kGeneration, ProfileToolSupervisor::PythonPorts::Unsupported(),
      ProfileToolSupervisor::LocalModelPorts::Unsupported(),
      ProfileToolSupervisor::MediaPorts::Unsupported());
  return tail.MakeManager(
      context, /*storage_broker=*/nullptr, std::move(tools),
      base::MakeRefCounted<CorePageObservationBroker>(context),
      std::make_unique<CoreEffectBroker>(CoreEffectBroker::Handlers{}));
}

class CoreServiceManagerPageExportTest : public testing::Test {
 protected:
  CoreServiceManagerPageExportTest()
      : context_(std::make_unique<content::TestBrowserContext>()),
        manager_(MakeManager(tail_, context_.get())) {}

  content::BrowserTaskEnvironment task_environment_;
  test::QuietManagerTail tail_;
  std::unique_ptr<content::TestBrowserContext> context_;
  std::unique_ptr<CoreServiceManager> manager_;
};

TEST_F(CoreServiceManagerPageExportTest,
       LateAttemptCannotSettleAReusedSurfaceRequest) {
  int first_completions = 0;
  ASSERT_TRUE(CoreServiceManagerPageExportTestPeer::Install(
      *manager_, "surface-request", "operation-old",
      base::BindLambdaForTesting(
          [&](api::PageSnapshotExportResultPtr) { ++first_completions; })));
  CoreServiceManagerPageExportTestPeer::Finish(
      *manager_, "operation-old",
      api::PageSnapshotExportAvailability::kCancelled);
  ASSERT_EQ(1, first_completions);

  int retry_completions = 0;
  ASSERT_TRUE(CoreServiceManagerPageExportTestPeer::Install(
      *manager_, "surface-request", "operation-retry",
      base::BindLambdaForTesting(
          [&](api::PageSnapshotExportResultPtr) { ++retry_completions; })));

  CoreServiceManagerPageExportTestPeer::Finish(
      *manager_, "operation-old",
      api::PageSnapshotExportAvailability::kAvailable);

  EXPECT_EQ(0, retry_completions);
  EXPECT_TRUE(CoreServiceManagerPageExportTestPeer::HasAttempt(
      *manager_, "operation-retry"));
  EXPECT_TRUE(CoreServiceManagerPageExportTestPeer::HasRequest(
      *manager_, "surface-request"));

  CoreServiceManagerPageExportTestPeer::Finish(
      *manager_, "operation-retry",
      api::PageSnapshotExportAvailability::kAvailable);
  EXPECT_EQ(1, retry_completions);
}

TEST_F(CoreServiceManagerPageExportTest,
       CancellationErasesBothIndexesAndSettlesExactlyOnce) {
  int completions = 0;
  api::PageSnapshotExportAvailability terminal =
      api::PageSnapshotExportAvailability::kAvailable;
  ASSERT_TRUE(CoreServiceManagerPageExportTestPeer::Install(
      *manager_, "surface-request", "operation-cancel",
      base::BindLambdaForTesting([&](api::PageSnapshotExportResultPtr result) {
        ++completions;
        terminal = result->availability;
      })));

  EXPECT_TRUE(manager_->CancelPageSnapshotExport("surface-request"));
  EXPECT_EQ(1, completions);
  EXPECT_EQ(api::PageSnapshotExportAvailability::kCancelled, terminal);
  EXPECT_FALSE(CoreServiceManagerPageExportTestPeer::HasAttempt(
      *manager_, "operation-cancel"));
  EXPECT_FALSE(CoreServiceManagerPageExportTestPeer::HasRequest(
      *manager_, "surface-request"));
  EXPECT_FALSE(manager_->CancelPageSnapshotExport("surface-request"));
  EXPECT_EQ(1, completions);
}

}  // namespace
}  // namespace taffy
