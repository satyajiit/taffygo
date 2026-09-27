// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/tool-runtime/python/python_tool_service.h"

#include <algorithm>
#include <utility>

#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/task/bind_post_task.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "crypto/hash.h"

namespace taffy {
namespace mojom = tool_runtime::mojom;
namespace {

constexpr size_t kMaxJobIdBytes = 128u;
constexpr size_t kMaxEntrypointBytes = 128u;
constexpr size_t kMaxInlineBytes = 262144u;
constexpr uint64_t kMaxOutputBytes = 1024u * 1024u;
constexpr uint64_t kMaxMemoryBytes = 64u * 1024u * 1024u;
constexpr uint64_t kMaxCpuMs = 5000u;

bool IsEntrypoint(std::string_view value) {
  return value == "document.build" || value == "spreadsheet.build";
}

bool HasUnexpectedArguments(const mojom::ToolJob& job) {
  return job.local_model || job.local_embedding || job.media_probe ||
         job.audio_extract || job.frame_sample || job.transcode ||
         job.signed_wasm;
}

bool HasUnexpectedResources(const mojom::ToolJobResources& resources) {
  return resources.model_artifact.IsValid() ||
         resources.model_adapter.IsValid() || resources.bulk_input.IsValid() ||
         resources.media_input.IsValid() || resources.media_output.IsValid() ||
         resources.bulk_output.is_valid();
}

mojom::ToolTerminalStatus TerminalFor(python_tool::Failure failure) {
  switch (failure) {
    case python_tool::Failure::kInvalidInput:
      return mojom::ToolTerminalStatus::kInvalidInput;
    case python_tool::Failure::kResourceLimit:
      return mojom::ToolTerminalStatus::kResourceLimit;
    case python_tool::Failure::kDeadlineExceeded:
      return mojom::ToolTerminalStatus::kDeadlineExceeded;
    case python_tool::Failure::kCancelled:
      return mojom::ToolTerminalStatus::kCancelled;
    case python_tool::Failure::kRuntimeFailure:
      return mojom::ToolTerminalStatus::kRuntimeCrashed;
  }
}

}  // namespace

PythonToolServiceImpl::PythonToolServiceImpl(
    mojo::PendingReceiver<mojom::PythonToolService> receiver)
    : receiver_(this, std::move(receiver)),
      execution_runner_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN})) {}

PythonToolServiceImpl::~PythonToolServiceImpl() = default;

mojom::ToolAdmissionStatus PythonToolServiceImpl::Admit(
    const mojom::ToolJob& job,
    const mojom::ToolJobResources& resources) const {
  if (running_) {
    return mojom::ToolAdmissionStatus::kBackpressure;
  }
  if (!job.operation || job.job_id.empty() ||
      job.job_id.size() > kMaxJobIdBytes ||
      job.runtime != mojom::ToolRuntimeKind::kPython ||
      job.operation_kind != mojom::ToolOperation::kRunBundledPythonModule) {
    return mojom::ToolAdmissionStatus::kInvalidJob;
  }
  if (job.tool_id != "python.execute" || job.tool_version != "1" ||
      !job.bundled_python || job.bundled_python->entrypoint_id.empty() ||
      job.bundled_python->entrypoint_id.size() > kMaxEntrypointBytes ||
      !IsEntrypoint(job.bundled_python->entrypoint_id)) {
    return mojom::ToolAdmissionStatus::kUnsupported;
  }
  if (HasUnexpectedArguments(job) || !job.input_payload ||
      !job.output_payload || !job.python_library || !job.budget) {
    return mojom::ToolAdmissionStatus::kInvalidJob;
  }
  const auto& input = job.bundled_python->input;
  if (input.size() > kMaxInlineBytes ||
      job.input_payload->transport != mojom::ToolInputTransport::kInline ||
      job.input_payload->byte_length != input.size() ||
      !std::ranges::equal(job.input_payload->digest,
                          crypto::hash::Sha256(base::span(input))) ||
      job.output_payload->transport !=
          mojom::ToolOutputTransport::kInlineChunks ||
      job.output_payload->max_byte_length == 0u ||
      job.output_payload->max_byte_length > kMaxOutputBytes) {
    return mojom::ToolAdmissionStatus::kInvalidJob;
  }
  if (job.budget->max_input_bytes < input.size() ||
      job.budget->max_input_bytes > kMaxInlineBytes ||
      job.budget->max_output_bytes == 0u ||
      job.budget->max_output_bytes > job.output_payload->max_byte_length ||
      job.budget->max_memory_bytes == 0u ||
      job.budget->max_memory_bytes > kMaxMemoryBytes ||
      job.budget->max_cpu_ms == 0u || job.budget->max_cpu_ms > kMaxCpuMs ||
      job.budget->max_temporary_bytes != 0u ||
      job.budget->max_output_chunks != 1u) {
    return mojom::ToolAdmissionStatus::kInvalidJob;
  }
  if (!resources.python_library.IsValid() ||
      HasUnexpectedResources(resources) ||
      job.python_library->library_id.empty() ||
      job.python_library->library_version.empty() ||
      job.python_library->archive_bytes == 0u ||
      job.python_library->archive_bytes > 128u * 1024u * 1024u) {
    return mojom::ToolAdmissionStatus::kInvalidJob;
  }
  return mojom::ToolAdmissionStatus::kAccepted;
}

void PythonToolServiceImpl::Start(
    mojom::ToolJobPtr job,
    mojom::ToolJobResourcesPtr resources,
    mojo::PendingRemote<mojom::ToolRuntimeClient> client,
    StartCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto admission = mojom::ToolAdmission::New();
  if (!job || !resources) {
    admission->status = mojom::ToolAdmissionStatus::kInvalidJob;
    std::move(callback).Run(std::move(admission));
    return;
  }
  admission->job_id = job->job_id;
  admission->status = Admit(*job, *resources);
  const bool accepted =
      admission->status == mojom::ToolAdmissionStatus::kAccepted;
  std::move(callback).Run(std::move(admission));
  if (!accepted) {
    return;
  }

  running_ = true;
  cancelled_->data.store(false, std::memory_order_relaxed);
  job_id_ = job->job_id;
  operation_ = job->operation.Clone();
  client_.Bind(std::move(client));
  python_tool::Request request;
  request.entrypoint = job->bundled_python->entrypoint_id;
  request.input = std::move(job->bundled_python->input);
  request.library = base::File(std::move(resources->python_library));
  request.library_bytes = job->python_library->archive_bytes;
  request.library_digest = job->python_library->archive_digest;
  request.max_output_bytes = job->budget->max_output_bytes;
  request.max_memory_bytes = job->budget->max_memory_bytes;
  request.max_cpu_ms = job->budget->max_cpu_ms;
  execution_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&python_tool::Execute, std::move(request), cancelled_),
      base::BindOnce(&PythonToolServiceImpl::OnExecuted,
                     weak_factory_.GetWeakPtr(), job_id_));
}

void PythonToolServiceImpl::Cancel(const std::string& job_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!running_ || job_id != job_id_) {
    return;
  }
  cancelled_->data.store(true, std::memory_order_relaxed);
  Publish(mojom::ToolTerminalStatus::kCancelled, nullptr);
}

void PythonToolServiceImpl::OnExecuted(std::string job_id,
                                       python_tool::Result result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!running_ || job_id != job_id_) {
    return;
  }
  if (!result.has_value()) {
    Publish(TerminalFor(result.error()), nullptr);
    return;
  }
  auto python = mojom::BundledPythonResult::New();
  python->output = std::move(result.value());
  auto success = mojom::ToolSuccess::New();
  success->operation_kind = mojom::ToolOperation::kRunBundledPythonModule;
  success->bundled_python = std::move(python);
  Publish(mojom::ToolTerminalStatus::kCompleted, std::move(success));
}

void PythonToolServiceImpl::Publish(mojom::ToolTerminalStatus status,
                                    mojom::ToolSuccessPtr success) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!running_) {
    return;
  }
  running_ = false;
  auto completion = mojom::ToolCompletion::New();
  completion->operation = std::move(operation_);
  completion->job_id = job_id_;
  completion->status = status;
  completion->success = std::move(success);
  if (client_) {
    client_->Completed(std::move(completion));
  }
  receiver_.reset();
}

}  // namespace taffy
