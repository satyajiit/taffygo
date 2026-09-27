// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/tool-runtime/supervisor/tool_job_client.h"

#include <utility>

#include "taffy/contracts/tool-runtime/generated/mojom/tool_runtime.mojom.h"

namespace taffy {

ToolJobClient::ToolJobClient(ProgressCallback progress,
                             ChunkCallback chunk,
                             CompletionCallback completion,
                             base::OnceClosure disconnected)
    : progress_(std::move(progress)),
      chunk_(std::move(chunk)),
      completion_(std::move(completion)),
      disconnected_(std::move(disconnected)) {}

ToolJobClient::~ToolJobClient() = default;

mojo::PendingRemote<tool_runtime::mojom::ToolRuntimeClient>
ToolJobClient::BindNewPipeAndPassRemote() {
  auto remote = receiver_.BindNewPipeAndPassRemote();
  receiver_.set_disconnect_handler(std::move(disconnected_));
  return remote;
}

void ToolJobClient::Progress(tool_runtime::mojom::ToolProgressPtr progress) {
  progress_.Run(std::move(progress));
}

void ToolJobClient::OutputChunk(tool_runtime::mojom::ToolOutputChunkPtr chunk) {
  chunk_.Run(std::move(chunk));
}

void ToolJobClient::Completed(
    tool_runtime::mojom::ToolCompletionPtr completion) {
  completion_.Run(std::move(completion));
}

}  // namespace taffy
