// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The tool seam with its processes actually running.
//
// Everything below this file is testable without a process: admission is pure,
// the probe is a function, and the supervisor's bounds are arithmetic. What
// only a browser test can answer is whether the seam works when the worker is
// somewhere else — whether the browser's descriptor survives the crossing,
// whether eight submitted jobs all cross bounded process admission, and
// whether a worker that dies is a job that ends instead of a job that hangs.
//
// It runs in TaffyGo's own harness rather than beside the core-service recovery
// suite in Chrome, and that is placement rather than convenience: a tool worker
// has no profile in it. It is handed a job, the descriptors the browser opened,
// and a pipe. What it needs from its host is a browser process carrying the
// product's utility registry, which `TaffyContentUtilityClient` installs here,
// and nothing Chrome alone can supply.

#include <stddef.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/bind.h"
#include "base/threading/thread_restrictions.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/content_browser_test.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "taffy/browser/profile_media_tool_launcher.h"
#include "taffy/contracts/tool-runtime/generated/mojom/tool_runtime.mojom.h"
#include "taffy/contracts/tool-runtime/tool_runtime_service.mojom.h"
#include "taffy/services/tool-runtime/media/test/synthetic_media.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = tool_runtime::mojom;

// Counts what one job's worker published, and signals when it is terminal.
class JobClient final : public mojom::ToolRuntimeClient {
 public:
  JobClient() = default;

  mojo::PendingRemote<mojom::ToolRuntimeClient> Bind() {
    return receiver_.BindNewPipeAndPassRemote();
  }

  void Progress(mojom::ToolProgressPtr progress) override {}
  void OutputChunk(mojom::ToolOutputChunkPtr chunk) override {}
  void Completed(mojom::ToolCompletionPtr completion) override {
    completion_ = std::move(completion);
    if (quit_) {
      std::move(quit_).Run();
    }
  }

  void WaitForCompletion() {
    if (completion_) {
      return;
    }
    base::RunLoop loop;
    quit_ = loop.QuitClosure();
    loop.Run();
  }

  const mojom::ToolCompletionPtr& completion() const { return completion_; }

 private:
  mojo::Receiver<mojom::ToolRuntimeClient> receiver_{this};
  mojom::ToolCompletionPtr completion_;
  base::OnceClosure quit_;
};

mojom::ToolJobPtr ProbeJob(const std::string& job_id) {
  auto job = mojom::ToolJob::New();
  job->operation = mojom::OperationEnvelope::New();
  job->operation->operation_id = "op-" + job_id;
  job->operation->service_generation = 1u;
  job->operation->task_revision = 1u;
  job->operation->deadline_monotonic_ms = 60000u;
  job->operation->idempotency_key = "idem-" + job_id;
  job->job_id = job_id;
  job->runtime = mojom::ToolRuntimeKind::kMedia;
  job->tool_id = "media.probe";
  job->tool_version = "1";
  job->operation_kind = mojom::ToolOperation::kProbeMedia;
  job->budget = mojom::ResourceBudget::New();
  job->budget->max_input_bytes = 1u << 20;
  job->budget->max_output_bytes = 4096u;
  job->budget->max_memory_bytes = 64u << 20;
  job->budget->max_cpu_ms = 5000u;
  job->budget->max_temporary_bytes = 0u;
  job->budget->max_output_chunks = 1u;
  job->media_probe = mojom::MediaProbeArguments::New();
  job->media_probe->input_handle = "handle-" + job_id;
  job->input_payload = mojom::ToolPayloadInput::New();
  job->input_payload->transport = mojom::ToolInputTransport::kInline;
  job->input_payload->byte_length = 0u;
  job->input_payload->digest.resize(32u);
  job->output_payload = mojom::ToolPayloadOutput::New();
  job->output_payload->transport = mojom::ToolOutputTransport::kInlineChunks;
  job->output_payload->max_byte_length = 4096u;
  // This runtime runs no interpreter, so the library declaration is the empty
  // one every job outside the Python family carries. It is written rather than
  // left unset because a job with a hole in it cannot be serialized at all.
  job->python_library = mojom::ToolPythonLibrary::New();
  job->python_library->archive_digest.resize(32u);
  return job;
}

class MediaToolWorkerBrowserTest : public content::ContentBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    content::ContentBrowserTest::SetUpOnMainThread();
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    launcher_ = base::MakeRefCounted<ProfileMediaToolLauncher>();
  }

  void TearDownOnMainThread() override {
    launcher_.reset();
    content::ContentBrowserTest::TearDownOnMainThread();
  }

  mojom::ToolJobResourcesPtr Resources(uint32_t duration_ms) {
    base::ScopedAllowBlockingForTesting allow_blocking;
    auto resources = mojom::ToolJobResources::New();
    resources->media_input = media_tool::test::WriteReadOnlyFile(
        temp_dir_.GetPath(), media_tool::test::BuildSilentWave(duration_ms));
    return resources;
  }

  base::ScopedTempDir temp_dir_;
  scoped_refptr<ProfileMediaToolLauncher> launcher_;
};

// The descriptor the browser opened is readable in the worker's sandbox, and
// the reading comes back through the contract rather than as bytes.
IN_PROC_BROWSER_TEST_F(MediaToolWorkerBrowserTest, AProbeRunsInItsOwnProcess) {
  JobClient client;
  mojom::ToolAdmissionPtr admission;
  base::RunLoop admitted;
  launcher_->GetStartPort().Run(
      ProbeJob("job-1"), Resources(1500u), client.Bind(),
      base::BindLambdaForTesting([&](mojom::ToolAdmissionPtr result) {
        admission = std::move(result);
        admitted.Quit();
      }));
  admitted.Run();
  ASSERT_TRUE(admission);
  ASSERT_EQ(mojom::ToolAdmissionStatus::kAccepted, admission->status);
  EXPECT_EQ(1u, launcher_->running_process_count_for_testing());
  EXPECT_EQ(1u, launcher_->peak_process_count_for_testing());

  client.WaitForCompletion();
  ASSERT_TRUE(client.completion());
  EXPECT_EQ("job-1", client.completion()->job_id);
  ASSERT_EQ(mojom::ToolTerminalStatus::kCompleted, client.completion()->status);
  ASSERT_TRUE(client.completion()->success);
  ASSERT_TRUE(client.completion()->success->media_probe);
  EXPECT_EQ(1u, client.completion()->success->media_probe->audio_streams);
  EXPECT_EQ(0u, client.completion()->success->media_probe->video_streams);
  EXPECT_NEAR(1500.0,
              static_cast<double>(
                  client.completion()->success->media_probe->duration_ms),
              150.0);

  // A finished job releases its process. Nothing cancels here: the worker ends
  // itself, because one process serves one job and a process kept alive after
  // its only job is a process nothing will ever close.
  base::RunLoop released;
  released.RunUntilIdle();
  EXPECT_EQ(0u, launcher_->running_process_count_for_testing());
}

// The claim the supervisor's `kMaxConcurrentJobs` is about. Eight jobs may be
// outstanding together, but only one new Android child crosses application
// bootstrap at a time. Admission releases the next launch immediately, while
// accepted workers keep running in their own processes. The host launcher
// suite holds those accepted workers open and proves all eight overlap; here a
// real worker is deliberately allowed to finish as soon as its small probe is
// done, so the assertion is over real launches and results rather than timing.
IN_PROC_BROWSER_TEST_F(MediaToolWorkerBrowserTest, EightJobsRunAtOnce) {
  constexpr size_t kJobs = 8u;
  std::vector<std::unique_ptr<JobClient>> clients;
  size_t admitted_count = 0u;
  bool every_job_admitted = true;
  base::RunLoop admitted;

  for (size_t i = 0; i < kJobs; ++i) {
    clients.push_back(std::make_unique<JobClient>());
    const std::string job_id = "job-" + base::NumberToString(i);
    launcher_->GetStartPort().Run(
        ProbeJob(job_id), Resources(500u + 100u * static_cast<uint32_t>(i)),
        clients.back()->Bind(),
        base::BindLambdaForTesting([&](mojom::ToolAdmissionPtr result) {
          if (!result ||
              result->status != mojom::ToolAdmissionStatus::kAccepted) {
            every_job_admitted = false;
          }
          if (++admitted_count == kJobs) {
            admitted.Quit();
          }
        }));
  }

  // The queue is not inferred from elapsed time: before the message loop can
  // deliver the first admission, exactly one process launch has begun and the
  // other seven jobs are held by the browser.
  EXPECT_EQ(1u, launcher_->running_process_count_for_testing());
  EXPECT_EQ(kJobs - 1u, launcher_->queued_job_count_for_testing());
  admitted.Run();
  ASSERT_TRUE(every_job_admitted);

  EXPECT_EQ(kJobs, launcher_->total_process_launch_count_for_testing());
  EXPECT_EQ(0u, launcher_->queued_job_count_for_testing());

  for (size_t i = 0; i < kJobs; ++i) {
    clients[i]->WaitForCompletion();
    ASSERT_TRUE(clients[i]->completion());
    EXPECT_EQ("job-" + base::NumberToString(i),
              clients[i]->completion()->job_id);
    ASSERT_EQ(mojom::ToolTerminalStatus::kCompleted,
              clients[i]->completion()->status);
    ASSERT_TRUE(clients[i]->completion()->success->media_probe);
    // Each job read its own file. A shared descriptor or a shared worker would
    // show up here as the same duration eight times.
    EXPECT_NEAR(
        500.0 + 100.0 * static_cast<double>(i),
        static_cast<double>(
            clients[i]->completion()->success->media_probe->duration_ms),
        150.0);
  }
}

// Bytes that are not a container end the job with a stated reason, in the
// worker, without the browser ever having parsed anything.
IN_PROC_BROWSER_TEST_F(MediaToolWorkerBrowserTest,
                       AnUnparseableFileEndsTheJobRatherThanTheProcess) {
  JobClient client;
  auto resources = mojom::ToolJobResources::New();
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    resources->media_input = media_tool::test::WriteReadOnlyFile(
        temp_dir_.GetPath(), std::vector<uint8_t>(8192u, 0x41u));
  }
  base::RunLoop admitted;
  launcher_->GetStartPort().Run(
      ProbeJob("job-1"), std::move(resources), client.Bind(),
      base::BindLambdaForTesting([&](mojom::ToolAdmissionPtr result) {
        ASSERT_TRUE(result);
        EXPECT_EQ(mojom::ToolAdmissionStatus::kAccepted, result->status);
        admitted.Quit();
      }));
  admitted.Run();

  client.WaitForCompletion();
  ASSERT_TRUE(client.completion());
  EXPECT_EQ(mojom::ToolTerminalStatus::kInvalidInput,
            client.completion()->status);
  EXPECT_FALSE(client.completion()->success);
}

// A cancel ends the worker, and the launcher stops holding its process.
IN_PROC_BROWSER_TEST_F(MediaToolWorkerBrowserTest, ACancelReleasesTheProcess) {
  JobClient client;
  base::RunLoop admitted;
  launcher_->GetStartPort().Run(
      ProbeJob("job-1"), Resources(3000u), client.Bind(),
      base::BindLambdaForTesting(
          [&](mojom::ToolAdmissionPtr result) { admitted.Quit(); }));
  admitted.Run();
  ASSERT_EQ(1u, launcher_->running_process_count_for_testing());

  launcher_->GetCancelPort().Run("job-1");
  EXPECT_EQ(0u, launcher_->running_process_count_for_testing());
}

}  // namespace
}  // namespace taffy
