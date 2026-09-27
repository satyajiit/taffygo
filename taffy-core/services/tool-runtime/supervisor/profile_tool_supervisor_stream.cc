// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The worker-to-browser publication path of ProfileToolSupervisor: the two
// methods a worker can call repeatedly, and therefore the two the bounds live
// in. They are here rather than beside admission because they answer a
// different question. Admission asks whether a job may run at all; this asks,
// of every message a running worker sends, whether it is the next one, whether
// it fits, and, for a streamed job, whether it is the last.

#include <algorithm>
#include <string>
#include <utility>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/contracts/tool-runtime/generated/mojom/tool_runtime.mojom.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor_job_record.h"
#include "taffy/services/tool-runtime/supervisor/tool_contract_conversion.h"

namespace taffy {
namespace {

namespace core = core_service::mojom;
namespace runtime = tool_runtime::mojom;

}  // namespace

void ProfileToolSupervisor::OnProgress(const std::string& job_id,
                                       runtime::ToolProgressPtr progress) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = jobs_.find(job_id);
  core::ToolProgressPtr converted;
  if (found == jobs_.end()) {
    ++rejected_message_count_;
    return;
  }
  JobRecord& record = *found->second;
  if (!record.admitted || !progress ||
      !tool_contract_conversion::ConvertProgress(*progress, *record.job,
                                                 &converted) ||
      (record.have_progress &&
       converted->sequence <= record.last_progress_sequence)) {
    ++rejected_message_count_;
    Finish(job_id, core::ToolTerminalStatus::kInvalidInput, nullptr, true);
    return;
  }
  if (record.progress.size() >= runtime::kMaxProgressEvents) {
    ++rejected_message_count_;
    Finish(job_id, core::ToolTerminalStatus::kResourceLimit, nullptr, true);
    return;
  }
  record.have_progress = true;
  record.last_progress_sequence = converted->sequence;
  record.progress.push_back(std::move(converted));
}

void ProfileToolSupervisor::OnChunk(const std::string& job_id,
                                    runtime::ToolOutputChunkPtr chunk) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto found = jobs_.find(job_id);
  core::ToolOutputChunkPtr converted;
  size_t bytes = 0u;
  if (found == jobs_.end()) {
    ++rejected_message_count_;
    return;
  }
  JobRecord& record = *found->second;
  // A stream ends at the chunk that says it does. Appending after that is the
  // tempting implementation and it removes the only signal a reader has that
  // the answer is complete, so a later chunk is a defect rather than data.
  if (!record.admitted || !chunk || record.stream_ended ||
      !tool_contract_conversion::ConvertChunk(*chunk, *record.runtime_operation,
                                              *record.job, record.streaming,
                                              &converted, &bytes) ||
      (record.have_chunk &&
       converted->sequence <= record.last_chunk_sequence)) {
    ++rejected_message_count_;
    Finish(job_id, core::ToolTerminalStatus::kInvalidInput, nullptr, true);
    return;
  }
  const size_t delivered = record.streaming ? record.streamed_chunks
                                            : record.chunks.size();
  const size_t chunk_ceiling =
      record.streaming
          ? static_cast<size_t>(runtime::kMaxStreamOutputChunks)
          : std::min(static_cast<size_t>(record.job->budget->max_output_chunks),
                     static_cast<size_t>(runtime::kMaxOutputChunks));
  if (delivered >= chunk_ceiling ||
      bytes > record.job->budget->max_output_bytes - record.output_bytes) {
    ++rejected_message_count_;
    Finish(job_id, core::ToolTerminalStatus::kResourceLimit, nullptr, true);
    return;
  }
  record.have_chunk = true;
  record.last_chunk_sequence = converted->sequence;
  record.output_bytes += bytes;
  record.stream_ended = converted->is_final;
  if (!record.streaming) {
    record.chunks.push_back(std::move(converted));
    return;
  }
  // A streamed chunk leaves the browser now and is not kept: holding it as
  // well would turn a bounded stream into an unbounded buffer, which is
  // exactly what the transport exists to avoid.
  ++record.streamed_chunks;
  stream_sink_.Run(core::ToolStreamChunk::New(record.core_operation->Clone(),
                                              record.effect_id, job_id,
                                              std::move(converted)));
}

}  // namespace taffy
