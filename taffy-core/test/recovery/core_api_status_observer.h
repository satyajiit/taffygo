// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_RECOVERY_CORE_API_STATUS_OBSERVER_H_
#define TAFFY_TEST_RECOVERY_CORE_API_STATUS_OBSERVER_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/test/recovery/observed_site_skill.h"

namespace taffy::test {

struct ObservedTaskStatus {
  std::string task_id;
  uint64_t revision = 0u;
  core_api::mojom::TaskPhase phase = core_api::mojom::TaskPhase::kIdle;
  // Present only when the production CoreStatus exposes a proposal for a
  // visible approval decision. Tests return this opaque identity through the
  // generated Core API; they never invent an action id.
  std::optional<std::string> pending_action_id;
  // Set only when the immutable task projection has attached its durable
  // workspace. Like the task id, this is learned from CoreStatus.
  std::optional<std::string> workspace_id;
  // The published status message names a handover; its private identity stays
  // with the browser. The Core API returns control using the task identity.
  bool waiting_for_handover = false;
  std::optional<core_api::mojom::CoreFailureCode> failure_code;
};

struct ObservedWorkspaceSource {
  std::string source_id;
  std::string title;
  std::string host;
  uint64_t read_at_epoch_ms = 0u;
  uint32_t fact_count = 0u;
  bool excluded = false;
};

struct ObservedWorkspaceFact {
  std::string fact_id;
  std::string field;
  std::string value;
  core_api::mojom::WorkspaceFactKind kind =
      core_api::mojom::WorkspaceFactKind::kFromPage;
  std::vector<std::string> sources;
  std::optional<std::string> correction;
  bool has_conflict = false;
  bool needs_new_source = false;
};

struct ObservedWorkspaceDeletionPreview {
  uint32_t sources = 0u;
  uint32_t facts = 0u;
  uint32_t artifact_metadata = 0u;
  uint32_t derived_indexes = 0u;
  std::string confirmation_token;
};

struct ObservedWorkspaceStatus {
  std::string workspace_id;
  uint64_t revision = 0u;
  std::string goal;
  core_api::mojom::WorkspacePhase phase =
      core_api::mojom::WorkspacePhase::kRunning;
  uint64_t last_updated_epoch_ms = 0u;
  core_api::mojom::TaskTemplateId template_id =
      core_api::mojom::TaskTemplateId::kCompareProducts;
  std::vector<ObservedWorkspaceSource> sources;
  std::vector<ObservedWorkspaceFact> facts;
  bool saved = false;
  std::string display_name;
  std::optional<ObservedWorkspaceDeletionPreview> deletion_preview;
};

struct ObservedWorkspaceExport {
  std::string request_id;
  std::string workspace_id;
  uint64_t revision = 0u;
  core_api::mojom::WorkspaceExportFormat format =
      core_api::mojom::WorkspaceExportFormat::kMarkdown;
  std::string content;
};

struct ObservedLibrarySource {
  std::string source_id;
  std::string title;
  std::string host;
  uint64_t observed_at_epoch_ms = 0u;
};

struct ObservedLibraryEntry {
  std::string entry_id;
  uint64_t revision = 0u;
  std::string collection_id;
  std::string collection_name;
  std::string source_workspace_id;
  uint64_t source_workspace_revision = 0u;
  std::string source_fact_id;
  std::string field;
  std::string original_value;
  std::optional<std::string> correction;
  core_api::mojom::WorkspaceFactKind kind =
      core_api::mojom::WorkspaceFactKind::kFromPage;
  std::vector<ObservedLibrarySource> sources;
  uint64_t captured_at_epoch_ms = 0u;
  uint64_t last_checked_epoch_ms = 0u;
  bool has_conflict = false;
};

struct ObservedLibrarySearchHit {
  std::string entry_id;
  uint64_t age_ms = 0u;
};

struct ObservedLibrarySearch {
  std::string request_id;
  std::string query;
  uint64_t library_revision = 0u;
  std::vector<ObservedLibrarySearchHit> hits;
};

struct ObservedLibraryStatus {
  core_api::mojom::LibraryAvailability availability =
      core_api::mojom::LibraryAvailability::kUnavailable;
  uint64_t revision = 0u;
  std::vector<ObservedLibraryEntry> entries;
  std::optional<ObservedLibrarySearch> search;
};

struct ObservedLibraryExport {
  std::string request_id;
  uint64_t library_revision = 0u;
  std::optional<std::string> collection_id;
  core_api::mojom::WorkspaceExportFormat format =
      core_api::mojom::WorkspaceExportFormat::kMarkdown;
  std::string content;
};

struct ObservedMemoryWorkspace {
  std::string workspace_id;
  std::string display_name;
};

struct ObservedMemoryRecord {
  std::string memory_id;
  uint64_t revision = 0u;
  std::string statement;
  core_api::mojom::MemorySourceKind source_kind =
      core_api::mojom::MemorySourceKind::kYouWrote;
  std::optional<std::string> source_task_id;
  std::optional<ObservedMemoryWorkspace> source_workspace;
  core_api::mojom::MemoryScopeKind scope_kind =
      core_api::mojom::MemoryScopeKind::kAllTasks;
  std::optional<ObservedMemoryWorkspace> scope_workspace;
  core_api::mojom::MemorySensitivity sensitivity =
      core_api::mojom::MemorySensitivity::kStandard;
  uint64_t created_at_epoch_ms = 0u;
  uint64_t updated_at_epoch_ms = 0u;
  uint64_t reviewed_at_epoch_ms = 0u;
  uint64_t expires_at_epoch_ms = 0u;
};

struct ObservedMemorySearch {
  std::string request_id;
  std::string query;
  uint64_t memory_revision = 0u;
  std::vector<std::string> memory_ids;
};

struct ObservedMemoryStatus {
  core_api::mojom::MemoryAvailability availability =
      core_api::mojom::MemoryAvailability::kUnavailable;
  uint64_t revision = 0u;
  std::vector<ObservedMemoryRecord> records;
  std::optional<ObservedMemorySearch> search;
};

// Strictly reads browser-minted task identities from the production generated
// CoreStatus payload delivered through the Android Core API observer.
class CoreApiStatusObserver final
    : public core_api::mojom::TaffyProfileCoreApiObserver {
 public:
  CoreApiStatusObserver();
  CoreApiStatusObserver(const CoreApiStatusObserver&) = delete;
  CoreApiStatusObserver& operator=(const CoreApiStatusObserver&) = delete;
  ~CoreApiStatusObserver() override;

  mojo::PendingRemote<core_api::mojom::TaffyProfileCoreApiObserver>
  BindNewPipeAndPassRemote();

  std::optional<ObservedTaskStatus> only_task() const;
  // Returns the one task whose browser-minted identity differs from the
  // supplied task, or no task when the snapshot does not contain exactly one
  // such row. This keeps multi-task verticals independent of projection
  // ordering and prevents a test from guessing an identity.
  std::optional<ObservedTaskStatus> task_other_than(
      const std::string& task_id) const;
  std::optional<ObservedTaskStatus> only_task_in_phase(
      core_api::mojom::TaskPhase phase) const;
  std::optional<ObservedWorkspaceStatus> only_workspace() const;
  std::optional<ObservedWorkspaceExport> workspace_export() const {
    return workspace_export_;
  }
  const ObservedLibraryStatus& library() const { return library_; }
  std::optional<ObservedLibraryExport> library_export() const {
    return library_export_;
  }
  const ObservedMemoryStatus& memory() const { return memory_; }
  const std::vector<ObservedSiteSkill>& site_skills() const {
    return site_skills_;
  }
  bool site_skills_complete() const { return site_skills_complete_; }
  void SetFirstTaskCallback(
      base::OnceCallback<void(ObservedTaskStatus)> callback);
  // A matching binary payload must pass the strict reader before the trusted
  // watermark advances. A payload-less availability update can withdraw
  // readiness without replacing that last decoded watermark.
  bool snapshot_seen() const { return snapshot_seen_; }
  core_api::mojom::CoreAvailability availability() const {
    return availability_;
  }
  uint64_t service_generation() const { return service_generation_; }
  uint64_t state_sequence() const { return state_sequence_; }
  bool malformed_payload_seen() const { return malformed_payload_seen_; }
  bool permission_request_seen() const { return permission_request_seen_; }

 private:
  void OnSnapshot(
      core_api::mojom::CoreAvailability availability,
      uint64_t service_generation,
      uint64_t state_sequence,
      uint32_t status_schema_version,
      const std::optional<std::vector<uint8_t>>& status_payload) override;
  void OnPermissionRequest(
      const std::string& request_id,
      core_api::mojom::PlatformPermission permission) override;
  // Two pushes this suite is not about, implemented because the interface is
  // not optional: a transfer's progress and a composer suggestion. Recording
  // either would be recording something the recovery narrative never drives.
  void OnAssetProgress(const std::string& asset_id,
                       const std::string& asset_revision,
                       uint64_t written_bytes,
                       uint64_t total_bytes) override {}
  void OnComposerCompletion(const std::string& request_id,
                            const std::optional<std::string>& text) override {}
  void OnTaskAnswerDelta(const std::string& task_id,
                         const std::string& call_id,
                         uint32_t sequence,
                         const std::optional<std::string>& text,
                         bool terminal,
                         bool complete) override {}
  void OnTaskArtifactExport(const std::string& request_id,
                            const std::string& task_id,
                            const std::string& artifact_id,
                            core_api::mojom::TaskArtifactKind kind,
                            const std::vector<uint8_t>& content) override {}

  mojo::Receiver<core_api::mojom::TaffyProfileCoreApiObserver> receiver_{this};
  std::vector<ObservedTaskStatus> tasks_;
  std::vector<ObservedWorkspaceStatus> workspaces_;
  std::optional<ObservedWorkspaceExport> workspace_export_;
  ObservedLibraryStatus library_;
  std::optional<ObservedLibraryExport> library_export_;
  ObservedMemoryStatus memory_;
  std::vector<ObservedSiteSkill> site_skills_;
  bool site_skills_complete_ = false;
  base::OnceCallback<void(ObservedTaskStatus)> first_task_callback_;
  core_api::mojom::CoreAvailability availability_ =
      core_api::mojom::CoreAvailability::kStarting;
  uint64_t service_generation_ = 0u;
  uint64_t state_sequence_ = 0u;
  bool snapshot_seen_ = false;
  bool malformed_payload_seen_ = false;
  bool permission_request_seen_ = false;
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_RECOVERY_CORE_API_STATUS_OBSERVER_H_
