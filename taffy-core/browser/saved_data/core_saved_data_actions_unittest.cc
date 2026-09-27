// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/saved_data/core_saved_data_actions.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "taffy/browser/core_effect_broker.h"
#include "taffy/browser/core_page_observation_broker.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_test_support.h"
#include "taffy/browser/saved_data/profile_saved_data_broker.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;

class RecordingSavedDataBroker final : public ProfileSavedDataBroker {
 public:
  void Start(SnapshotCallback callback) override {
    started = true;
    snapshots = std::move(callback);
  }

  void UpsertDetail(uint64_t expected_revision,
                    const std::optional<std::string>& detail_id,
                    SavedDetailInput input,
                    MutationCallback callback) override {
    upsert_revision = expected_revision;
    upsert_id = detail_id;
    upsert_input = std::move(input);
    std::move(callback).Run(next_status);
  }

  void DeleteDetail(uint64_t expected_revision,
                    const std::string& detail_id,
                    MutationCallback callback) override {
    detail_delete_revision = expected_revision;
    detail_delete_id = detail_id;
    std::move(callback).Run(next_status);
  }

  void DeleteSignIn(uint64_t expected_revision,
                    const std::string& sign_in_id,
                    MutationCallback callback) override {
    sign_in_delete_revision = expected_revision;
    sign_in_delete_id = sign_in_id;
    std::move(callback).Run(next_status);
  }

  bool started = false;
  SnapshotCallback snapshots;
  MutationStatus next_status = MutationStatus::kAccepted;
  uint64_t upsert_revision = 0;
  std::optional<std::string> upsert_id;
  SavedDetailInput upsert_input;
  uint64_t detail_delete_revision = 0;
  std::string detail_delete_id;
  uint64_t sign_in_delete_revision = 0;
  std::string sign_in_delete_id;
};

class CoreSavedDataActionsTest : public testing::Test {
 protected:
  CoreSavedDataActionsTest() {
    auto broker = std::make_unique<RecordingSavedDataBroker>();
    broker_ = broker.get();
    auto tools = std::make_unique<ProfileToolSupervisor>(
        1u, ProfileToolSupervisor::PythonPorts::Unsupported(),
        ProfileToolSupervisor::LocalModelPorts::Unsupported(),
        ProfileToolSupervisor::MediaPorts::Unsupported());
    manager_ = tail_.MakeManager(
        &browser_context_, /*storage_broker=*/nullptr, std::move(tools),
        base::MakeRefCounted<CorePageObservationBroker>(&browser_context_),
        std::make_unique<CoreEffectBroker>(CoreEffectBroker::Handlers{}),
        std::move(broker));
  }

  content::BrowserTaskEnvironment task_environment_;
  test::QuietManagerTail tail_;
  content::TestBrowserContext browser_context_;
  raw_ptr<RecordingSavedDataBroker> broker_ = nullptr;
  std::unique_ptr<CoreServiceManager> manager_;
};

TEST_F(CoreSavedDataActionsTest, ForwardsExactRevisionOpaqueIdAndDetailFields) {
  ASSERT_TRUE(broker_->started);
  std::optional<api::CoreApiSubmissionStatus> answer;
  CoreSavedDataActions::UpsertDetail(
      manager_.get(), 17u, std::optional<std::string>("saved-detail-opaque"),
      "Asha", "Rao", "asha@example.test", "555 0100", "12 First Street",
      "411001", "IN",
      base::BindOnce(
          [](std::optional<api::CoreApiSubmissionStatus>* destination,
             api::CoreApiSubmissionStatus status) { *destination = status; },
          &answer));

  EXPECT_EQ(17u, broker_->upsert_revision);
  ASSERT_TRUE(broker_->upsert_id.has_value());
  EXPECT_EQ("saved-detail-opaque", *broker_->upsert_id);
  EXPECT_EQ("asha@example.test", broker_->upsert_input.email);
  ASSERT_TRUE(answer.has_value());
  EXPECT_EQ(api::CoreApiSubmissionStatus::kAccepted, *answer);
}

TEST_F(CoreSavedDataActionsTest, MapsStaleMutationAndNeverReinterpretsIds) {
  broker_->next_status = ProfileSavedDataBroker::MutationStatus::kStaleRevision;
  std::optional<api::CoreApiSubmissionStatus> answer;
  CoreSavedDataActions::DeleteSignIn(
      manager_.get(), "saved-sign-in-opaque", 22u,
      base::BindOnce(
          [](std::optional<api::CoreApiSubmissionStatus>* destination,
             api::CoreApiSubmissionStatus status) { *destination = status; },
          &answer));

  EXPECT_EQ(22u, broker_->sign_in_delete_revision);
  EXPECT_EQ("saved-sign-in-opaque", broker_->sign_in_delete_id);
  ASSERT_TRUE(answer.has_value());
  EXPECT_EQ(api::CoreApiSubmissionStatus::kStaleRevision, *answer);
}

}  // namespace
}  // namespace taffy
