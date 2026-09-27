// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_TOOL_RUNTIME_SUPERVISOR_PROFILE_TOOL_SUPERVISOR_JOB_RECORD_H_
#define TAFFY_SERVICES_TOOL_RUNTIME_SUPERVISOR_PROFILE_TOOL_SUPERVISOR_JOB_RECORD_H_

// The supervisor's private per-job state. It lives in a header of its own
// because the class is implemented across two translation units - admission in
// profile_tool_supervisor.cc, and the worker-to-browser publication path in
// profile_tool_supervisor_stream.cc - and both need the whole record. Nothing
// outside this directory includes it.

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "base/timer/timer.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/contracts/tool-runtime/generated/mojom/tool_runtime.mojom.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"
#include "taffy/services/tool-runtime/supervisor/tool_job_client.h"

namespace taffy {

struct ProfileToolSupervisor::JobRecord {
  std::string effect_id;
  std::string idempotency_key;
  core_service::mojom::ToolRuntimeKind runtime_kind;
  core_service::mojom::ToolJobEffectPtr job;
  core_service::mojom::OperationEnvelopePtr core_operation;
  tool_runtime::mojom::OperationEnvelopePtr runtime_operation;
  CompletionCallback callback;
  std::unique_ptr<ToolJobClient> client;
  base::OneShotTimer deadline;
  std::vector<core_service::mojom::ToolProgressPtr> progress;
  std::vector<core_service::mojom::ToolOutputChunkPtr> chunks;
  size_t output_bytes = 0u;
  uint32_t last_progress_sequence = 0u;
  uint32_t last_chunk_sequence = 0u;
  uint32_t streamed_chunks = 0u;
  bool streaming = false;
  bool stream_ended = false;
  bool have_progress = false;
  bool have_chunk = false;
  bool admitted = false;
};

}  // namespace taffy

#endif  // TAFFY_SERVICES_TOOL_RUNTIME_SUPERVISOR_PROFILE_TOOL_SUPERVISOR_JOB_RECORD_H_
