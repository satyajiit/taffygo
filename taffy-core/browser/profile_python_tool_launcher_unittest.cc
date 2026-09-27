// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/profile_python_tool_launcher.h"

#include <stddef.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "taffy/contracts/tool-runtime/generated/mojom/tool_runtime.mojom.h"
#include "taffy/contracts/tool-runtime/tool_runtime_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = tool_runtime::mojom;

mojom::ToolJobPtr Job(std::string job_id) {
  auto job = mojom::ToolJob::New();
  job->operation = mojom::OperationEnvelope::New();
  job->operation->operation_id = "operation-" + job_id;
  job->operation->idempotency_key = "idempotency-" + job_id;
  job->job_id = std::move(job_id);
  job->runtime = mojom::ToolRuntimeKind::kPython;
  job->tool_id = "python.execute";
  job->tool_version = "1";
  job->operation_kind = mojom::ToolOperation::kRunBundledPythonModule;
  job->budget = mojom::ResourceBudget::New();
  job->budget->max_input_bytes = 2u;
  job->budget->max_output_bytes = 1u;
  job->budget->max_memory_bytes = 1u;
  job->budget->max_cpu_ms = 1u;
  job->budget->max_output_chunks = 1u;
  job->bundled_python = mojom::BundledPythonArguments::New();
  job->bundled_python->entrypoint_id = "document.build";
  job->bundled_python->input = {'{', '}'};
  job->input_payload = mojom::ToolPayloadInput::New();
  job->input_payload->transport = mojom::ToolInputTransport::kInline;
  job->input_payload->byte_length = 2u;
  job->input_payload->digest.resize(32u);
  job->output_payload = mojom::ToolPayloadOutput::New();
  job->output_payload->transport = mojom::ToolOutputTransport::kInlineChunks;
  job->output_payload->max_byte_length = 1u;
  job->python_library = mojom::ToolPythonLibrary::New();
  job->python_library->library_id = "python-stdlib";
  job->python_library->library_version = "test";
  job->python_library->archive_bytes = 1u;
  job->python_library->archive_digest.resize(32u);
  return job;
}

mojo::PendingRemote<mojom::ToolRuntimeClient> DisconnectedClient() {
  mojo::PendingRemote<mojom::ToolRuntimeClient> client;
  auto receiver = client.InitWithNewPipeAndPassReceiver();
  receiver.reset();
  return client;
}

class ControlledPythonService final : public mojom::PythonToolService {
 public:
  explicit ControlledPythonService(
      mojo::PendingReceiver<mojom::PythonToolService> receiver)
      : receiver_(this, std::move(receiver)) {}

  void Start(mojom::ToolJobPtr job,
             mojom::ToolJobResourcesPtr,
             mojo::PendingRemote<mojom::ToolRuntimeClient> client,
             StartCallback callback) override {
    job_id_ = job ? job->job_id : std::string();
    operation_ = job && job->operation ? job->operation.Clone() : nullptr;
    client_.Bind(std::move(client));
    callback_ = std::move(callback);
  }

  void Cancel(const std::string&) override {}

  bool start_received() const { return !callback_.is_null(); }

  void Admit() { Respond(job_id_, mojom::ToolAdmissionStatus::kAccepted); }

  void Refuse(mojom::ToolAdmissionStatus status) { Respond(job_id_, status); }

  void AdmitAs(std::string job_id) {
    Respond(std::move(job_id), mojom::ToolAdmissionStatus::kAccepted);
  }

  void Disconnect() { receiver_.reset(); }

  void Complete() {
    auto completion = mojom::ToolCompletion::New();
    completion->operation = operation_.Clone();
    completion->job_id = job_id_;
    completion->status = mojom::ToolTerminalStatus::kCancelled;
    client_->Completed(std::move(completion));
  }

  void DropPendingResponse() { callback_.Reset(); }

 private:
  void Respond(std::string job_id, mojom::ToolAdmissionStatus status) {
    ASSERT_TRUE(callback_);
    std::move(callback_).Run(
        mojom::ToolAdmission::New(std::move(job_id), status));
  }

  mojo::Receiver<mojom::PythonToolService> receiver_;
  mojo::Remote<mojom::ToolRuntimeClient> client_;
  mojom::OperationEnvelopePtr operation_;
  std::string job_id_;
  StartCallback callback_;
};

class RecordingClient final : public mojom::ToolRuntimeClient {
 public:
  explicit RecordingClient(base::RepeatingClosure on_completion)
      : on_completion_(std::move(on_completion)) {}

  mojo::PendingRemote<mojom::ToolRuntimeClient> Bind() {
    auto remote = receiver_.BindNewPipeAndPassRemote();
    receiver_.set_disconnect_handler(base::BindOnce(
        &RecordingClient::OnDisconnected, base::Unretained(this)));
    return remote;
  }

  void Progress(mojom::ToolProgressPtr) override {}
  void OutputChunk(mojom::ToolOutputChunkPtr) override {}
  void Completed(mojom::ToolCompletionPtr) override {
    ++completion_count_;
    on_completion_.Run();
  }

  size_t completion_count() const { return completion_count_; }
  size_t disconnect_count() const { return disconnect_count_; }

 private:
  void OnDisconnected() { ++disconnect_count_; }

  base::RepeatingClosure on_completion_;
  mojo::Receiver<mojom::ToolRuntimeClient> receiver_{this};
  size_t completion_count_ = 0u;
  size_t disconnect_count_ = 0u;
};

class ControlledProcessLauncher {
 public:
  ProfilePythonToolLauncher::ProcessLaunchPort port() {
    return base::BindRepeating(&ControlledProcessLauncher::Launch,
                               base::Unretained(this));
  }

  void Launch(mojo::PendingReceiver<mojom::PythonToolService> receiver) {
    services_.push_back(
        std::make_unique<ControlledPythonService>(std::move(receiver)));
  }

  size_t launch_count() const { return services_.size(); }

  ControlledPythonService& service(size_t index) {
    return *services_.at(index);
  }

  void DropPendingResponses() {
    for (const auto& service : services_) {
      service->DropPendingResponse();
    }
  }

 private:
  std::vector<std::unique_ptr<ControlledPythonService>> services_;
};

class ProfilePythonToolLauncherTest : public ::testing::Test {
 protected:
  ProfilePythonToolLauncherTest()
      : launcher_(base::MakeRefCounted<ProfilePythonToolLauncher>(
            processes_.port(),
            base::Seconds(1))) {}

  void TearDown() override {
    launcher_.reset();
    task_environment_.RunUntilIdle();
    processes_.DropPendingResponses();
    task_environment_.RunUntilIdle();
  }

  void Start(std::string job_id) {
    StartWithClient(std::move(job_id), DisconnectedClient());
  }

  void StartWithClient(std::string job_id,
                       mojo::PendingRemote<mojom::ToolRuntimeClient> client) {
    launcher_->GetStartPort().Run(
        Job(job_id), mojom::ToolJobResources::New(), std::move(client),
        base::BindOnce(&ProfilePythonToolLauncherTest::OnAdmission,
                       base::Unretained(this)));
  }

  void OnAdmission(mojom::ToolAdmissionPtr admission) {
    process_counts_at_admission_.push_back(
        launcher_->running_process_count_for_testing());
    admissions_.push_back(std::move(admission));
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  ControlledProcessLauncher processes_;
  scoped_refptr<ProfilePythonToolLauncher> launcher_;
  std::vector<mojom::ToolAdmissionPtr> admissions_;
  std::vector<size_t> process_counts_at_admission_;
};

TEST_F(ProfilePythonToolLauncherTest,
       SerializesBootstrapButAcceptedWorkersContinueTogether) {
  constexpr size_t kJobs = 8u;
  for (size_t index = 0; index < kJobs; ++index) {
    Start("job-" + std::to_string(index));
  }

  EXPECT_EQ(1u, processes_.launch_count());
  EXPECT_EQ(1u, launcher_->running_process_count_for_testing());
  EXPECT_EQ(7u, launcher_->queued_job_count_for_testing());

  for (size_t index = 0; index < kJobs; ++index) {
    task_environment_.RunUntilIdle();
    ASSERT_TRUE(processes_.service(index).start_received());
    processes_.service(index).Admit();
    task_environment_.RunUntilIdle();
    EXPECT_EQ(index + 1u, admissions_.size());
    EXPECT_EQ(mojom::ToolAdmissionStatus::kAccepted,
              admissions_.back()->status);
  }

  EXPECT_EQ(kJobs, processes_.launch_count());
  EXPECT_EQ(kJobs, launcher_->total_process_launch_count_for_testing());
  EXPECT_EQ(kJobs, launcher_->running_process_count_for_testing());
  EXPECT_EQ(kJobs, launcher_->peak_process_count_for_testing());
  EXPECT_EQ(0u, launcher_->queued_job_count_for_testing());
}

TEST_F(ProfilePythonToolLauncherTest,
       DisconnectBeforeAdmissionSettlesAndStartsNextJob) {
  Start("job-1");
  Start("job-2");
  task_environment_.RunUntilIdle();

  processes_.service(0u).Disconnect();
  task_environment_.RunUntilIdle();

  ASSERT_EQ(1u, admissions_.size());
  EXPECT_EQ("job-1", admissions_.front()->job_id);
  EXPECT_EQ(mojom::ToolAdmissionStatus::kDeadlineExceeded,
            admissions_.front()->status);
  ASSERT_EQ(2u, processes_.launch_count());
  EXPECT_TRUE(processes_.service(1u).start_received());
}

TEST_F(ProfilePythonToolLauncherTest,
       TerminalClosesTheProcessBeforeExactlyOneClientCompletion) {
  size_t process_count_at_completion = 99u;
  RecordingClient client(base::BindRepeating(
      [](ProfilePythonToolLauncher* launcher, size_t* count) {
        *count = launcher->running_process_count_for_testing();
      },
      base::Unretained(launcher_.get()), &process_count_at_completion));
  StartWithClient("job-1", client.Bind());
  task_environment_.RunUntilIdle();
  processes_.service(0u).Admit();
  task_environment_.RunUntilIdle();
  ASSERT_EQ(1u, launcher_->running_process_count_for_testing());

  processes_.service(0u).Complete();
  processes_.service(0u).Complete();
  task_environment_.RunUntilIdle();

  EXPECT_EQ(0u, process_count_at_completion);
  EXPECT_EQ(0u, launcher_->running_process_count_for_testing());
  EXPECT_EQ(1u, client.completion_count());

  processes_.service(0u).Disconnect();
  task_environment_.RunUntilIdle();
  EXPECT_EQ(1u, client.completion_count());
}

TEST_F(ProfilePythonToolLauncherTest,
       TerminalOnTheClientPipeWaitsForAdmissionAuthority) {
  size_t process_count_at_completion = 99u;
  RecordingClient client(base::BindRepeating(
      [](ProfilePythonToolLauncher* launcher, size_t* count) {
        *count = launcher->running_process_count_for_testing();
      },
      base::Unretained(launcher_.get()), &process_count_at_completion));
  StartWithClient("job-1", client.Bind());
  task_environment_.RunUntilIdle();

  processes_.service(0u).Complete();
  task_environment_.RunUntilIdle();
  EXPECT_EQ(0u, client.completion_count());
  EXPECT_EQ(1u, launcher_->running_process_count_for_testing());

  processes_.service(0u).Admit();
  task_environment_.RunUntilIdle();
  EXPECT_EQ(1u, admissions_.size());
  EXPECT_EQ(1u, client.completion_count());
  EXPECT_EQ(0u, process_count_at_completion);
  EXPECT_EQ(0u, launcher_->running_process_count_for_testing());
}

TEST_F(ProfilePythonToolLauncherTest,
       CancellationRevokesALateTerminalRatherThanDuplicatingIt) {
  RecordingClient client(base::BindRepeating([] {}));
  StartWithClient("job-1", client.Bind());
  task_environment_.RunUntilIdle();
  processes_.service(0u).Admit();
  task_environment_.RunUntilIdle();

  launcher_->GetCancelPort().Run("job-1");
  processes_.service(0u).Complete();
  task_environment_.RunUntilIdle();

  EXPECT_EQ(0u, launcher_->running_process_count_for_testing());
  EXPECT_EQ(0u, client.completion_count());
}

TEST_F(ProfilePythonToolLauncherTest,
       MissingPreMojoDisconnectTimesOutAndStartsNextJob) {
  Start("job-1");
  Start("job-2");
  task_environment_.RunUntilIdle();

  task_environment_.FastForwardBy(base::Seconds(1));
  task_environment_.RunUntilIdle();

  ASSERT_EQ(1u, admissions_.size());
  EXPECT_EQ("job-1", admissions_.front()->job_id);
  EXPECT_EQ(mojom::ToolAdmissionStatus::kDeadlineExceeded,
            admissions_.front()->status);
  ASSERT_EQ(2u, processes_.launch_count());
  EXPECT_TRUE(processes_.service(1u).start_received());
}

TEST_F(ProfilePythonToolLauncherTest, CancellingAQueuedJobNeverLaunchesIt) {
  Start("job-1");
  Start("job-2");
  launcher_->GetCancelPort().Run("job-2");
  task_environment_.RunUntilIdle();

  processes_.service(0u).Admit();
  task_environment_.RunUntilIdle();

  EXPECT_EQ(1u, processes_.launch_count());
  EXPECT_EQ(0u, launcher_->queued_job_count_for_testing());
  ASSERT_EQ(1u, admissions_.size());
  EXPECT_EQ("job-1", admissions_.front()->job_id);
}

TEST_F(ProfilePythonToolLauncherTest,
       CancellingTheStartingJobReleasesAdmissionForTheNextJob) {
  Start("job-1");
  Start("job-2");
  task_environment_.RunUntilIdle();

  launcher_->GetCancelPort().Run("job-1");
  task_environment_.RunUntilIdle();

  ASSERT_EQ(2u, processes_.launch_count());
  EXPECT_TRUE(processes_.service(1u).start_received());
  EXPECT_EQ(0u, launcher_->queued_job_count_for_testing());
  EXPECT_TRUE(admissions_.empty());

  processes_.service(1u).Admit();
  task_environment_.RunUntilIdle();

  ASSERT_EQ(1u, admissions_.size());
  EXPECT_EQ("job-2", admissions_.front()->job_id);
  EXPECT_EQ(mojom::ToolAdmissionStatus::kAccepted, admissions_.front()->status);
}

TEST_F(ProfilePythonToolLauncherTest, NinthOutstandingJobGetsBackpressure) {
  for (size_t index = 0; index < 9u; ++index) {
    Start("job-" + std::to_string(index));
  }

  ASSERT_EQ(1u, admissions_.size());
  EXPECT_EQ("job-8", admissions_.front()->job_id);
  EXPECT_EQ(mojom::ToolAdmissionStatus::kBackpressure,
            admissions_.front()->status);
  EXPECT_EQ(1u, processes_.launch_count());
  EXPECT_EQ(7u, launcher_->queued_job_count_for_testing());
}

TEST_F(ProfilePythonToolLauncherTest,
       RefusedAndMismatchedAdmissionsCloseBeforeCallbackAndAdvanceQueue) {
  Start("job-1");
  Start("job-2");
  Start("job-3");
  task_environment_.RunUntilIdle();

  processes_.service(0u).Refuse(mojom::ToolAdmissionStatus::kUnsupported);
  task_environment_.RunUntilIdle();
  ASSERT_EQ(1u, admissions_.size());
  EXPECT_EQ(mojom::ToolAdmissionStatus::kUnsupported, admissions_[0u]->status);
  EXPECT_EQ(0u, process_counts_at_admission_[0u]);
  ASSERT_EQ(2u, processes_.launch_count());

  processes_.service(1u).AdmitAs("not-job-2");
  task_environment_.RunUntilIdle();
  ASSERT_EQ(2u, admissions_.size());
  EXPECT_EQ("job-2", admissions_[1u]->job_id);
  EXPECT_EQ(mojom::ToolAdmissionStatus::kInvalidJob, admissions_[1u]->status);
  EXPECT_EQ(0u, process_counts_at_admission_[1u]);
  ASSERT_EQ(3u, processes_.launch_count());
  EXPECT_TRUE(processes_.service(2u).start_received());
}

TEST_F(ProfilePythonToolLauncherTest,
       LateAdmissionAfterTimeoutCannotSettleTheLaunchTwice) {
  Start("job-1");
  task_environment_.RunUntilIdle();

  task_environment_.FastForwardBy(base::Seconds(1));
  task_environment_.RunUntilIdle();
  ASSERT_EQ(1u, admissions_.size());
  EXPECT_EQ(mojom::ToolAdmissionStatus::kDeadlineExceeded,
            admissions_.front()->status);
  EXPECT_EQ(0u, launcher_->running_process_count_for_testing());

  processes_.service(0u).Admit();
  processes_.service(0u).Disconnect();
  task_environment_.RunUntilIdle();
  EXPECT_EQ(1u, admissions_.size());
  EXPECT_EQ(0u, launcher_->running_process_count_for_testing());
  EXPECT_EQ(1u, launcher_->total_process_launch_count_for_testing());
}

TEST_F(ProfilePythonToolLauncherTest,
       DisconnectAfterAdmissionClosesTheRelayExactlyOnce) {
  RecordingClient client(base::BindRepeating([] {}));
  StartWithClient("job-1", client.Bind());
  task_environment_.RunUntilIdle();
  processes_.service(0u).Admit();
  task_environment_.RunUntilIdle();

  processes_.service(0u).Disconnect();
  task_environment_.RunUntilIdle();
  EXPECT_EQ(0u, launcher_->running_process_count_for_testing());
  EXPECT_EQ(0u, client.completion_count());
  EXPECT_EQ(1u, client.disconnect_count());

  processes_.service(0u).Complete();
  processes_.service(0u).Disconnect();
  task_environment_.RunUntilIdle();
  EXPECT_EQ(0u, client.completion_count());
  EXPECT_EQ(1u, client.disconnect_count());
}

}  // namespace
}  // namespace taffy
