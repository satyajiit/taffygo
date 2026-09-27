// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <utility>

#include "base/time/time.h"
#include "taffy/browser/core_api/profile_core_api_facade.h"

namespace taffy {
namespace {

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

uint64_t NowUtcMillis() {
  const int64_t value = base::Time::Now().InMillisecondsSinceUnixEpoch();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

}  // namespace

void ProfileCoreApiFacade::CorrectWorkspaceFact(
    const std::string& workspace_id,
    uint64_t expected_revision,
    const std::string& fact_id,
    const std::string& value,
    CorrectWorkspaceFactCallback callback) {
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(
      factory.BuildCorrectWorkspaceFact(
          workspace_id, expected_revision, fact_id, value,
          manager_ ? manager_->service_generation() : 0u, NowMonotonicMillis()),
      std::move(callback));
}

void ProfileCoreApiFacade::ExcludeWorkspaceSource(
    const std::string& workspace_id,
    uint64_t expected_revision,
    const std::string& source_id,
    ExcludeWorkspaceSourceCallback callback) {
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(
      factory.BuildExcludeWorkspaceSource(
          workspace_id, expected_revision, source_id,
          manager_ ? manager_->service_generation() : 0u, NowMonotonicMillis()),
      std::move(callback));
}

void ProfileCoreApiFacade::RequestWorkspaceExport(
    const std::string& request_id,
    const std::string& workspace_id,
    uint64_t expected_revision,
    core_api::mojom::WorkspaceExportFormat format,
    RequestWorkspaceExportCallback callback) {
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(
      factory.BuildRequestWorkspaceExport(
          request_id, workspace_id, expected_revision, format,
          manager_ ? manager_->service_generation() : 0u, NowMonotonicMillis()),
      std::move(callback));
}

void ProfileCoreApiFacade::SaveWorkspace(const std::string& workspace_id,
                                         uint64_t expected_revision,
                                         SaveWorkspaceCallback callback) {
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(
      factory.BuildSaveWorkspace(workspace_id, expected_revision,
                                 manager_ ? manager_->service_generation() : 0u,
                                 NowMonotonicMillis()),
      std::move(callback));
}

void ProfileCoreApiFacade::RenameWorkspace(const std::string& workspace_id,
                                           uint64_t expected_revision,
                                           const std::string& display_name,
                                           RenameWorkspaceCallback callback) {
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(
      factory.BuildRenameWorkspace(
          workspace_id, expected_revision, display_name,
          manager_ ? manager_->service_generation() : 0u, NowMonotonicMillis()),
      std::move(callback));
}

void ProfileCoreApiFacade::DeleteWorkspace(
    const std::string& workspace_id,
    uint64_t expected_revision,
    const std::string& confirmation_token,
    DeleteWorkspaceCallback callback) {
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(
      factory.BuildDeleteWorkspace(
          workspace_id, expected_revision, confirmation_token,
          manager_ ? manager_->service_generation() : 0u, NowMonotonicMillis()),
      std::move(callback));
}

void ProfileCoreApiFacade::DiscardWorkspace(const std::string& workspace_id,
                                            uint64_t expected_revision,
                                            DiscardWorkspaceCallback callback) {
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(
      factory.BuildDiscardWorkspace(
          workspace_id, expected_revision,
          manager_ ? manager_->service_generation() : 0u, NowMonotonicMillis()),
      std::move(callback));
}

void ProfileCoreApiFacade::SearchLibrary(const std::string& request_id,
                                         const std::string& query,
                                         uint32_t limit,
                                         SearchLibraryCallback callback) {
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(
      factory.BuildSearchLibrary(request_id, query, limit, NowUtcMillis(),
                                 manager_ ? manager_->service_generation() : 0u,
                                 NowMonotonicMillis()),
      std::move(callback));
}

void ProfileCoreApiFacade::SaveLibraryFact(const std::string& workspace_id,
                                           uint64_t expected_workspace_revision,
                                           const std::string& fact_id,
                                           uint64_t expected_library_revision,
                                           uint64_t expected_entry_revision,
                                           SaveLibraryFactCallback callback) {
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(
      factory.BuildSaveLibraryFact(
          workspace_id, expected_workspace_revision, fact_id,
          expected_library_revision, expected_entry_revision, NowUtcMillis(),
          manager_ ? manager_->service_generation() : 0u, NowMonotonicMillis()),
      std::move(callback));
}

void ProfileCoreApiFacade::RemoveLibraryEntry(
    const std::string& entry_id,
    uint64_t expected_library_revision,
    uint64_t expected_entry_revision,
    RemoveLibraryEntryCallback callback) {
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(
      factory.BuildRemoveLibraryEntry(
          entry_id, expected_library_revision, expected_entry_revision,
          NowUtcMillis(), manager_ ? manager_->service_generation() : 0u,
          NowMonotonicMillis()),
      std::move(callback));
}

void ProfileCoreApiFacade::RequestLibraryExport(
    const std::string& request_id,
    uint64_t expected_library_revision,
    const std::optional<std::string>& collection_id,
    core_api::mojom::WorkspaceExportFormat format,
    RequestLibraryExportCallback callback) {
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(
      factory.BuildRequestLibraryExport(
          request_id, expected_library_revision, collection_id, format,
          manager_ ? manager_->service_generation() : 0u, NowMonotonicMillis()),
      std::move(callback));
}

void ProfileCoreApiFacade::StartLibraryRefresh(
    const std::string& preview_id,
    const std::string& collection_id,
    uint64_t expected_library_revision,
    uint64_t expected_workspace_revision,
    uint32_t source_count,
    StartLibraryRefreshCallback callback) {
  if (!manager_) {
    std::move(callback).Run(SubmissionStatus::kCoreUnavailable);
    return;
  }
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(
      factory.BuildStartLibraryRefresh(
          preview_id, collection_id, expected_library_revision,
          expected_workspace_revision, source_count,
          manager_->browser_session_id(), manager_->service_generation(),
          NowMonotonicMillis()),
      std::move(callback));
}

}  // namespace taffy
