// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_TOOL_RUNTIME_MEDIA_MEDIA_TOOL_SERVICE_H_
#define TAFFY_SERVICES_TOOL_RUNTIME_MEDIA_MEDIA_TOOL_SERVICE_H_

#include <stdint.h>

#include <atomic>
#include <memory>
#include <string>

#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/task/sequenced_task_runner.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "taffy/contracts/tool-runtime/generated/mojom/tool_runtime.mojom.h"
#include "taffy/contracts/tool-runtime/tool_runtime_service.mojom.h"
#include "taffy/services/tool-runtime/media/media_probe.h"
#include "taffy/services/tool-runtime/media/media_transform.h"

namespace taffy {

// The sandboxed media worker: one process, one job, one terminal result.
//
// One job per process is the contract's own shape and it is what makes this
// class small. There is no queue, no scheduler and no worker pool here —
// concurrency is the browser launching several of these, bounded by the
// supervisor's `kMaxConcurrentJobs`, so a job that wedges or is killed takes
// nothing with it but its own process.
//
// The service never sees a path, a URL, a codec name or a command line. It
// receives a job, the descriptors the browser opened for it, one closed preset
// identity and a client to answer on. Anything the job declares that the
// resources do not carry is a refusal here rather than something the worker
// interprets.
class MediaToolServiceImpl final
    : public tool_runtime::mojom::MediaToolService {
 public:
  explicit MediaToolServiceImpl(
      mojo::PendingReceiver<tool_runtime::mojom::MediaToolService> receiver);
  MediaToolServiceImpl(const MediaToolServiceImpl&) = delete;
  MediaToolServiceImpl& operator=(const MediaToolServiceImpl&) = delete;
  ~MediaToolServiceImpl() override;

  // tool_runtime::mojom::MediaToolService
  void Start(tool_runtime::mojom::ToolJobPtr job,
             tool_runtime::mojom::ToolJobResourcesPtr resources,
             mojo::PendingRemote<tool_runtime::mojom::ToolRuntimeClient> client,
             StartCallback callback) override;
  void Cancel(const std::string& job_id) override;

 private:
  // Decides admission from the job and its resources alone. Pure, so the suite
  // can ask it what a shape means without starting a parse.
  tool_runtime::mojom::ToolAdmissionStatus Admit(
      const tool_runtime::mojom::ToolJob& job,
      const tool_runtime::mojom::ToolJobResources& resources) const;

  void OnProbed(std::string job_id, media_tool::ProbeResult result);
  void OnTransformed(std::string job_id,
                     media_tool::TransformKind kind,
                     media_tool::TransformResult result);
  void Publish(tool_runtime::mojom::ToolTerminalStatus status,
               tool_runtime::mojom::ToolSuccessPtr success);

  mojo::Receiver<tool_runtime::mojom::MediaToolService> receiver_;
  mojo::Remote<tool_runtime::mojom::ToolRuntimeClient> client_;
  scoped_refptr<base::SequencedTaskRunner> parse_runner_;

  // The one job this process was launched for, once admitted.
  tool_runtime::mojom::OperationEnvelopePtr operation_;
  std::string job_id_;
  std::string output_handle_;
  std::shared_ptr<std::atomic_bool> cancelled_signal_;
  bool running_ = false;
  bool cancelled_ = false;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<MediaToolServiceImpl> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_SERVICES_TOOL_RUNTIME_MEDIA_MEDIA_TOOL_SERVICE_H_
