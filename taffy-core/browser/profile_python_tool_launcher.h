// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PROFILE_PYTHON_TOOL_LAUNCHER_H_
#define TAFFY_BROWSER_PROFILE_PYTHON_TOOL_LAUNCHER_H_

#include <stddef.h>

#include <deque>
#include <map>
#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "taffy/contracts/tool-runtime/generated/mojom/tool_runtime.mojom.h"
#include "taffy/contracts/tool-runtime/tool_runtime_service.mojom.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"

namespace taffy {

// Starts one sandboxed Python utility process for one registered invocation.
// Dropping the process remote is also the hard cancellation boundary: a trace
// hook gives cooperative cancellation a chance, then process teardown makes
// cancellation final even if native interpreter code is not tracing.
//
// The launcher relays the worker client because admission and completion use
// different Mojo pipes. That relay is the one ordering point that can withhold
// a terminal until admission and close the one-job process before publishing
// its result. No interpreter is pooled or reused.
class ProfilePythonToolLauncher
    : public base::RefCountedThreadSafe<ProfilePythonToolLauncher>,
      public tool_runtime::mojom::ToolRuntimeClient {
 public:
  using ProcessLaunchPort = base::RepeatingCallback<void(
      mojo::PendingReceiver<tool_runtime::mojom::PythonToolService>)>;

  ProfilePythonToolLauncher();
  // Production uses the default constructor. Tests inject process creation
  // and a deadline so every bootstrap outcome is deterministic on a host.
  ProfilePythonToolLauncher(ProcessLaunchPort process_launch,
                            base::TimeDelta admission_timeout);
  ProfilePythonToolLauncher(const ProfilePythonToolLauncher&) = delete;
  ProfilePythonToolLauncher& operator=(const ProfilePythonToolLauncher&) =
      delete;

  ProfileToolSupervisor::StartPort GetStartPort();
  ProfileToolSupervisor::CancelPort GetCancelPort();

  size_t running_process_count_for_testing() const { return workers_.size(); }
  size_t queued_job_count_for_testing() const { return pending_.size(); }
  size_t total_process_launch_count_for_testing() const {
    return total_process_launch_count_;
  }
  size_t peak_process_count_for_testing() const { return peak_process_count_; }

 private:
  struct PendingLaunch {
    tool_runtime::mojom::ToolJobPtr job;
    tool_runtime::mojom::ToolJobResourcesPtr resources;
    mojo::PendingRemote<tool_runtime::mojom::ToolRuntimeClient> client;
    ProfileToolSupervisor::AdmissionCallback callback;
  };

  struct Worker {
    Worker();
    ~Worker();

    mojo::Remote<tool_runtime::mojom::PythonToolService> remote;
    mojo::Remote<tool_runtime::mojom::ToolRuntimeClient> client;
    std::optional<mojo::ReceiverId> client_receiver_id;
    ProfileToolSupervisor::AdmissionCallback admission_callback;
    base::OneShotTimer admission_timer;
    tool_runtime::mojom::ToolCompletionPtr pending_completion;
  };

  friend class base::RefCountedThreadSafe<ProfilePythonToolLauncher>;
  ~ProfilePythonToolLauncher() override;

  void Launch(
      tool_runtime::mojom::ToolJobPtr job,
      tool_runtime::mojom::ToolJobResourcesPtr resources,
      mojo::PendingRemote<tool_runtime::mojom::ToolRuntimeClient> client,
      ProfileToolSupervisor::AdmissionCallback callback);
  void Cancel(const std::string& job_id);
  void OnAdmission(std::string job_id,
                   tool_runtime::mojom::ToolAdmissionPtr admission);
  void OnDisconnected(std::string job_id);
  void OnAdmissionTimeout(std::string job_id);
  void FinishFailedAdmission(const std::string& job_id,
                             tool_runtime::mojom::ToolAdmissionStatus status);
  void FinishCompletion(const std::string& job_id,
                        tool_runtime::mojom::ToolCompletionPtr completion);
  void EraseWorker(const std::string& job_id);
  void PumpLaunchQueue();
  bool ContainsJob(const std::string& job_id) const;

  // tool_runtime::mojom::ToolRuntimeClient. The relay is the ordering point
  // between admission, terminal publication and process release.
  void Progress(tool_runtime::mojom::ToolProgressPtr progress) override;
  void OutputChunk(tool_runtime::mojom::ToolOutputChunkPtr chunk) override;
  void Completed(tool_runtime::mojom::ToolCompletionPtr completion) override;

  // Only process bootstrap is serialized. An accepted worker keeps running in
  // its own process while the next interpreter starts, up to the same eight-job
  // ceiling the profile supervisor enforces.
  static constexpr size_t kMaxOutstandingJobs = 8u;
  static constexpr base::TimeDelta kDefaultAdmissionTimeout = base::Seconds(10);

  ProcessLaunchPort process_launch_;
  base::TimeDelta admission_timeout_;
  std::deque<PendingLaunch> pending_;
  std::optional<std::string> launching_job_id_;
  std::map<std::string, Worker> workers_;
  mojo::ReceiverSet<tool_runtime::mojom::ToolRuntimeClient, std::string>
      client_receivers_;
  size_t peak_process_count_ = 0u;
  size_t total_process_launch_count_ = 0u;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<ProfilePythonToolLauncher> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_PROFILE_PYTHON_TOOL_LAUNCHER_H_
