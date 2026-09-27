// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <string>
#include <utility>

#include "content/public/browser/browser_context.h"
#include "content/public/test/fake_download_item.h"
#include "content/public/test/mock_download_manager.h"
#include "content/public/test/test_renderer_host.h"
#include "taffy/browser/core_effect_broker.h"
#include "taffy/browser/core_page_observation_broker.h"
#include "taffy/browser/core_service_manager_task_effect_test_peer.h"
#include "taffy/browser/core_service_manager_test_support.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

// FakeDownloadItem leaves these two live safety queries unimplemented.
class OpenableTaskFile final : public content::FakeDownloadItem {
 public:
  OpenableTaskFile();
  ~OpenableTaskFile() override;
  bool CanOpenDownload() override { return openable; }
  InsecureDownloadStatus GetInsecureDownloadStatus() const override {
    return insecure;
  }
  bool openable = true;
  InsecureDownloadStatus insecure = InsecureDownloadStatus::SAFE;
};

OpenableTaskFile::OpenableTaskFile() = default;
OpenableTaskFile::~OpenableTaskFile() = default;

class CoreServiceManagerTaskDownloadOpenTest
    : public content::RenderViewHostTestHarness {
 protected:
  void SetUp() override {
    content::RenderViewHostTestHarness::SetUp();
    auto downloads =
        std::make_unique<testing::NiceMock<content::MockDownloadManager>>();
    ON_CALL(*downloads, GetDownloadByGuid(testing::_))
        .WillByDefault([this](const std::string& guid) -> download::DownloadItem* {
          return guid == file_.GetGuid() && file_present_ ? &file_ : nullptr;
        });
    browser_context()->SetDownloadManagerForTesting(std::move(downloads));
    file_.SetGuid("actual-profile-guid");
    file_.SetMimeType("application/pdf");
    file_.SetState(download::DownloadItem::COMPLETE);
    auto tools = std::make_unique<ProfileToolSupervisor>(
        1u, ProfileToolSupervisor::PythonPorts::Unsupported(),
        ProfileToolSupervisor::LocalModelPorts::Unsupported(),
        ProfileToolSupervisor::MediaPorts::Unsupported());
    manager_ = tail_.MakeManager(
        browser_context(), nullptr, std::move(tools),
        base::MakeRefCounted<CorePageObservationBroker>(browser_context()),
        std::make_unique<CoreEffectBroker>(CoreEffectBroker::Handlers{}));
    session_ = CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager_);
  }

  void TearDown() override {
    manager_->Shutdown();
    manager_.reset();
    content::RenderViewHostTestHarness::TearDown();
  }

  void RecordStarted(const std::string& session_id) {
    CoreServiceManagerTaskEffectTestPeer::RecordStartedTaskDownload(
        *manager_, "task-1", session_id, file_.GetGuid());
  }

  void RegisterTerminal(mojom::TerminalTaskKind kind, bool publish = true) {
    auto bindings = mojom::CoreStateBrowserBindings::New();
    bindings->service_generation = 1u;
    bindings->state_sequence = ++sequence_;
    bindings->task_revisions.push_back(mojom::TaskRevisionBinding::New(
        "task-1", 1u, sequence_, std::vector<mojom::TaskControlKind>()));
    bindings->terminal_tasks.push_back(mojom::TerminalTaskBinding::New(
        "task-1", 1u, sequence_, kind));
    ASSERT_EQ(CoreServiceManagerTaskEffectTestPeer::RegisterBindings(
                  *manager_, std::move(bindings)),
              mojom::PendingApprovalRegistrationStatus::kRegistered);
    if (publish) {
      Publish();
    }
  }

  void Publish() {
    auto state = mojom::CoreStateUpdate::New();
    state->service_generation = 1u;
    state->sequence = sequence_;
    state->core_status_schema_version = 1u;
    state->payload = {1u};
    CoreServiceManagerTaskEffectTestPeer::Publish(*manager_, std::move(state));
  }

  bool CanOpen() {
    return manager_->CanOpenTaskDownloadForPerson("task-1", file_.GetGuid());
  }

  test::QuietManagerTail tail_;
  std::unique_ptr<CoreServiceManager> manager_;
  mojo::PendingReceiver<mojom::CoreSession> session_;
  OpenableTaskFile file_;
  bool file_present_ = true;
  uint64_t sequence_ = 0u;
};

TEST_F(CoreServiceManagerTaskDownloadOpenTest,
       ManualProfileFileNeedsRecordedOwnershipAndPublishedTerminal) {
  // Presence in the profile and an identical file type/name are insufficient.
  RegisterTerminal(mojom::TerminalTaskKind::kCompleted);
  EXPECT_FALSE(CanOpen());
  RecordStarted(manager_->browser_session_id());
  EXPECT_TRUE(CanOpen());
  EXPECT_FALSE(manager_->CanOpenTaskDownloadForPerson(
      "another-task", file_.GetGuid()));
  EXPECT_FALSE(manager_->CanOpenTaskDownloadForPerson("task-1", "other-guid"));

  RegisterTerminal(mojom::TerminalTaskKind::kCompleted, /*publish=*/false);
  EXPECT_FALSE(CanOpen());
  Publish();
  EXPECT_TRUE(CanOpen());

  // A completed file is independent of the page that initiated it. It uses
  // no actor lease or source consent, both revoked at terminal publication.
  DeleteContents();
  EXPECT_TRUE(CanOpen());
}

TEST_F(CoreServiceManagerTaskDownloadOpenTest,
       PriorSessionOwnershipCannotBeRecreatedByListingOrARepeatedGuid) {
  RecordStarted("previous-browser-session");
  RegisterTerminal(mojom::TerminalTaskKind::kCompleted);
  EXPECT_FALSE(CanOpen());
  RecordStarted(manager_->browser_session_id());
  EXPECT_FALSE(CanOpen());
}

TEST_F(CoreServiceManagerTaskDownloadOpenTest,
       EveryOpenRechecksTheNativeFileAndTask) {
  RecordStarted(manager_->browser_session_id());
  RegisterTerminal(mojom::TerminalTaskKind::kCompleted);
  ASSERT_TRUE(CanOpen());
  file_.SetState(download::DownloadItem::IN_PROGRESS);
  EXPECT_FALSE(CanOpen());
  file_.SetState(download::DownloadItem::COMPLETE);
  file_.SetFileExternallyRemoved(true);
  EXPECT_FALSE(CanOpen());
  file_.SetFileExternallyRemoved(false);
  file_.openable = false;
  EXPECT_FALSE(CanOpen());
  file_.openable = true;
  file_.insecure = download::DownloadItem::InsecureDownloadStatus::BLOCK;
  EXPECT_FALSE(CanOpen());
  file_.insecure = download::DownloadItem::InsecureDownloadStatus::VALIDATED;
  EXPECT_TRUE(CanOpen());
  file_.SetIsDangerous(true);
  EXPECT_FALSE(CanOpen());
  file_.SetIsDangerous(false);
  file_present_ = false;
  EXPECT_FALSE(CanOpen());
  file_present_ = true;
  RegisterTerminal(mojom::TerminalTaskKind::kPartial);
  EXPECT_TRUE(CanOpen());
  RegisterTerminal(mojom::TerminalTaskKind::kFailed);
  EXPECT_TRUE(CanOpen());
  RegisterTerminal(mojom::TerminalTaskKind::kCancelled);
  EXPECT_FALSE(CanOpen());
  manager_->Shutdown();
  EXPECT_FALSE(CanOpen());
}

}  // namespace
}  // namespace taffy
