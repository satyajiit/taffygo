// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <utility>

#include "taffy/browser/profile_tool_artifact_broker.h"
#include "taffy/browser/profile_tool_handle_store.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"

namespace taffy {
namespace {

constexpr size_t kMaxSources = 32u;
constexpr size_t kMaxSourcesPerTask = 8u;
constexpr size_t kMaxPendingJobs = 8u;
constexpr size_t kMaxPendingJobsPerTask = 2u;
constexpr size_t kMaxArtifacts = 16u;
constexpr size_t kMaxArtifactsPerTask = 4u;
constexpr size_t kMaxArtifactBytes = 64u * 1024u * 1024u;
constexpr size_t kMaxArtifactBytesPerTask = 16u * 1024u * 1024u;

}  // namespace

bool ProfileToolArtifactBroker::CanAdmitSource(const std::string& task_id,
                                               bool replacing) const {
  if (replacing) {
    return true;
  }
  const size_t task_sources =
      std::count_if(sources_.begin(), sources_.end(), [&](const auto& entry) {
        return entry.second.admission.task_id == task_id;
      });
  return sources_.size() < kMaxSources && task_sources < kMaxSourcesPerTask;
}

bool ProfileToolArtifactBroker::CanReservePendingJob(
    const std::string& task_id) const {
  const size_t pending_jobs = pending_media_.size() + pending_python_.size();
  const size_t task_media = std::count_if(
      pending_media_.begin(), pending_media_.end(),
      [&](const auto& entry) { return entry.second.task_id == task_id; });
  const size_t task_python = std::count_if(
      pending_python_.begin(), pending_python_.end(),
      [&](const auto& entry) { return entry.second.task_id == task_id; });
  return pending_jobs < kMaxPendingJobs &&
         task_media + task_python < kMaxPendingJobsPerTask;
}

bool ProfileToolArtifactBroker::CanRetainArtifact(const std::string& task_id,
                                                  size_t bytes) const {
  if (bytes == 0u || artifacts_.size() >= kMaxArtifacts ||
      bytes > kMaxArtifactBytes ||
      bytes > kMaxArtifactBytes - artifact_bytes_) {
    return false;
  }
  size_t task_artifacts = 0u;
  size_t task_bytes = 0u;
  for (const auto& entry : artifacts_) {
    if (entry.second.task_id != task_id) {
      continue;
    }
    ++task_artifacts;
    if (entry.second.content.size() > kMaxArtifactBytesPerTask - task_bytes) {
      return false;
    }
    task_bytes += entry.second.content.size();
  }
  return task_artifacts < kMaxArtifactsPerTask &&
         bytes <= kMaxArtifactBytesPerTask - task_bytes;
}

void ProfileToolArtifactBroker::CleanupPendingMediaJob(
    base::flat_map<std::string, PendingMediaJob>::iterator pending) {
  handles_->Forget(pending->second.input_handle);
  handles_->Forget(pending->second.output_handle);
  supervisor_->handle_broker().RevokeJob(pending->first);
  pending_media_.erase(pending);
}

void ProfileToolArtifactBroker::AbandonTaskToolJob(const std::string& job_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto media = pending_media_.find(job_id);
  if (media != pending_media_.end()) {
    if (media->second.async_work_in_flight) {
      handles_->Forget(media->second.input_handle);
      handles_->Forget(media->second.output_handle);
      supervisor_->handle_broker().RevokeJob(job_id);
      media->second.abandoned = true;
    } else {
      CleanupPendingMediaJob(media);
    }
  }
  const auto python = pending_python_.find(job_id);
  if (python != pending_python_.end()) {
    python->second.abandoned = true;
  }
}

void ProfileToolArtifactBroker::ReleaseTaskJobs(const std::string& task_id,
                                                uint64_t service_generation) {
  for (auto pending = pending_media_.begin();
       pending != pending_media_.end();) {
    if (pending->second.task_id != task_id ||
        pending->second.service_generation != service_generation) {
      ++pending;
      continue;
    }
    if (pending->second.async_work_in_flight) {
      handles_->Forget(pending->second.input_handle);
      handles_->Forget(pending->second.output_handle);
      supervisor_->handle_broker().RevokeJob(pending->first);
      pending->second.abandoned = true;
      ++pending;
    } else {
      handles_->Forget(pending->second.input_handle);
      handles_->Forget(pending->second.output_handle);
      supervisor_->handle_broker().RevokeJob(pending->first);
      pending = pending_media_.erase(pending);
    }
  }
  for (auto& entry : pending_python_) {
    if (entry.second.task_id == task_id &&
        entry.second.service_generation == service_generation) {
      entry.second.abandoned = true;
    }
  }
}

void ProfileToolArtifactBroker::ReleaseTaskSources(
    const std::string& task_id,
    uint64_t service_generation) {
  for (auto source = sources_.begin(); source != sources_.end();) {
    if (source->second.admission.task_id != task_id ||
        source->second.admission.service_generation != service_generation) {
      ++source;
      continue;
    }
    source = sources_.erase(source);
  }
}

void ProfileToolArtifactBroker::ReleaseTaskArtifacts(
    const std::string& task_id,
    uint64_t service_generation) {
  for (auto artifact = artifacts_.begin(); artifact != artifacts_.end();) {
    if (artifact->second.task_id != task_id ||
        artifact->second.service_generation != service_generation) {
      ++artifact;
      continue;
    }
    artifact_bytes_ -=
        std::min(artifact_bytes_, artifact->second.content.size());
    artifact = artifacts_.erase(artifact);
  }
}

void ProfileToolArtifactBroker::SettleTask(const std::string& task_id,
                                           uint64_t service_generation,
                                           TaskDisposition disposition) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ReleaseTaskJobs(task_id, service_generation);
  if (disposition == TaskDisposition::kPaused) {
    return;
  }
  ReleaseTaskSources(task_id, service_generation);
  if (disposition == TaskDisposition::kAbandoned || private_profile_) {
    ReleaseTaskArtifacts(task_id, service_generation);
  }
}

void ProfileToolArtifactBroker::SetActiveGeneration(
    uint64_t service_generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (service_generation == 0u || service_generation == active_generation_) {
    return;
  }
  active_generation_ = service_generation;
  sources_.clear();
  artifacts_.clear();
  artifact_bytes_ = 0u;
  for (auto pending = pending_media_.begin();
       pending != pending_media_.end();) {
    handles_->Forget(pending->second.input_handle);
    handles_->Forget(pending->second.output_handle);
    supervisor_->handle_broker().RevokeJob(pending->first);
    if (pending->second.async_work_in_flight) {
      pending->second.abandoned = true;
      ++pending;
    } else {
      pending = pending_media_.erase(pending);
    }
  }
  for (auto& entry : pending_python_) {
    entry.second.abandoned = true;
  }
  handles_->ForgetAll();
}

void ProfileToolArtifactBroker::Shutdown() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  weak_factory_.InvalidateWeakPtrs();
  sources_.clear();
  pending_media_.clear();
  pending_python_.clear();
  artifacts_.clear();
  artifact_bytes_ = 0u;
  handles_->ForgetAll();
  supervisor_->handle_broker().RevokeAll();
}

size_t ProfileToolArtifactBroker::source_count_for_testing() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return sources_.size();
}

size_t ProfileToolArtifactBroker::pending_job_count_for_testing() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return pending_media_.size() + pending_python_.size();
}

size_t ProfileToolArtifactBroker::artifact_count_for_testing() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return artifacts_.size();
}

size_t ProfileToolArtifactBroker::artifact_bytes_for_testing() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return artifact_bytes_;
}

}  // namespace taffy
