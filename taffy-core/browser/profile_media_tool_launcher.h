// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PROFILE_MEDIA_TOOL_LAUNCHER_H_
#define TAFFY_BROWSER_PROFILE_MEDIA_TOOL_LAUNCHER_H_

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

// Turns the supervisor's media launch port into real processes.
//
// One process per job, which is the media root's own rule and not a policy
// invented here. It is what makes the bound meaningful: a wedged parse, a
// hostile container that drives the demuxer into an enormous allocation, and a
// worker killed for exceeding its budget are all one process ending, and the
// browser learns about each the same way — the pipe closes and the supervisor
// settles that job alone.
//
// The launcher holds no descriptor and opens nothing. The resources travel
// through it, having been opened by the browser's own handle broker, and it
// keeps only what it needs to end a process it started.
class ProfileMediaToolLauncher
    : public base::RefCountedThreadSafe<ProfileMediaToolLauncher>,
      public tool_runtime::mojom::ToolRuntimeClient {
 public:
  using ProcessLaunchPort = base::RepeatingCallback<void(
      mojo::PendingReceiver<tool_runtime::mojom::MediaToolService>)>;

  ProfileMediaToolLauncher();
  // A process launcher and admission deadline are injected together so the
  // queue's launch, disconnect and timeout paths can be proved without a
  // process. Production always uses the default constructor.
  ProfileMediaToolLauncher(ProcessLaunchPort process_launch,
                           base::TimeDelta admission_timeout);
  ProfileMediaToolLauncher(const ProfileMediaToolLauncher&) = delete;
  ProfileMediaToolLauncher& operator=(const ProfileMediaToolLauncher&) = delete;

  // The pair `ProfileToolSupervisor::MediaPorts` is built from.
  ProfileToolSupervisor::StartPort GetStartPort();
  ProfileToolSupervisor::CancelPort GetCancelPort();

  size_t running_process_count_for_testing() const { return workers_.size(); }
  size_t queued_job_count_for_testing() const { return pending_.size(); }
  size_t total_process_launch_count_for_testing() const {
    return total_process_launch_count_;
  }

  // The most processes that were alive at one moment. A live count answers a
  // different question: by the time eight jobs have been admitted the first may
  // already have finished and released its process, so a suite reading the live
  // count would be asking the scheduler rather than asserting the bound.
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

    mojo::Remote<tool_runtime::mojom::MediaToolService> remote;
    mojo::Remote<tool_runtime::mojom::ToolRuntimeClient> client;
    std::optional<mojo::ReceiverId> client_receiver_id;
    ProfileToolSupervisor::AdmissionCallback admission_callback;
    base::OneShotTimer admission_timer;
    tool_runtime::mojom::ToolCompletionPtr pending_completion;
  };

  friend class base::RefCountedThreadSafe<ProfileMediaToolLauncher>;
  ~ProfileMediaToolLauncher() override;

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
  // between a terminal result and closing the process that produced it.
  void Progress(tool_runtime::mojom::ToolProgressPtr progress) override;
  void OutputChunk(tool_runtime::mojom::ToolOutputChunkPtr chunk) override;
  void Completed(tool_runtime::mojom::ToolCompletionPtr completion) override;

  // Android's shipping APK declares forty sandboxed-service slots and the
  // profile supervisor admits at most eight jobs, so this is not a substitute
  // for Chromium's process allocator. It serializes only process bootstrap
  // through admission. Once a worker accepts its job the next process starts,
  // and accepted workers continue concurrently in separate processes.
  //
  // This boundary matters for incremental and updated APKs: child application
  // bootstrap touches package-scoped state before native IPC exists. If that
  // bootstrap dies, Android has no Mojo endpoint through which to report the
  // failure. The timer turns that missing handshake into a bounded admission
  // result and lets the queue continue.
  static constexpr size_t kMaxOutstandingJobs = 8u;
  static constexpr base::TimeDelta kDefaultAdmissionTimeout = base::Seconds(10);

  ProcessLaunchPort process_launch_;
  base::TimeDelta admission_timeout_;
  std::deque<PendingLaunch> pending_;
  std::optional<std::string> launching_job_id_;

  // One remote per launched job. Erasing the entry drops the remote, which is
  // how the process is asked to end: there is no kill switch here, because a
  // browser that could terminate a worker by identity would need a second
  // authority to decide when it may.
  std::map<std::string, Worker> workers_;
  mojo::ReceiverSet<tool_runtime::mojom::ToolRuntimeClient, std::string>
      client_receivers_;
  size_t peak_process_count_ = 0u;
  size_t total_process_launch_count_ = 0u;

  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<ProfileMediaToolLauncher> weak_factory_{this};
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_PROFILE_MEDIA_TOOL_LAUNCHER_H_
