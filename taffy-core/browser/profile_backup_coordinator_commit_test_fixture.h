// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PROFILE_BACKUP_COORDINATOR_COMMIT_TEST_FIXTURE_H_
#define TAFFY_BROWSER_PROFILE_BACKUP_COORDINATOR_COMMIT_TEST_FIXTURE_H_

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/bind.h"
#include "base/time/time.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "taffy/browser/core_effect_broker.h"
#include "taffy/browser/core_page_observation_broker.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_task_effect_test_peer.h"
#include "taffy/browser/core_service_manager_test_support.h"
#include "taffy/browser/profile_backup_coordinator.h"
#include "taffy/browser/profile_backup_coordinator_test_support.h"
#include "taffy/components/storage/browser/backup_archive_stage_store.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::backup_coordinator_commit_test {

namespace wire = core_service::mojom;
using Peer = ProfileBackupCoordinatorTestPeer;
using Outcome = wire::BackupRestoreCommitOutcome;
inline constexpr char kSource[] = "11111111-1111-4111-8111-111111111111";
inline constexpr char kTarget[] = "22222222-2222-4222-8222-222222222222";
inline constexpr char kInstallation[] = "33333333-3333-4333-8333-333333333333";
inline constexpr char kOperation[] = "commit-test";

inline wire::BackupRestorePlanResultPtr Plan(uint64_t generation) {
  auto plan = wire::BackupRestorePlanResult::New();
  plan->operation = wire::OperationEnvelope::New(
      "plan", generation, 0u,
      base::TimeTicks::Now().since_origin().InMilliseconds() + 30'000u,
      "plan-once");
  plan->status = wire::BackupPlanningStatus::kSucceeded;
  plan->backup_id = "archive";
  plan->target = wire::BackupRestoreTarget::New(
      wire::BackupRestoreTargetKind::kNewRegularProfile, kTarget);
  plan->snapshot_sha256.assign(32u, 1u);
  plan->confirmation_sha256.assign(32u, 2u);
  plan->binding = wire::BackupRestoreBinding::New(
      plan->operation.Clone(), kSource, plan->target.Clone(), plan->backup_id,
      plan->snapshot_sha256, plan->confirmation_sha256);
  return plan;
}

class ProfileBackupCoordinatorCommitTest : public testing::Test {
 protected:
  ProfileBackupCoordinatorCommitTest();
  ~ProfileBackupCoordinatorCommitTest() override;

  void SetUp() override;
  void TearDown() override;

  void Commit() {
    ASSERT_TRUE(Peer::MarkStaged(*coordinator_, kOperation));
    coordinator_->CommitStagedImportedRestore(
        kOperation,
        base::BindLambdaForTesting(
            [this](ProfileBackupCoordinator::RestoreCommitResult result) {
              ++callbacks_;
              result_ = std::move(result);
            }));
  }

  void Authorize() {
    ASSERT_TRUE(
        Peer::DeliverExactCommitAuthorization(*coordinator_, kOperation));
  }

  void Acknowledge() {
    Peer::DeliverCommitReport(*coordinator_, kOperation,
                              wire::BackupRestoreProtocolStatus::kSucceeded);
  }

  content::BrowserTaskEnvironment environment_;
  std::unique_ptr<content::TestBrowserContext> context_;
  test::QuietManagerTail tail_;
  std::unique_ptr<CoreServiceManager> manager_;
  mojo::PendingReceiver<wire::TaffyCoreService> service_;
  mojo::PendingReceiver<wire::CoreSession> session_;
  base::ScopedTempDir directory_;
  scoped_refptr<storage::backup::BackupArchiveStageStore> store_;
  ProfileBackupRestoreTargetTestState target_{kTarget};
  std::unique_ptr<ProfileBackupCoordinator> coordinator_;
  int callbacks_ = 0;
  std::optional<ProfileBackupCoordinator::RestoreCommitResult> result_;
};

// The chromium-style plugin refuses an inlined constructor, destructor or
// non-empty virtual body on a class this complex, and this fixture has no .cc
// of its own: two test translation units include this header, so the
// definitions are out of line here and carry `inline`.
inline ProfileBackupCoordinatorCommitTest::
    ProfileBackupCoordinatorCommitTest() = default;

inline ProfileBackupCoordinatorCommitTest::
    ~ProfileBackupCoordinatorCommitTest() = default;

inline void ProfileBackupCoordinatorCommitTest::SetUp() {
  context_ = std::make_unique<content::TestBrowserContext>();
  auto supervisor = std::make_unique<ProfileToolSupervisor>(
      1u, ProfileToolSupervisor::PythonPorts::Unsupported(),
      ProfileToolSupervisor::LocalModelPorts::Unsupported(),
      ProfileToolSupervisor::MediaPorts::Unsupported());
  manager_ = tail_.MakeManager(
      context_.get(), nullptr, std::move(supervisor),
      base::MakeRefCounted<CorePageObservationBroker>(context_.get()),
      std::make_unique<CoreEffectBroker>(CoreEffectBroker::Handlers{}));
  service_ = CoreServiceManagerTaskEffectTestPeer::BindService(*manager_);
  session_ = CoreServiceManagerTaskEffectTestPeer::MakeReady(*manager_);
  CoreServiceManagerTaskEffectTestPeer::SetBrowserProfileId(*manager_,
                                                            kSource);
  ASSERT_TRUE(directory_.CreateUniqueTempDir());
  store_ = storage::backup::BackupArchiveStageStore::Create(
      directory_.GetPath().Append(
          storage::backup::kBackupStagingDirectoryName));
  ASSERT_TRUE(store_);
  coordinator_ = std::make_unique<ProfileBackupCoordinator>(
      manager_.get(), store_, kInstallation);
  storage::backup::Secret key{};
  key.fill(1u);
  ASSERT_TRUE(coordinator_->BeginImport(kOperation, key).has_value());
  base::FilePath payload_path;
  auto payload = base::CreateAndOpenTemporaryFileInDir(directory_.GetPath(),
                                                       &payload_path);
  ASSERT_TRUE(payload.IsValid());
  ASSERT_TRUE(Peer::SetReadyRestore(
      *coordinator_, kOperation, target_.TakeOwner(),
      Plan(manager_->service_generation()), std::move(payload)));
}

inline void ProfileBackupCoordinatorCommitTest::TearDown() {
  coordinator_.reset();
  manager_->Shutdown();
  environment_.RunUntilIdle();
}

}  // namespace taffy::backup_coordinator_commit_test

#endif  // TAFFY_BROWSER_PROFILE_BACKUP_COORDINATOR_COMMIT_TEST_FIXTURE_H_
