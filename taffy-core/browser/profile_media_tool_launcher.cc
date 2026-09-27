// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/profile_media_tool_launcher.h"

#include <algorithm>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "content/public/browser/service_process_host.h"

namespace taffy {

namespace mojom = tool_runtime::mojom;

namespace {

void LaunchMediaProcess(
    mojo::PendingReceiver<tool_runtime::mojom::MediaToolService> receiver) {
  content::ServiceProcessHost::Launch(std::move(receiver),
                                      content::ServiceProcessHost::Options()
                                          .WithDisplayName("Taffy media tool")
                                          .Pass());
}

mojom::ToolAdmissionPtr Admission(std::string job_id,
                                  mojom::ToolAdmissionStatus status) {
  return mojom::ToolAdmission::New(std::move(job_id), status);
}

}  // namespace

ProfileMediaToolLauncher::Worker::Worker() = default;
ProfileMediaToolLauncher::Worker::~Worker() = default;

ProfileMediaToolLauncher::ProfileMediaToolLauncher()
    : ProfileMediaToolLauncher(base::BindRepeating(&LaunchMediaProcess),
                               kDefaultAdmissionTimeout) {}

ProfileMediaToolLauncher::ProfileMediaToolLauncher(
    ProcessLaunchPort process_launch,
    base::TimeDelta admission_timeout)
    : process_launch_(std::move(process_launch)),
      admission_timeout_(admission_timeout) {
  CHECK(process_launch_);
  CHECK(admission_timeout_.is_positive());
}

ProfileMediaToolLauncher::~ProfileMediaToolLauncher() = default;

ProfileToolSupervisor::StartPort ProfileMediaToolLauncher::GetStartPort() {
  return base::BindRepeating(&ProfileMediaToolLauncher::Launch,
                             base::RetainedRef(this));
}

ProfileToolSupervisor::CancelPort ProfileMediaToolLauncher::GetCancelPort() {
  return base::BindRepeating(&ProfileMediaToolLauncher::Cancel,
                             base::RetainedRef(this));
}

void ProfileMediaToolLauncher::Launch(
    mojom::ToolJobPtr job,
    mojom::ToolJobResourcesPtr resources,
    mojo::PendingRemote<mojom::ToolRuntimeClient> client,
    ProfileToolSupervisor::AdmissionCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!job || !resources) {
    auto admission = mojom::ToolAdmission::New();
    admission->status = mojom::ToolAdmissionStatus::kInvalidJob;
    std::move(callback).Run(std::move(admission));
    return;
  }
  const std::string job_id = job->job_id;
  if (job_id.empty() || ContainsJob(job_id)) {
    // An identifier already running is not a second attempt at the same work;
    // it is two jobs claiming one name, and admitting the later one would let
    // its cancel end the earlier one's process.
    auto admission = mojom::ToolAdmission::New();
    admission->job_id = job_id;
    admission->status = mojom::ToolAdmissionStatus::kInvalidJob;
    std::move(callback).Run(std::move(admission));
    return;
  }
  if (workers_.size() + pending_.size() >= kMaxOutstandingJobs) {
    std::move(callback).Run(
        Admission(job_id, mojom::ToolAdmissionStatus::kBackpressure));
    return;
  }

  pending_.push_back(PendingLaunch{std::move(job), std::move(resources),
                                   std::move(client), std::move(callback)});
  PumpLaunchQueue();
}

void ProfileMediaToolLauncher::Cancel(const std::string& job_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto pending =
      std::ranges::find_if(pending_, [&job_id](const PendingLaunch& launch) {
        return launch.job && launch.job->job_id == job_id;
      });
  if (pending != pending_.end()) {
    pending_.erase(pending);
    return;
  }

  const auto it = workers_.find(job_id);
  if (it == workers_.end()) {
    return;
  }
  // Ask first, then end the process by dropping its pipe. The worker answers a
  // cancel it can still hear with a `kCancelled` completion; one that cannot is
  // a disconnect, and the supervisor settles that job either way.
  it->second.remote->Cancel(job_id);
  const bool was_launching = launching_job_id_ == job_id;
  EraseWorker(job_id);
  if (was_launching) {
    launching_job_id_.reset();
    PumpLaunchQueue();
  }
}

void ProfileMediaToolLauncher::OnAdmission(std::string job_id,
                                           mojom::ToolAdmissionPtr admission) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto found = workers_.find(job_id);
  if (found == workers_.end() || !found->second.admission_callback) {
    return;
  }
  CHECK(launching_job_id_ && *launching_job_id_ == job_id);
  found->second.admission_timer.Stop();
  ProfileToolSupervisor::AdmissionCallback callback =
      std::move(found->second.admission_callback);
  launching_job_id_.reset();
  if (!admission || admission->job_id != job_id) {
    admission = Admission(job_id, mojom::ToolAdmissionStatus::kInvalidJob);
  }
  const bool accepted =
      admission->status == mojom::ToolAdmissionStatus::kAccepted;
  mojom::ToolCompletionPtr pending_completion;
  if (!accepted) {
    // A worker that refused its only job has nothing left to do, and a process
    // kept alive for a job that never started is a process nothing will ever
    // close.
    EraseWorker(job_id);
  } else {
    pending_completion = std::move(found->second.pending_completion);
  }
  std::move(callback).Run(std::move(admission));
  if (pending_completion) {
    FinishCompletion(job_id, std::move(pending_completion));
  }
  PumpLaunchQueue();
}

void ProfileMediaToolLauncher::OnDisconnected(std::string job_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto found = workers_.find(job_id);
  if (found == workers_.end()) {
    return;
  }
  if (found->second.admission_callback) {
    // A disconnect before admission has no bound client pipe for the
    // supervisor to observe. Settle the launch here and release the queue.
    FinishFailedAdmission(job_id,
                          mojom::ToolAdmissionStatus::kDeadlineExceeded);
    return;
  }
  // After admission the supervisor observes the same disconnect on the client
  // receiver it handed the worker, and settles that job as a runtime crash.
  EraseWorker(job_id);
}

void ProfileMediaToolLauncher::OnAdmissionTimeout(std::string job_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  FinishFailedAdmission(job_id, mojom::ToolAdmissionStatus::kDeadlineExceeded);
}

void ProfileMediaToolLauncher::FinishFailedAdmission(
    const std::string& job_id,
    mojom::ToolAdmissionStatus status) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto found = workers_.find(job_id);
  if (found == workers_.end() || !found->second.admission_callback) {
    return;
  }
  CHECK(launching_job_id_ && *launching_job_id_ == job_id);
  ProfileToolSupervisor::AdmissionCallback callback =
      std::move(found->second.admission_callback);
  EraseWorker(job_id);
  launching_job_id_.reset();
  std::move(callback).Run(Admission(job_id, status));
  PumpLaunchQueue();
}

void ProfileMediaToolLauncher::FinishCompletion(
    const std::string& job_id,
    mojom::ToolCompletionPtr completion) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto found = workers_.find(job_id);
  if (found == workers_.end() || found->second.admission_callback) {
    return;
  }

  // Completion and the service receiver are different Mojo pipes. There is no
  // ordering guarantee between them, so forwarding first can publish a
  // terminal job while its process still occupies an Android service slot.
  // Take the browser-side client, then erase the worker: erasing closes the
  // service remote and its relay receiver before the terminal message becomes
  // observable upstream. One process serves one job, so no legitimate call is
  // lost after this point.
  mojo::Remote<mojom::ToolRuntimeClient> client =
      std::move(found->second.client);
  EraseWorker(job_id);
  if (client) {
    client->Completed(std::move(completion));
  }
}

void ProfileMediaToolLauncher::EraseWorker(const std::string& job_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto found = workers_.find(job_id);
  if (found == workers_.end()) {
    return;
  }
  const std::optional<mojo::ReceiverId> receiver_id =
      found->second.client_receiver_id;
  workers_.erase(found);
  if (receiver_id.has_value()) {
    client_receivers_.Remove(*receiver_id);
  }
}

void ProfileMediaToolLauncher::Progress(mojom::ToolProgressPtr progress) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto found = workers_.find(client_receivers_.current_context());
  if (found != workers_.end() && found->second.client) {
    found->second.client->Progress(std::move(progress));
  }
}

void ProfileMediaToolLauncher::OutputChunk(mojom::ToolOutputChunkPtr chunk) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto found = workers_.find(client_receivers_.current_context());
  if (found != workers_.end() && found->second.client) {
    found->second.client->OutputChunk(std::move(chunk));
  }
}

void ProfileMediaToolLauncher::Completed(mojom::ToolCompletionPtr completion) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::string job_id = client_receivers_.current_context();
  const auto found = workers_.find(job_id);
  if (found == workers_.end()) {
    return;
  }
  if (found->second.admission_callback) {
    // Admission is the authority to publish runtime output. The worker's
    // callback and client are separate pipes, so a tiny job can finish before
    // the acceptance callback is dispatched. Hold at most the one terminal
    // result this runtime is allowed to produce and release it immediately
    // after acceptance.
    if (!found->second.pending_completion) {
      found->second.pending_completion = std::move(completion);
    }
    return;
  }
  FinishCompletion(job_id, std::move(completion));
}

void ProfileMediaToolLauncher::PumpLaunchQueue() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (launching_job_id_ || pending_.empty()) {
    return;
  }

  PendingLaunch launch = std::move(pending_.front());
  pending_.pop_front();
  CHECK(launch.job);
  const std::string job_id = launch.job->job_id;
  auto [found, inserted] = workers_.try_emplace(job_id);
  CHECK(inserted);
  Worker& worker = found->second;
  worker.admission_callback = std::move(launch.callback);
  worker.client.Bind(std::move(launch.client));
  mojo::PendingRemote<mojom::ToolRuntimeClient> relayed_client;
  worker.client_receiver_id = client_receivers_.Add(
      this, relayed_client.InitWithNewPipeAndPassReceiver(), job_id);
  launching_job_id_ = job_id;
  ++total_process_launch_count_;
  peak_process_count_ = std::max(peak_process_count_, workers_.size());

  auto receiver = worker.remote.BindNewPipeAndPassReceiver();
  worker.remote.set_disconnect_handler(
      base::BindOnce(&ProfileMediaToolLauncher::OnDisconnected,
                     weak_factory_.GetWeakPtr(), job_id));
  worker.admission_timer.Start(
      FROM_HERE, admission_timeout_,
      base::BindOnce(&ProfileMediaToolLauncher::OnAdmissionTimeout,
                     weak_factory_.GetWeakPtr(), job_id));
  process_launch_.Run(std::move(receiver));
  worker.remote->Start(std::move(launch.job), std::move(launch.resources),
                       std::move(relayed_client),
                       base::BindOnce(&ProfileMediaToolLauncher::OnAdmission,
                                      weak_factory_.GetWeakPtr(), job_id));
}

bool ProfileMediaToolLauncher::ContainsJob(const std::string& job_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (workers_.contains(job_id)) {
    return true;
  }
  return std::ranges::any_of(pending_, [&job_id](const PendingLaunch& launch) {
    return launch.job && launch.job->job_id == job_id;
  });
}

}  // namespace taffy
