// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "taffy/browser/core_api/profile_core_api_facade.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;
namespace service = core_service::mojom;

std::optional<api::TaskArtifactKind> ProjectArtifactKind(
    service::TaskArtifactKind kind) {
  switch (kind) {
    case service::TaskArtifactKind::kMarkdown:
      return api::TaskArtifactKind::kMarkdown;
    case service::TaskArtifactKind::kCsv:
      return api::TaskArtifactKind::kCsv;
    case service::TaskArtifactKind::kXlsx:
      return api::TaskArtifactKind::kXlsx;
    case service::TaskArtifactKind::kPdf:
      return api::TaskArtifactKind::kPdf;
    case service::TaskArtifactKind::kDocx:
      return api::TaskArtifactKind::kDocx;
    case service::TaskArtifactKind::kPptx:
      return api::TaskArtifactKind::kPptx;
    case service::TaskArtifactKind::kWaveAudio:
      return api::TaskArtifactKind::kWaveAudio;
    case service::TaskArtifactKind::kFrameArchive:
      return api::TaskArtifactKind::kFrameArchive;
  }
  return std::nullopt;
}

uint64_t NowMonotonicMillis() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value < 0 ? 0u : static_cast<uint64_t>(value);
}

}  // namespace

void ProfileCoreApiFacade::AcceptTaskArtifact(
    const std::string& task_id,
    const std::string& artifact_id,
    AcceptTaskArtifactCallback callback) {
  if (!manager_) {
    std::move(callback).Run(SubmissionStatus::kCoreUnavailable);
    return;
  }
  const std::optional<uint64_t> revision = manager_->FindTaskRevision(task_id);
  if (!revision) {
    std::move(callback).Run(UnavailableOrStaleRevision());
    return;
  }
  CoreApiCommandFactory factory = NewFactory();
  SubmitProjected(factory.BuildAcceptTaskArtifact(
                      task_id, artifact_id, *revision,
                      manager_->service_generation(), NowMonotonicMillis()),
                  std::move(callback));
}

void ProfileCoreApiFacade::RequestTaskArtifactExport(
    const std::string& request_id,
    const std::string& task_id,
    const std::string& artifact_id,
    api::TaskArtifactKind kind,
    RequestTaskArtifactExportCallback callback) {
  if (!manager_) {
    std::move(callback).Run(SubmissionStatus::kCoreUnavailable);
    return;
  }
  const std::optional<uint64_t> revision = manager_->FindTaskRevision(task_id);
  if (!revision) {
    std::move(callback).Run(UnavailableOrStaleRevision());
    return;
  }
  CoreApiCommandFactory factory = NewFactory();
  std::optional<ProjectedCoreCommand> projected =
      factory.BuildRequestTaskArtifactExport(
          request_id, task_id, artifact_id, kind, *revision,
          manager_->service_generation(), NowMonotonicMillis());
  if (!projected || !projected->core_service_command) {
    std::move(callback).Run(SubmissionStatus::kInvalidRequest);
    return;
  }

  const base::TimeTicks now = base::TimeTicks::Now();
  for (auto pending = pending_task_artifact_exports_.begin();
       pending != pending_task_artifact_exports_.end();) {
    if (pending->second.expires_at <= now) {
      pending = pending_task_artifact_exports_.erase(pending);
    } else {
      ++pending;
    }
  }
  if (pending_task_artifact_exports_.contains(request_id)) {
    std::move(callback).Run(SubmissionStatus::kDuplicate);
    return;
  }
  if (pending_task_artifact_exports_.size() >=
      service::kMaxInFlightPerProfile) {
    std::move(callback).Run(SubmissionStatus::kBackpressure);
    return;
  }
  pending_task_artifact_exports_.emplace(
      request_id, PendingTaskArtifactExport{
                      task_id, artifact_id, kind, now + base::Seconds(30)});
  manager_->Submit(
      std::move(projected->core_service_command),
      base::BindOnce(&ProfileCoreApiFacade::OnTaskArtifactExportSubmitted,
                     weak_factory_.GetWeakPtr(), request_id,
                     std::move(callback)));
}

void ProfileCoreApiFacade::OnTaskArtifactExportSubmitted(
    std::string request_id,
    RequestTaskArtifactExportCallback callback,
    service::AdmissionPtr admission) {
  const SubmissionStatus status =
      admission ? ProjectAdmission(admission->status)
                : SubmissionStatus::kCoreUnavailable;
  if (status != SubmissionStatus::kAccepted) {
    pending_task_artifact_exports_.erase(request_id);
  }
  std::move(callback).Run(status);
}

bool ProfileCoreApiFacade::OnTaskArtifactExport(
    const std::string& task_id,
    const std::string& artifact_id,
    service::TaskArtifactKind kind,
    const std::vector<uint8_t>& content) {
  const std::optional<api::TaskArtifactKind> api_kind =
      ProjectArtifactKind(kind);
  if (!api_kind || !observer_.is_bound() || content.empty() ||
      content.size() > api::kMaxTaskArtifactExportBytes) {
    return false;
  }
  bool delivered = false;
  const base::TimeTicks now = base::TimeTicks::Now();
  for (auto pending = pending_task_artifact_exports_.begin();
       pending != pending_task_artifact_exports_.end();) {
    const bool matches = pending->second.expires_at > now &&
                         pending->second.task_id == task_id &&
                         pending->second.artifact_id == artifact_id &&
                         pending->second.kind == *api_kind;
    if (!matches) {
      if (pending->second.expires_at <= now) {
        pending = pending_task_artifact_exports_.erase(pending);
      } else {
        ++pending;
      }
      continue;
    }
    observer_->OnTaskArtifactExport(pending->first, task_id, artifact_id,
                                    *api_kind, content);
    pending = pending_task_artifact_exports_.erase(pending);
    delivered = true;
  }
  return delivered;
}

}  // namespace taffy
