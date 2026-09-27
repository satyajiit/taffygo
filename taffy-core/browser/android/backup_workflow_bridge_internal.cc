// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/android/backup_workflow_bridge_internal.h"

#include <optional>
#include <utility>

#include "base/android/jni_string.h"
#include "base/functional/bind.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/nuke_profile_directory_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "content/public/browser/browser_thread.h"
#include "taffy/browser/android/backup_workflow_android_notifications.h"
#include "taffy/browser/android/backup_workflow_restore_android.h"
#include "taffy/browser/backup_installation_id.h"
#include "taffy/browser/backup_restore_profile_registry.h"
#include "taffy/browser/core_service_manager_factory.h"
#include "taffy/contracts/core-service/generated/cpp/core_service_enums.h"

namespace taffy {

namespace backup_workflow_internal {

std::optional<std::vector<core_service::mojom::BackupRecordKind>>
DecodeSelection(const std::vector<int32_t>& wire_selection) {
  constexpr size_t kMaximumSelectableRecordKinds = 6u;
  if (wire_selection.empty() ||
      wire_selection.size() > kMaximumSelectableRecordKinds) {
    return std::nullopt;
  }
  std::vector<core_service::mojom::BackupRecordKind> selection;
  selection.reserve(wire_selection.size());
  for (int32_t wire : wire_selection) {
    if (wire < 0) {
      return std::nullopt;
    }
    std::optional<core_service::mojom::BackupRecordKind> kind =
        core_service::wire::BackupRecordKindFromWire(
            static_cast<uint32_t>(wire));
    if (!kind) {
      return std::nullopt;
    }
    selection.push_back(*kind);
  }
  return selection;
}

}  // namespace backup_workflow_internal
namespace {

bool IsEligibleSourceProfile(Profile* profile) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  ProfileManager* manager =
      g_browser_process ? g_browser_process->profile_manager() : nullptr;
  PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  if (!profile || !manager || !local_state || profile->IsOffTheRecord() ||
      profile->GetOriginalProfile() != profile ||
      !manager->IsValidProfile(profile) ||
      manager->GetLastUsedProfileDir() != profile->GetPath() ||
      IsProfileDirectoryMarkedForDeletion(profile->GetPath()) ||
      BackupRestoreQuarantineForProfilePath(local_state, profile->GetPath()) !=
          BackupRestoreProfileQuarantineStatus::kNotQuarantined) {
    return false;
  }
  ProfileAttributesEntry* entry =
      manager->GetProfileAttributesStorage().GetProfileAttributesWithPath(
          profile->GetPath());
  return entry && !entry->IsOmitted() && !entry->IsEphemeral() &&
         manager->IsAllowedProfilePath(profile->GetPath());
}

void ReleaseStageStore(
    scoped_refptr<storage::backup::BackupArchiveStageStore> store) {}

void DeliverCreatedStageStore(
    base::WeakPtr<BackupWorkflowAndroidBridge> bridge,
    scoped_refptr<storage::backup::BackupArchiveStageStore> store) {
  if (bridge && store && bridge->InstallStageStore(store)) {
    return;
  }
  if (store) {
    base::ThreadPool::PostTask(
        FROM_HERE,
        {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
         base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
        base::BindOnce(&ReleaseStageStore, std::move(store)));
  }
}

}  // namespace

std::unique_ptr<BackupWorkflowAndroidBridge>
BackupWorkflowAndroidBridge::Create(const jni_zero::JavaRef<jobject>& caller,
                                    Profile* profile) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (!IsEligibleSourceProfile(profile)) {
    return nullptr;
  }
  CoreServiceManager* manager =
      CoreServiceManagerFactory::GetForProfile(profile);
  std::optional<std::string> installation_id =
      GetOrCreateBackupInstallationId(g_browser_process->local_state());
  if (!manager || !installation_id) {
    return nullptr;
  }
  return std::unique_ptr<BackupWorkflowAndroidBridge>(
      new BackupWorkflowAndroidBridge(caller, profile, manager,
                                      std::move(*installation_id)));
}

BackupWorkflowAndroidBridge::BackupWorkflowAndroidBridge(
    const jni_zero::JavaRef<jobject>& caller,
    Profile* profile,
    CoreServiceManager* manager,
    std::string installation_id)
    : caller_(caller),
      profile_(profile),
      manager_(manager),
      workflow_(
          std::make_unique<ProfileBackupWorkflow>(manager,
                                                  std::move(installation_id))),
      restore_(std::make_unique<BackupWorkflowRestoreAndroid>(
          profile,
          g_browser_process->profile_manager(),
          g_browser_process->local_state(),
          workflow_.get())) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  const base::FilePath stage_directory =
      profile_->GetPath().Append(storage::backup::kBackupStagingDirectoryName);
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&storage::backup::BackupArchiveStageStore::Create,
                     stage_directory),
      base::BindOnce(&DeliverCreatedStageStore, weak_factory_.GetWeakPtr()));
}

BackupWorkflowAndroidBridge::~BackupWorkflowAndroidBridge() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
}

bool BackupWorkflowAndroidBridge::InstallStageStore(
    scoped_refptr<storage::backup::BackupArchiveStageStore> store) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  return Eligible() && workflow_->InstallStageStore(std::move(store));
}

bool BackupWorkflowAndroidBridge::Eligible() const {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  return IsEligibleSourceProfile(profile_) &&
         CoreServiceManagerFactory::GetForProfileIfExists(profile_) == manager_;
}

base::WeakPtr<BackupWorkflowAndroidBridge>
BackupWorkflowAndroidBridge::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

void BackupWorkflowAndroidBridge::OnExportPrepared(
    ProfileBackupWorkflow::WindowToken window,
    std::string operation_id,
    ProfileBackupWorkflow::ExportPreparation result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  backup_workflow_notifications::ExportPrepared(
      caller_, static_cast<int64_t>(window), operation_id,
      static_cast<int64_t>(result.archive_bytes),
      static_cast<int32_t>(result.status));
}

void BackupWorkflowAndroidBridge::UnregisterWindow(
    ProfileBackupWorkflow::WindowToken window) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  restore_->OnWindowUnregistered(window);
  workflow_->UnregisterWindow(window);
}

void BackupWorkflowAndroidBridge::AbandonOperation(
    ProfileBackupWorkflow::WindowToken window,
    const std::string& operation_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  restore_->OnOperationAbandoned(window, operation_id);
  workflow_->AbandonOperation(window, operation_id);
}

}  // namespace taffy
