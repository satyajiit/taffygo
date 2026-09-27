// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_TOOL_RUNTIME_PYTHON_PYTHON_TOOL_SERVICE_H_
#define TAFFY_SERVICES_TOOL_RUNTIME_PYTHON_PYTHON_TOOL_SERVICE_H_

#include <string>

#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/task/sequenced_task_runner.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "taffy/contracts/tool-runtime/generated/mojom/tool_runtime.mojom.h"
#include "taffy/contracts/tool-runtime/tool_runtime_service.mojom.h"
#include "taffy/services/tool-runtime/python/python_executor.h"

namespace taffy {

// One fixed registered invocation per utility process. No method accepts
// source, a module, a path, a URL or argv; the only variable bytes are the
// bounded declarative input to a compiled entrypoint.
class PythonToolServiceImpl final
    : public tool_runtime::mojom::PythonToolService {
 public:
  explicit PythonToolServiceImpl(
      mojo::PendingReceiver<tool_runtime::mojom::PythonToolService> receiver);
  PythonToolServiceImpl(const PythonToolServiceImpl&) = delete;
  PythonToolServiceImpl& operator=(const PythonToolServiceImpl&) = delete;
  ~PythonToolServiceImpl() override;

  void Start(tool_runtime::mojom::ToolJobPtr job,
             tool_runtime::mojom::ToolJobResourcesPtr resources,
             mojo::PendingRemote<tool_runtime::mojom::ToolRuntimeClient> client,
             StartCallback callback) override;
  void Cancel(const std::string& job_id) override;

 private:
  tool_runtime::mojom::ToolAdmissionStatus Admit(
      const tool_runtime::mojom::ToolJob& job,
      const tool_runtime::mojom::ToolJobResources& resources) const;
  void OnExecuted(std::string job_id, python_tool::Result result);
  void Publish(tool_runtime::mojom::ToolTerminalStatus status,
               tool_runtime::mojom::ToolSuccessPtr success);

  mojo::Receiver<tool_runtime::mojom::PythonToolService> receiver_;
  mojo::Remote<tool_runtime::mojom::ToolRuntimeClient> client_;
  scoped_refptr<base::SequencedTaskRunner> execution_runner_;
  tool_runtime::mojom::OperationEnvelopePtr operation_;
  std::string job_id_;
  scoped_refptr<python_tool::CancellationFlag> cancelled_ =
      base::MakeRefCounted<python_tool::CancellationFlag>();
  bool running_ = false;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<PythonToolServiceImpl> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_SERVICES_TOOL_RUNTIME_PYTHON_PYTHON_TOOL_SERVICE_H_
