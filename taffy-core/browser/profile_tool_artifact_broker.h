// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PROFILE_TOOL_ARTIFACT_BROKER_H_
#define TAFFY_BROWSER_PROFILE_TOOL_ARTIFACT_BROKER_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/sequence_checker.h"
#include "taffy/browser/core_service_manager_types.h"
#include "taffy/browser/profile_tool_artifact_validation.h"
#include "taffy/browser/task_download_manager_adapter.h"
#include "taffy/common/public/taffy_download_facts.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom-forward.h"

namespace content {
class WebContents;
}

namespace taffy {

class ProfileToolHandleStore;
class ProfileToolSupervisor;

// Browser custody for descriptor-backed tool input and verified task output.
// It never accepts a path or URL. Worker handles are minted only after an
// already-open source and, when needed, a browser-owned output descriptor are
// ready; successful bytes remain here and cross Core Service only as a typed
// digest/count/artifact receipt.
class ProfileToolArtifactBroker {
 public:
  enum class SourceKind {
    kExplicitPicker,
    kCompletedDownload,
    kTaskArtifact,
  };

  // What one task lifecycle edge does to browser-resident custody. Pausing
  // keeps reusable sources and completed outputs, finishing keeps only
  // completed outputs for Save/Share, and abandoning discards everything.
  enum class TaskDisposition {
    kPaused,
    kFinished,
    kAbandoned,
  };

  struct SourceAdmission {
    SourceKind kind = SourceKind::kCompletedDownload;
    std::string task_id;
    std::string source_id;
    std::string browser_session_id;
    uint64_t service_generation = 0;
    uint64_t byte_count = 0;
    MediaTopLevelType media_type = MediaTopLevelType::kUnknown;
  };

  using PrepareCallback =
      base::OnceCallback<void(core_service::mojom::TaskEffectBindingPtr)>;
  using RetainCallback =
      base::OnceCallback<void(core_service::mojom::TaskToolOutputReceiptPtr)>;

  ProfileToolArtifactBroker(ProfileToolSupervisor* supervisor,
                            scoped_refptr<ProfileToolHandleStore> handles,
                            base::FilePath output_root,
                            bool private_profile);
  ProfileToolArtifactBroker(const ProfileToolArtifactBroker&) = delete;
  ProfileToolArtifactBroker& operator=(const ProfileToolArtifactBroker&) =
      delete;
  ~ProfileToolArtifactBroker();

  // Registers one source returned by the exact task download listing. The
  // descriptor is opened later on Chromium's download file sequence and only
  // if every snapshot fact still matches.
  bool RegisterCompletedDownload(const std::string& task_id,
                                 const std::string& browser_session_id,
                                 uint64_t service_generation,
                                 content::WebContents* web_contents,
                                 const TaskDownloadManagerSnapshot& snapshot);

  // Admission seam for an existing picker/task-artifact integration. The
  // caller supplies an already-open descriptor; raw paths and bytes have no
  // API here. Product code currently wires completed downloads through the
  // method above, while tests exercise all three closed source categories.
  bool AdmitOpenedSource(SourceAdmission admission, base::File resource);

  // Rewrites a typed media job's source/output markers to fresh one-job
  // handles only after its exact source has been admitted.
  void PrepareTaskToolJob(core_service::mojom::TaskEffectBindingPtr binding,
                          PrepareCallback callback);

  // Verifies and retains one successful Python or media result, returning the
  // content-free receipt Core Service journals. A null receipt is refusal.
  void RetainTaskToolOutput(
      const core_service::mojom::TaskEffectBinding& binding,
      core_service::mojom::EffectResultPtr result,
      RetainCallback callback);

  // Drops retained descriptor state for a refused/cancelled/failed job.
  void AbandonTaskToolJob(const std::string& job_id);

  // Applies one exact task lifecycle disposition. Any preparation or
  // validation already running on a blocking sequence remains charged until
  // its callback arrives, but is marked abandoned and can never become
  // current custody.
  void SettleTask(const std::string& task_id,
                  uint64_t service_generation,
                  TaskDisposition disposition);

  // Resolves only the exact current-generation artifact recorded by the task
  // reducer and offers its bytes to the trusted Save/Share observer surface.
  static bool RetainsArtifactKind(core_service::mojom::TaskArtifactKind kind);
  bool DeliverArtifact(
      base::ObserverList<CoreServiceObserver>& observers,
      const std::string& task_id,
      uint64_t service_generation,
      const core_service::mojom::TaskArtifactEffect& artifact);

  void SetActiveGeneration(uint64_t service_generation);
  void Shutdown();

  size_t source_count_for_testing() const;
  size_t pending_job_count_for_testing() const;
  size_t artifact_count_for_testing() const;
  size_t artifact_bytes_for_testing() const;

 private:
  struct OperationIdentity {
    std::string operation_id;
    uint64_t service_generation = 0;
    uint64_t task_revision = 0;
    uint64_t deadline_monotonic_ms = 0;
    std::string idempotency_key;
  };

  struct SourceEntry {
    SourceAdmission admission;
    base::File resource;
    base::WeakPtr<content::WebContents> web_contents;
    std::optional<TaskDownloadManagerSnapshot> download;
  };

  struct PendingMediaJob {
    std::string task_id;
    std::string action_id;
    std::string effect_id;
    std::string source_id;
    std::string input_handle;
    std::string output_handle;
    uint64_t service_generation = 0;
    OperationIdentity operation_identity;
    core_service::mojom::ToolOperation operation;
    std::optional<core_service::mojom::TaskArtifactKind> artifact_kind;
    base::File output;
    bool async_work_in_flight = false;
    bool abandoned = false;
  };

  struct PendingPythonValidation {
    std::string task_id;
    std::string action_id;
    std::string effect_id;
    uint64_t service_generation = 0;
    OperationIdentity operation_identity;
    core_service::mojom::ToolOperation operation;
    bool abandoned = false;
  };

  struct ArtifactEntry {
    std::string task_id;
    std::string source_id;
    uint64_t service_generation = 0;
    uint64_t task_revision = 0;
    core_service::mojom::TaskArtifactKind kind;
    std::vector<uint8_t> content;
  };

  struct OutputFiles {
    base::File worker;
    base::File retained;
  };

  static OutputFiles CreateOutputFiles(base::FilePath output_root);
  static std::optional<VerifiedToolOutput> ReadAndValidateOutput(
      base::File output,
      uint64_t expected_bytes,
      core_service::mojom::TaskArtifactKind kind,
      uint32_t frame_count,
      std::vector<uint8_t> expected_digest);
  static OperationIdentity CaptureOperation(
      const core_service::mojom::OperationEnvelope& operation);
  static bool MatchesOperation(
      const OperationIdentity& expected,
      const core_service::mojom::OperationEnvelope& actual);
  static bool OperationsMatch(
      const core_service::mojom::OperationEnvelope& expected,
      const core_service::mojom::OperationEnvelope& actual);

  void OnInputOpened(core_service::mojom::TaskEffectBindingPtr binding,
                     PrepareCallback callback,
                     base::File input);
  void OnOutputCreated(core_service::mojom::TaskEffectBindingPtr binding,
                       PrepareCallback callback,
                       base::File input,
                       OutputFiles output);
  void AdmitPrepared(core_service::mojom::TaskEffectBindingPtr binding,
                     PrepareCallback callback,
                     base::File input,
                     OutputFiles output);
  void OnMediaOutputValidated(std::string job_id,
                              uint64_t expected_bytes,
                              RetainCallback callback,
                              std::optional<VerifiedToolOutput> verified);
  void OnPythonOutputValidated(
      core_service::mojom::TaskEffectBindingPtr binding,
      RetainCallback callback,
      std::optional<VerifiedToolOutput> verified);
  bool CanAdmitSource(const std::string& task_id, bool replacing) const;
  bool CanReservePendingJob(const std::string& task_id) const;
  bool CanRetainArtifact(const std::string& task_id, size_t bytes) const;
  void ReleaseTaskJobs(const std::string& task_id,
                       uint64_t service_generation);
  void ReleaseTaskSources(const std::string& task_id,
                          uint64_t service_generation);
  void ReleaseTaskArtifacts(const std::string& task_id,
                            uint64_t service_generation);
  void CleanupPendingMediaJob(
      base::flat_map<std::string, PendingMediaJob>::iterator pending);
  core_service::mojom::TaskToolOutputReceiptPtr RetainVerifiedArtifact(
      const core_service::mojom::TaskEffectBinding& binding,
      std::string source_id,
      core_service::mojom::TaskArtifactKind kind,
      std::vector<uint8_t> digest,
      std::vector<uint8_t> content);

  const raw_ptr<ProfileToolSupervisor> supervisor_;
  const scoped_refptr<ProfileToolHandleStore> handles_;
  const base::FilePath output_root_;
  const bool private_profile_;
  uint64_t active_generation_ = 1u;
  size_t artifact_bytes_ = 0u;

  base::flat_map<std::pair<std::string, std::string>, SourceEntry> sources_;
  base::flat_map<std::string, PendingMediaJob> pending_media_;
  base::flat_map<std::string, PendingPythonValidation> pending_python_;
  base::flat_map<std::pair<std::string, std::string>, ArtifactEntry> artifacts_;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<ProfileToolArtifactBroker> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_PROFILE_TOOL_ARTIFACT_BROKER_H_
