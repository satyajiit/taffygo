// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_TOOL_RUNTIME_SUPERVISOR_TOOL_CONTRACT_CONVERSION_H_
#define TAFFY_SERVICES_TOOL_RUNTIME_SUPERVISOR_TOOL_CONTRACT_CONVERSION_H_

#include <stddef.h>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom-forward.h"
#include "taffy/contracts/tool-runtime/generated/mojom/tool_runtime.mojom-forward.h"

namespace taffy::tool_contract_conversion {

// Translates one Core Service effect into the broker-to-worker job, and
// decides how its bounded input and output cross the process boundary.
// `streaming_available` says whether anything is present to receive an
// incremental stream; when nothing is, the plan says so rather than selecting
// a transport whose other end does not exist.
bool ConvertJob(const core_service::mojom::OperationEnvelope& operation,
                const core_service::mojom::ToolJobEffect& job,
                bool streaming_available,
                tool_runtime::mojom::ToolJobPtr* converted);

bool ConvertProgress(const tool_runtime::mojom::ToolProgress& progress,
                     const core_service::mojom::ToolJobEffect& job,
                     core_service::mojom::ToolProgressPtr* converted);

// `streaming` is whether this job's output transport streams, which decides
// the byte bound one chunk is held to.
bool ConvertChunk(const tool_runtime::mojom::ToolOutputChunk& chunk,
                  const tool_runtime::mojom::OperationEnvelope& operation,
                  const core_service::mojom::ToolJobEffect& job,
                  bool streaming,
                  core_service::mojom::ToolOutputChunkPtr* converted,
                  size_t* output_bytes);

bool ConvertCompletion(const tool_runtime::mojom::ToolCompletion& completion,
                       const tool_runtime::mojom::OperationEnvelope& operation,
                       const core_service::mojom::ToolJobEffect& job,
                       core_service::mojom::ToolTerminalStatus* status,
                       core_service::mojom::ToolSuccessPtr* success,
                       size_t* output_bytes);

core_service::mojom::ToolTerminalStatus ConvertTerminalStatus(
    tool_runtime::mojom::ToolTerminalStatus status);

}  // namespace taffy::tool_contract_conversion

#endif  // TAFFY_SERVICES_TOOL_RUNTIME_SUPERVISOR_TOOL_CONTRACT_CONVERSION_H_
