// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_TOOL_RUNTIME_SUPERVISOR_TOOL_JOB_CLIENT_H_
#define TAFFY_SERVICES_TOOL_RUNTIME_SUPERVISOR_TOOL_JOB_CLIENT_H_

#include "base/functional/callback.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "taffy/contracts/tool-runtime/tool_runtime_service.mojom.h"

namespace taffy {

// Owns exactly one browser-side client endpoint for one worker job. The
// profile supervisor owns this object and therefore closes the endpoint when
// the profile, generation, deadline, or job ends.
class ToolJobClient final : public tool_runtime::mojom::ToolRuntimeClient {
 public:
  using ProgressCallback =
      base::RepeatingCallback<void(tool_runtime::mojom::ToolProgressPtr)>;
  using ChunkCallback =
      base::RepeatingCallback<void(tool_runtime::mojom::ToolOutputChunkPtr)>;
  using CompletionCallback =
      base::RepeatingCallback<void(tool_runtime::mojom::ToolCompletionPtr)>;

  ToolJobClient(ProgressCallback progress,
                ChunkCallback chunk,
                CompletionCallback completion,
                base::OnceClosure disconnected);
  ToolJobClient(const ToolJobClient&) = delete;
  ToolJobClient& operator=(const ToolJobClient&) = delete;
  ~ToolJobClient() override;

  mojo::PendingRemote<tool_runtime::mojom::ToolRuntimeClient>
  BindNewPipeAndPassRemote();

  // tool_runtime::mojom::ToolRuntimeClient:
  void Progress(tool_runtime::mojom::ToolProgressPtr progress) override;
  void OutputChunk(tool_runtime::mojom::ToolOutputChunkPtr chunk) override;
  void Completed(tool_runtime::mojom::ToolCompletionPtr completion) override;

 private:
  ProgressCallback progress_;
  ChunkCallback chunk_;
  CompletionCallback completion_;
  base::OnceClosure disconnected_;
  mojo::Receiver<tool_runtime::mojom::ToolRuntimeClient> receiver_{this};
};

}  // namespace taffy

#endif  // TAFFY_SERVICES_TOOL_RUNTIME_SUPERVISOR_TOOL_JOB_CLIENT_H_
