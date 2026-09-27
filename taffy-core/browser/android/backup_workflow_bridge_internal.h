// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_ANDROID_BACKUP_WORKFLOW_BRIDGE_INTERNAL_H_
#define TAFFY_BROWSER_ANDROID_BACKUP_WORKFLOW_BRIDGE_INTERNAL_H_

#include <jni.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/android/scoped_java_ref.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "taffy/browser/profile_backup_workflow.h"

class Profile;

namespace taffy {

class CoreServiceManager;
class BackupWorkflowRestoreAndroid;

class BackupWorkflowAndroidBridge {
 public:
  static std::unique_ptr<BackupWorkflowAndroidBridge> Create(
      const jni_zero::JavaRef<jobject>& caller,
      Profile* profile);

  BackupWorkflowAndroidBridge(const BackupWorkflowAndroidBridge&) = delete;
  BackupWorkflowAndroidBridge& operator=(const BackupWorkflowAndroidBridge&) =
      delete;
  ~BackupWorkflowAndroidBridge();

  bool Eligible() const;
  ProfileBackupWorkflow* workflow() { return workflow_.get(); }
  BackupWorkflowRestoreAndroid* restore() { return restore_.get(); }
  base::WeakPtr<BackupWorkflowAndroidBridge> GetWeakPtr();
  bool InstallStageStore(
      scoped_refptr<storage::backup::BackupArchiveStageStore> store);

  void OnExportPrepared(ProfileBackupWorkflow::WindowToken window,
                        std::string operation_id,
                        ProfileBackupWorkflow::ExportPreparation result);
  void UnregisterWindow(ProfileBackupWorkflow::WindowToken window);
  void AbandonOperation(ProfileBackupWorkflow::WindowToken window,
                        const std::string& operation_id);

 private:
  BackupWorkflowAndroidBridge(const jni_zero::JavaRef<jobject>& caller,
                              Profile* profile,
                              CoreServiceManager* manager,
                              std::string installation_id);

  const jni_zero::ScopedJavaGlobalRef<jobject> caller_;
  const raw_ptr<Profile> profile_;
  const raw_ptr<CoreServiceManager> manager_;
  std::unique_ptr<ProfileBackupWorkflow> workflow_;
  std::unique_ptr<BackupWorkflowRestoreAndroid> restore_;
  base::WeakPtrFactory<BackupWorkflowAndroidBridge> weak_factory_{this};
};

namespace backup_workflow_internal {

// Validates the record-kind selection a surface sent across JNI. Returns
// nothing unless every entry names a kind this build knows and the set is
// non-empty and within the six the workflow accepts, so an unknown wire value
// refuses the export rather than silently narrowing it.
std::optional<std::vector<core_service::mojom::BackupRecordKind>>
DecodeSelection(const std::vector<int32_t>& wire_selection);

}  // namespace backup_workflow_internal

}  // namespace taffy

#endif  // TAFFY_BROWSER_ANDROID_BACKUP_WORKFLOW_BRIDGE_INTERNAL_H_
