// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"

#include <algorithm>
#include <string_view>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/contracts/tool-runtime/generated/mojom/tool_runtime.mojom.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor_job_record.h"
#include "taffy/services/tool-runtime/supervisor/tool_contract_conversion.h"
#include "taffy/services/tool-runtime/supervisor/tool_job_client.h"
#include "taffy/services/tool-runtime/supervisor/tool_job_resources.h"

namespace taffy {
namespace {

namespace core = core_service::mojom;
namespace runtime = tool_runtime::mojom;

uint64_t MonotonicMilliseconds() {
  const int64_t value = base::TimeTicks::Now().since_origin().InMilliseconds();
  return value > 0 ? static_cast<uint64_t>(value) : 0u;
}

core::ToolTerminalStatus AdmissionTerminal(
    runtime::ToolAdmissionStatus status) {
  switch (status) {
    case runtime::ToolAdmissionStatus::kAccepted:
      return core::ToolTerminalStatus::kInvalidInput;
    case runtime::ToolAdmissionStatus::kUnsupported:
      return core::ToolTerminalStatus::kUnsupported;
    case runtime::ToolAdmissionStatus::kInvalidJob:
      return core::ToolTerminalStatus::kInvalidInput;
    case runtime::ToolAdmissionStatus::kBackpressure:
      return core::ToolTerminalStatus::kResourceLimit;
    case runtime::ToolAdmissionStatus::kDeadlineExceeded:
      return core::ToolTerminalStatus::kDeadlineExceeded;
    case runtime::ToolAdmissionStatus::kModelArtifactMissing:
      return core::ToolTerminalStatus::kModelArtifactMissing;
    case runtime::ToolAdmissionStatus::kModelArtifactIncompatible:
      return core::ToolTerminalStatus::kModelArtifactIncompatible;
    case runtime::ToolAdmissionStatus::kLocalRuntimeUnavailable:
      return core::ToolTerminalStatus::kLocalRuntimeUnavailable;
  }
  return core::ToolTerminalStatus::kInvalidInput;
}

core::ToolTerminalStatus BindTerminal(tool_job_resources::BindResult result) {
  switch (result) {
    case tool_job_resources::BindResult::kBound:
    case tool_job_resources::BindResult::kInvalidJob:
      return core::ToolTerminalStatus::kInvalidInput;
    case tool_job_resources::BindResult::kModelArtifactMissing:
      return core::ToolTerminalStatus::kModelArtifactMissing;
    case tool_job_resources::BindResult::kModelArtifactIncompatible:
      return core::ToolTerminalStatus::kModelArtifactIncompatible;
  }
  return core::ToolTerminalStatus::kInvalidInput;
}

}  // namespace

ProfileToolSupervisor::ProfileToolSupervisor(uint64_t service_generation,
                                             PythonPorts python,
                                             LocalModelPorts local_model,
                                             MediaPorts media)
    : active_generation_(service_generation),
      python_(std::move(python)),
      local_model_(std::move(local_model)),
      media_(std::move(media)) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

ProfileToolSupervisor::~ProfileToolSupervisor() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Shutdown();
}

void ProfileToolSupervisor::Start(core::OperationEnvelopePtr operation,
                                  std::string effect_id,
                                  core::ToolJobEffectPtr job,
                                  CompletionCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  runtime::ToolJobPtr runtime_job;
  runtime::ToolJobResourcesPtr resources;
  const std::string job_id = job ? job->job_id : std::string();
  if (shutting_down_) {
    FinishImmediate(job_id, core::ToolTerminalStatus::kCancelled,
                    std::move(callback));
    return;
  }
  if (!operation || !job || effect_id.empty() || effect_id.size() > 256u ||
      operation->service_generation != active_generation_ ||
      !tool_contract_conversion::ConvertJob(
          *operation, *job, !stream_sink_.is_null(), &runtime_job) ||
      HasActiveIdentityConflict(*operation, effect_id, *job)) {
    FinishImmediate(job_id, core::ToolTerminalStatus::kInvalidInput,
                    std::move(callback));
    return;
  }
  const uint64_t now = MonotonicMilliseconds();
  if (operation->deadline_monotonic_ms <= now) {
    FinishImmediate(job_id, core::ToolTerminalStatus::kDeadlineExceeded,
                    std::move(callback));
    return;
  }
  if (job->runtime == core::ToolRuntimeKind::kWasm) {
    FinishImmediate(job_id, core::ToolTerminalStatus::kUnsupported,
                    std::move(callback));
    return;
  }
  StartPort* const port = StartPortFor(job->runtime);
  if (!port) {
    FinishImmediate(job_id, core::ToolTerminalStatus::kUnsupported,
                    std::move(callback));
    return;
  }
  if (jobs_.size() >= kMaxConcurrentJobs) {
    FinishImmediate(job_id, core::ToolTerminalStatus::kResourceLimit,
                    std::move(callback));
    return;
  }
  // A declared transport whose browser side does not exist is refused rather
  // than downgraded. The plan never selects the pipe today; this is the check
  // that keeps that true if a future plan does before the drain lands.
  if (runtime_job->output_payload->transport ==
      runtime::ToolOutputTransport::kDataPipe) {
    FinishImmediate(job_id, core::ToolTerminalStatus::kUnsupported,
                    std::move(callback));
    return;
  }
  // Every resource the job named is opened here, before the worker exists.
  // Failing here means the job never starts, because a job that named a model
  // and did not get one would otherwise run against nothing and still report.
  const tool_job_resources::BindResult binding = tool_job_resources::Bind(
      model_artifact_port_, python_library_port_, &handle_broker_,
      runtime_job.get(), &resources);
  if (binding != tool_job_resources::BindResult::kBound) {
    FinishImmediate(job_id, BindTerminal(binding), std::move(callback));
    return;
  }

  auto record = std::make_unique<JobRecord>();
  record->effect_id = std::move(effect_id);
  record->idempotency_key = operation->idempotency_key;
  record->runtime_kind = job->runtime;
  record->job = job->Clone();
  record->core_operation = operation->Clone();
  record->runtime_operation = runtime_job->operation->Clone();
  record->streaming = runtime_job->output_payload->transport ==
                      runtime::ToolOutputTransport::kStreamChunks;
  record->callback = std::move(callback);
  record->client = std::make_unique<ToolJobClient>(
      base::BindRepeating(&ProfileToolSupervisor::OnProgress,
                          weak_factory_.GetWeakPtr(), job_id),
      base::BindRepeating(&ProfileToolSupervisor::OnChunk,
                          weak_factory_.GetWeakPtr(), job_id),
      base::BindRepeating(&ProfileToolSupervisor::OnCompleted,
                          weak_factory_.GetWeakPtr(), job_id),
      base::BindOnce(&ProfileToolSupervisor::OnWorkerDisconnected,
                     weak_factory_.GetWeakPtr(), job_id));
  auto remote = record->client->BindNewPipeAndPassRemote();
  const uint64_t remaining = operation->deadline_monotonic_ms - now;
  const uint64_t bounded =
      std::min(remaining, static_cast<uint64_t>(runtime::kMaxJobWallTimeMs));
  record->deadline.Start(FROM_HERE, base::Milliseconds(bounded),
                         base::BindOnce(&ProfileToolSupervisor::OnDeadline,
                                        weak_factory_.GetWeakPtr(), job_id));
  jobs_.emplace(job_id, std::move(record));
  port->Run(std::move(runtime_job), std::move(resources), std::move(remote),
            base::BindOnce(&ProfileToolSupervisor::OnAdmission,
                           weak_factory_.GetWeakPtr(), job_id));
}

void ProfileToolSupervisor::SetStreamSink(StreamCallback sink) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  stream_sink_ = std::move(sink);
}

void ProfileToolSupervisor::SetResourcePorts(
    ToolHandleBroker::ResolvePort resolve_handle,
    tool_job_resources::ModelArtifactPort open_artifact) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  handle_broker_.SetResolvePort(std::move(resolve_handle));
  model_artifact_port_ = std::move(open_artifact);
}

void ProfileToolSupervisor::SetPythonLibraryPort(
    tool_job_resources::PythonLibraryPort open_library) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  python_library_port_ = std::move(open_library);
}

void ProfileToolSupervisor::Cancel(const std::string& job_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Finish(job_id, core::ToolTerminalStatus::kCancelled, nullptr, true);
}

void ProfileToolSupervisor::CancelTask(std::string_view task_id,
                                       uint64_t service_generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (task_id.empty() || service_generation != active_generation_) {
    return;
  }
  // Collected before any of them is finished. Finish() erases from `jobs_`
  // and runs a callback the core may re-enter this supervisor from, so
  // iterating the map while cancelling would be walking a container the
  // cancellation is editing.
  std::vector<std::string> claimed;
  for (const auto& entry : jobs_) {
    if (entry.second->job->task_id == task_id) {
      claimed.push_back(entry.first);
    }
  }
  for (const auto& job_id : claimed) {
    Finish(job_id, core::ToolTerminalStatus::kCancelled, nullptr, true);
  }
}

void ProfileToolSupervisor::SetActiveGeneration(uint64_t generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (generation == active_generation_) {
    return;
  }
  active_generation_ = generation;
  handle_broker_.RevokeAll();
  std::vector<std::string> ids;
  ids.reserve(jobs_.size());
  for (const auto& entry : jobs_) {
    ids.push_back(entry.first);
  }
  for (const auto& job_id : ids) {
    Finish(job_id, core::ToolTerminalStatus::kCancelled, nullptr, true);
  }
}

void ProfileToolSupervisor::Shutdown() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (shutting_down_) {
    return;
  }
  shutting_down_ = true;
  handle_broker_.RevokeAll();
  std::vector<std::string> ids;
  ids.reserve(jobs_.size());
  for (const auto& entry : jobs_) {
    ids.push_back(entry.first);
  }
  for (const auto& job_id : ids) {
    Finish(job_id, core::ToolTerminalStatus::kCancelled, nullptr, true);
  }
}

bool ProfileToolSupervisor::HasActiveJobs() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return !jobs_.empty();
}

size_t ProfileToolSupervisor::active_job_count_for_testing() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return jobs_.size();
}

void ProfileToolSupervisor::OnAdmission(const std::string& job_id,
                                        runtime::ToolAdmissionPtr admission) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = jobs_.find(job_id);
  if (found == jobs_.end()) {
    ++rejected_message_count_;
    return;
  }
  if (!admission || admission->job_id != job_id) {
    ++rejected_message_count_;
    Finish(job_id, core::ToolTerminalStatus::kInvalidInput, nullptr, true);
    return;
  }
  if (admission->status != runtime::ToolAdmissionStatus::kAccepted) {
    Finish(job_id, AdmissionTerminal(admission->status), nullptr, false);
    return;
  }
  found->second->admitted = true;
}

void ProfileToolSupervisor::OnCompleted(const std::string& job_id,
                                        runtime::ToolCompletionPtr completion) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = jobs_.find(job_id);
  core::ToolTerminalStatus status;
  core::ToolSuccessPtr success;
  size_t bytes = 0u;
  if (found == jobs_.end()) {
    ++rejected_message_count_;
    return;
  }
  JobRecord& record = *found->second;
  if (!record.admitted || !completion ||
      !tool_contract_conversion::ConvertCompletion(
          *completion, *record.runtime_operation, *record.job, &status,
          &success, &bytes) ||
      bytes > record.job->budget->max_output_bytes - record.output_bytes) {
    ++rejected_message_count_;
    Finish(job_id, core::ToolTerminalStatus::kInvalidInput, nullptr, true);
    return;
  }
  Finish(job_id, status, std::move(success), false);
}

void ProfileToolSupervisor::OnWorkerDisconnected(const std::string& job_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Finish(job_id, core::ToolTerminalStatus::kRuntimeCrashed, nullptr, false);
}

void ProfileToolSupervisor::OnDeadline(const std::string& job_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Finish(job_id, core::ToolTerminalStatus::kDeadlineExceeded, nullptr, true);
}

bool ProfileToolSupervisor::HasActiveIdentityConflict(
    const core::OperationEnvelope& operation,
    const std::string& effect_id,
    const core::ToolJobEffect& job) const {
  if (jobs_.contains(job.job_id)) {
    return true;
  }
  for (const auto& entry : jobs_) {
    if (entry.second->effect_id == effect_id ||
        entry.second->idempotency_key == operation.idempotency_key) {
      return true;
    }
  }
  return false;
}

ProfileToolSupervisor::StartPort* ProfileToolSupervisor::StartPortFor(
    core::ToolRuntimeKind runtime_kind) {
  switch (runtime_kind) {
    case core::ToolRuntimeKind::kPython:
      return &python_.start;
    case core::ToolRuntimeKind::kLocalModel:
      return &local_model_.start;
    case core::ToolRuntimeKind::kMedia:
      return &media_.start;
    case core::ToolRuntimeKind::kWasm:
      return nullptr;
  }
  return nullptr;
}

void ProfileToolSupervisor::CancelRuntime(core::ToolRuntimeKind runtime_kind,
                                          const std::string& job_id) {
  CancelPort* port = nullptr;
  switch (runtime_kind) {
    case core::ToolRuntimeKind::kPython:
      port = &python_.cancel;
      break;
    case core::ToolRuntimeKind::kLocalModel:
      port = &local_model_.cancel;
      break;
    case core::ToolRuntimeKind::kMedia:
      port = &media_.cancel;
      break;
    case core::ToolRuntimeKind::kWasm:
      return;
  }
  port->Run(job_id);
}

void ProfileToolSupervisor::Finish(const std::string& job_id,
                                   core::ToolTerminalStatus status,
                                   core::ToolSuccessPtr success,
                                   bool cancel_worker) {
  auto found = jobs_.find(job_id);
  if (found == jobs_.end()) {
    return;
  }
  std::unique_ptr<JobRecord> record = std::move(found->second);
  jobs_.erase(found);
  if (cancel_worker) {
    CancelRuntime(record->runtime_kind, job_id);
  }
  // Identifiers do not outlive the job they were minted for, whichever way it
  // ended: a cancelled job and a completed one revoke the same way, because a
  // handle left behind is a resource still reachable.
  handle_broker_.RevokeJob(job_id);
  auto result = core::ToolEffectResult::New(
      job_id, status, std::move(record->progress), std::move(record->chunks),
      std::move(success), record->streamed_chunks);
  std::move(record->callback).Run(std::move(result));
}

void ProfileToolSupervisor::FinishImmediate(std::string job_id,
                                            core::ToolTerminalStatus status,
                                            CompletionCallback callback) {
  std::move(callback).Run(core::ToolEffectResult::New(
      std::move(job_id), status, std::vector<core::ToolProgressPtr>(),
      std::vector<core::ToolOutputChunkPtr>(), nullptr, 0u));
}

}  // namespace taffy
