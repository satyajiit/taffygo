// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/profile_python_tool_launcher.h"

#include <algorithm>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/location.h"
#include "content/public/browser/service_process_host.h"

namespace taffy {

namespace mojom = tool_runtime::mojom;

namespace {

void LaunchPythonProcess(
    mojo::PendingReceiver<tool_runtime::mojom::PythonToolService> receiver) {
  content::ServiceProcessHost::Launch(std::move(receiver),
                                      content::ServiceProcessHost::Options()
                                          .WithDisplayName("Taffy Python tool")
                                          .Pass());
}

mojom::ToolAdmissionPtr Admission(std::string job_id,
                                  mojom::ToolAdmissionStatus status) {
  return mojom::ToolAdmission::New(std::move(job_id), status);
}

}  // namespace

ProfilePythonToolLauncher::Worker::Worker() = default;
ProfilePythonToolLauncher::Worker::~Worker() = default;

ProfilePythonToolLauncher::ProfilePythonToolLauncher()
    : ProfilePythonToolLauncher(base::BindRepeating(&LaunchPythonProcess),
                                kDefaultAdmissionTimeout) {}

ProfilePythonToolLauncher::ProfilePythonToolLauncher(
    ProcessLaunchPort process_launch,
    base::TimeDelta admission_timeout)
    : process_launch_(std::move(process_launch)),
      admission_timeout_(admission_timeout) {
  CHECK(process_launch_);
  CHECK(admission_timeout_.is_positive());
}

ProfilePythonToolLauncher::~ProfilePythonToolLauncher() = default;

ProfileToolSupervisor::StartPort ProfilePythonToolLauncher::GetStartPort() {
  return base::BindRepeating(&ProfilePythonToolLauncher::Launch,
                             base::RetainedRef(this));
}

ProfileToolSupervisor::CancelPort ProfilePythonToolLauncher::GetCancelPort() {
  return base::BindRepeating(&ProfilePythonToolLauncher::Cancel,
                             base::RetainedRef(this));
}

void ProfilePythonToolLauncher::Launch(
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
    std::move(callback).Run(
        Admission(job_id, mojom::ToolAdmissionStatus::kInvalidJob));
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

void ProfilePythonToolLauncher::Cancel(const std::string& job_id) {
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
  it->second.remote->Cancel(job_id);
  // This is the hard part of cancellation. The worker may currently be in C
  // code where the Python trace hook cannot run; ending its only Mojo pipe
  // tears down the one-job service process instead of waiting for that code.
  const bool was_launching = launching_job_id_ == job_id;
  EraseWorker(job_id);
  if (was_launching) {
    launching_job_id_.reset();
    PumpLaunchQueue();
  }
}

void ProfilePythonToolLauncher::OnAdmission(std::string job_id,
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

void ProfilePythonToolLauncher::OnDisconnected(std::string job_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto found = workers_.find(job_id);
  if (found == workers_.end()) {
    return;
  }
  if (found->second.admission_callback) {
    FinishFailedAdmission(job_id,
                          mojom::ToolAdmissionStatus::kDeadlineExceeded);
    return;
  }
  EraseWorker(job_id);
}

void ProfilePythonToolLauncher::OnAdmissionTimeout(std::string job_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  FinishFailedAdmission(job_id, mojom::ToolAdmissionStatus::kDeadlineExceeded);
}

void ProfilePythonToolLauncher::FinishFailedAdmission(
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

void ProfilePythonToolLauncher::FinishCompletion(
    const std::string& job_id,
    mojom::ToolCompletionPtr completion) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto found = workers_.find(job_id);
  if (found == workers_.end() || found->second.admission_callback) {
    return;
  }

  // Keep the downstream pipe alive while the worker and relay are closed.
  // Completed then precedes the downstream disconnect on that same pipe.
  mojo::Remote<mojom::ToolRuntimeClient> client =
      std::move(found->second.client);
  EraseWorker(job_id);
  if (client) {
    client->Completed(std::move(completion));
  }
}

void ProfilePythonToolLauncher::EraseWorker(const std::string& job_id) {
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

void ProfilePythonToolLauncher::Progress(mojom::ToolProgressPtr progress) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto found = workers_.find(client_receivers_.current_context());
  if (found != workers_.end() && found->second.client) {
    found->second.client->Progress(std::move(progress));
  }
}

void ProfilePythonToolLauncher::OutputChunk(mojom::ToolOutputChunkPtr chunk) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto found = workers_.find(client_receivers_.current_context());
  if (found != workers_.end() && found->second.client) {
    found->second.client->OutputChunk(std::move(chunk));
  }
}

void ProfilePythonToolLauncher::Completed(mojom::ToolCompletionPtr completion) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::string job_id = client_receivers_.current_context();
  const auto found = workers_.find(job_id);
  if (found == workers_.end()) {
    return;
  }
  if (found->second.admission_callback) {
    if (!found->second.pending_completion) {
      found->second.pending_completion = std::move(completion);
    }
    return;
  }
  FinishCompletion(job_id, std::move(completion));
}

void ProfilePythonToolLauncher::PumpLaunchQueue() {
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
      base::BindOnce(&ProfilePythonToolLauncher::OnDisconnected,
                     weak_factory_.GetWeakPtr(), job_id));
  worker.admission_timer.Start(
      FROM_HERE, admission_timeout_,
      base::BindOnce(&ProfilePythonToolLauncher::OnAdmissionTimeout,
                     weak_factory_.GetWeakPtr(), job_id));
  process_launch_.Run(std::move(receiver));
  worker.remote->Start(std::move(launch.job), std::move(launch.resources),
                       std::move(relayed_client),
                       base::BindOnce(&ProfilePythonToolLauncher::OnAdmission,
                                      weak_factory_.GetWeakPtr(), job_id));
}

bool ProfilePythonToolLauncher::ContainsJob(const std::string& job_id) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (workers_.contains(job_id)) {
    return true;
  }
  return std::ranges::any_of(pending_, [&job_id](const PendingLaunch& launch) {
    return launch.job && launch.job->job_id == job_id;
  });
}

}  // namespace taffy
