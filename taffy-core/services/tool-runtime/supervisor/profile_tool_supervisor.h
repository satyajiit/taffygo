// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_SERVICES_TOOL_RUNTIME_SUPERVISOR_PROFILE_TOOL_SUPERVISOR_H_
#define TAFFY_SERVICES_TOOL_RUNTIME_SUPERVISOR_PROFILE_TOOL_SUPERVISOR_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <string_view>

#include "base/containers/flat_map.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "taffy/contracts/core-service/core_service.mojom.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/contracts/tool-runtime/tool_runtime_service.mojom.h"
#include "taffy/services/tool-runtime/supervisor/tool_handle_broker.h"
#include "taffy/services/tool-runtime/supervisor/tool_job_resources.h"

namespace taffy {

// Profile-scoped browser broker for isolated tool workers. Runtime launch and
// cancellation are injected as three separate ports so no product target can
// accidentally create a universal worker or a cross-runtime fallback.
class ProfileToolSupervisor {
 public:
  using CompletionCallback =
      base::OnceCallback<void(core_service::mojom::ToolEffectResultPtr)>;
  using AdmissionCallback =
      base::OnceCallback<void(tool_runtime::mojom::ToolAdmissionPtr)>;
  // A worker is started with the job and with the resources the browser
  // opened for it, and with nothing else. The two travel together because
  // neither is meaningful alone: the job declares a length, a digest and a
  // transport, and the resources are what those declarations describe.
  using StartPort = base::RepeatingCallback<void(
      tool_runtime::mojom::ToolJobPtr,
      tool_runtime::mojom::ToolJobResourcesPtr,
      mojo::PendingRemote<tool_runtime::mojom::ToolRuntimeClient>,
      AdmissionCallback)>;
  using CancelPort = base::RepeatingCallback<void(const std::string&)>;
  // Receives one ordered chunk of a streamed job before its terminal result.
  // Absent by default, and while it is absent no job is planned to stream:
  // selecting a transport whose other end does not exist would produce output
  // nobody receives.
  using StreamCallback =
      base::RepeatingCallback<void(core_service::mojom::ToolStreamChunkPtr)>;

  struct PythonPorts {
    PythonPorts(StartPort start, CancelPort cancel);
    PythonPorts(const PythonPorts&);
    PythonPorts(PythonPorts&&);
    ~PythonPorts();
    static PythonPorts Unsupported();

    StartPort start;
    CancelPort cancel;
  };
  struct LocalModelPorts {
    LocalModelPorts(StartPort start, CancelPort cancel);
    LocalModelPorts(const LocalModelPorts&);
    LocalModelPorts(LocalModelPorts&&);
    ~LocalModelPorts();
    static LocalModelPorts Unsupported();

    StartPort start;
    CancelPort cancel;
  };
  struct MediaPorts {
    MediaPorts(StartPort start, CancelPort cancel);
    MediaPorts(const MediaPorts&);
    MediaPorts(MediaPorts&&);
    ~MediaPorts();
    static MediaPorts Unsupported();

    StartPort start;
    CancelPort cancel;
  };

  ProfileToolSupervisor(uint64_t service_generation,
                        PythonPorts python,
                        LocalModelPorts local_model,
                        MediaPorts media);
  ProfileToolSupervisor(const ProfileToolSupervisor&) = delete;
  ProfileToolSupervisor& operator=(const ProfileToolSupervisor&) = delete;
  ~ProfileToolSupervisor();

  // The CoreEffectBroker tool handler. `operation` and `job` are translated
  // field-by-field; generated objects are never serialized into an opaque
  // body.
  void Start(core_service::mojom::OperationEnvelopePtr operation,
             std::string effect_id,
             core_service::mojom::ToolJobEffectPtr job,
             CompletionCallback callback);

  void Cancel(const std::string& job_id);

  // Kills every worker running for one cancelled task, which is what makes
  // this seam safe to install. Generation cancellation cannot stand in for it:
  // a generation ends when the core does, and a cancelled task leaves the rest
  // of its profile running. A job whose effect carried no task owner is never
  // guessed into the set, for the same reason the broker does not guess one.
  void CancelTask(std::string_view task_id, uint64_t service_generation);
  void SetActiveGeneration(uint64_t service_generation);
  void Shutdown();

  // Installs the sink for streamed output. Until it is installed, the payload
  // plan never selects a streaming transport.
  void SetStreamSink(StreamCallback sink);

  // Installs the two ports that turn a declaration into an open resource: one
  // that opens a broker-minted handle, and one that resolves a registered
  // model identity against the browser's own register. Until they are
  // installed, a job that names a resource is refused.
  void SetResourcePorts(ToolHandleBroker::ResolvePort resolve_handle,
                        tool_job_resources::ModelArtifactPort open_artifact);

  // Installs the port that opens the bundled Python standard library. Held
  // apart from the two above because there is one catalog-selected installed
  // archive and no job may ask for another. Shipping configurations that
  // compile the source-built interpreter install this port beside its launch
  // port; configurations without that interpreter install neither and answer
  // that Python is unsupported before starting a process.
  void SetPythonLibraryPort(tool_job_resources::PythonLibraryPort open_library);

  // The one place resource identifiers are minted. Product composition mints
  // through this before submitting a job that names a resource; nothing below
  // the browser can reach it.
  ToolHandleBroker& handle_broker() { return handle_broker_; }

  // True while any tool job is still running in an isolated worker.
  bool HasActiveJobs() const;

  size_t active_job_count_for_testing() const;
  uint64_t rejected_message_count_for_testing() const {
    return rejected_message_count_;
  }

 private:
  struct JobRecord;

  void OnAdmission(const std::string& job_id,
                   tool_runtime::mojom::ToolAdmissionPtr admission);
  void OnProgress(const std::string& job_id,
                  tool_runtime::mojom::ToolProgressPtr progress);
  void OnChunk(const std::string& job_id,
               tool_runtime::mojom::ToolOutputChunkPtr chunk);
  void OnCompleted(const std::string& job_id,
                   tool_runtime::mojom::ToolCompletionPtr completion);
  void OnWorkerDisconnected(const std::string& job_id);
  void OnDeadline(const std::string& job_id);

  bool HasActiveIdentityConflict(
      const core_service::mojom::OperationEnvelope& operation,
      const std::string& effect_id,
      const core_service::mojom::ToolJobEffect& job) const;
  StartPort* StartPortFor(core_service::mojom::ToolRuntimeKind runtime);
  void CancelRuntime(core_service::mojom::ToolRuntimeKind runtime,
                     const std::string& job_id);
  void Finish(const std::string& job_id,
              core_service::mojom::ToolTerminalStatus status,
              core_service::mojom::ToolSuccessPtr success,
              bool cancel_worker);
  void FinishImmediate(std::string job_id,
                       core_service::mojom::ToolTerminalStatus status,
                       CompletionCallback callback);

  static constexpr size_t kMaxConcurrentJobs = 8u;

  uint64_t active_generation_;
  ToolHandleBroker handle_broker_;
  tool_job_resources::ModelArtifactPort model_artifact_port_;
  tool_job_resources::PythonLibraryPort python_library_port_;
  StreamCallback stream_sink_;
  PythonPorts python_;
  LocalModelPorts local_model_;
  MediaPorts media_;
  base::flat_map<std::string, std::unique_ptr<JobRecord>> jobs_;
  uint64_t rejected_message_count_ = 0u;
  bool shutting_down_ = false;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<ProfileToolSupervisor> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_SERVICES_TOOL_RUNTIME_SUPERVISOR_PROFILE_TOOL_SUPERVISOR_H_
