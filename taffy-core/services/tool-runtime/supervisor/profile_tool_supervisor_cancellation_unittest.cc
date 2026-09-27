// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"

#include <stdint.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/files/file.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/contracts/tool-runtime/generated/mojom/tool_runtime.mojom.h"
#include "taffy/contracts/tool-runtime/tool_runtime_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace core = core_service::mojom;
namespace runtime = tool_runtime::mojom;

constexpr uint64_t kGeneration = 7u;

uint64_t NowMilliseconds() {
  return static_cast<uint64_t>(
      base::TimeTicks::Now().since_origin().InMilliseconds());
}

core::OperationEnvelopePtr Operation(const std::string& suffix) {
  return core::OperationEnvelope::New("operation-" + suffix, kGeneration, 1u,
                                      NowMilliseconds() + 10'000u,
                                      "idempotency-" + suffix);
}

core::ToolJobEffectPtr Job(const std::string& suffix,
                           const std::string& task_id) {
  auto job = core::ToolJobEffect::New();
  job->job_id = "job-" + suffix;
  job->runtime = core::ToolRuntimeKind::kPython;
  job->tool_id = "bundled-tool";
  job->tool_version = "1";
  job->operation_kind = core::ToolOperation::kRunBundledPythonModule;
  job->budget = core::ToolResourceBudget::New(1024u, 1024u, 1024u * 1024u,
                                              1000u, 4096u, 4u);
  job->bundled_python =
      core::BundledPythonArguments::New("entrypoint", std::vector<uint8_t>{1});
  job->task_id = task_id;
  return job;
}

class ProfileToolSupervisorCancellationTest : public testing::Test {
 public:
  ProfileToolSupervisorCancellationTest()
      : supervisor_(kGeneration,
                    Ports<ProfileToolSupervisor::PythonPorts>(),
                    Ports<ProfileToolSupervisor::LocalModelPorts>(),
                    Ports<ProfileToolSupervisor::MediaPorts>()) {}

  // One capturing pair per runtime family. The three structs are separate
  // types on purpose - so no build can create a universal worker - which is
  // why this is a template rather than one shared value.
  template <typename PortStruct>
  PortStruct Ports() {
    return PortStruct(
        base::BindRepeating(
            &ProfileToolSupervisorCancellationTest::StartWorker,
            base::Unretained(this)),
        base::BindRepeating(
            &ProfileToolSupervisorCancellationTest::CancelWorker,
            base::Unretained(this)));
  }

  void StartWorker(runtime::ToolJobPtr job,
                   runtime::ToolJobResourcesPtr resources,
                   mojo::PendingRemote<runtime::ToolRuntimeClient> client,
                   ProfileToolSupervisor::AdmissionCallback callback) {
    started_jobs_.push_back(job->Clone());
    started_resources_.push_back(std::move(resources));
    clients_.push_back(
        std::make_unique<mojo::Remote<runtime::ToolRuntimeClient>>(
            std::move(client)));
    std::move(callback).Run(runtime::ToolAdmission::New(
        job->job_id, runtime::ToolAdmissionStatus::kAccepted));
  }

  void CancelWorker(const std::string& job_id) {
    cancelled_workers_.push_back(job_id);
  }

  void Start(const std::string& suffix, const std::string& task_id) {
    supervisor_.Start(
        Operation(suffix), "effect-" + suffix, Job(suffix, task_id),
        base::BindOnce(&ProfileToolSupervisorCancellationTest::Collect,
                       base::Unretained(this)));
  }

  void Collect(core::ToolEffectResultPtr result) {
    results_.push_back(std::move(result));
  }

  base::File OpenScratch(const std::string& name) {
    if (!scratch_.IsValid()) {
      CHECK(scratch_.CreateUniqueTempDir());
    }
    return base::File(scratch_.GetPath().AppendASCII(name),
                      base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_READ |
                          base::File::FLAG_WRITE);
  }

 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  base::ScopedTempDir scratch_;
  std::vector<runtime::ToolJobPtr> started_jobs_;
  std::vector<runtime::ToolJobResourcesPtr> started_resources_;
  std::vector<std::unique_ptr<mojo::Remote<runtime::ToolRuntimeClient>>>
      clients_;
  std::vector<std::string> cancelled_workers_;
  std::vector<core::ToolEffectResultPtr> results_;
  ProfileToolSupervisor supervisor_;
};

// A cancelled task's workers stop, and only its workers stop. Generation
// cancellation cannot stand in for this: a generation ends when the core does,
// and one cancelled task leaves the rest of the profile running.
TEST_F(ProfileToolSupervisorCancellationTest, OnlyTheCancelledTaskStops) {
  Start("a", "task-1");
  Start("b", "task-2");
  ASSERT_EQ(2u, started_jobs_.size());
  EXPECT_EQ(2u, supervisor_.active_job_count_for_testing());

  supervisor_.CancelTask("task-1", kGeneration);

  // The worker process was told to stop, which is the half a synthesized
  // terminal cannot do: without it the job keeps its memory, its CPU budget
  // and every descriptor the browser opened for it.
  ASSERT_EQ(1u, cancelled_workers_.size());
  EXPECT_EQ("job-a", cancelled_workers_.front());
  ASSERT_EQ(1u, results_.size());
  EXPECT_EQ("job-a", results_.front()->job_id);
  EXPECT_EQ(core::ToolTerminalStatus::kCancelled, results_.front()->status);
  EXPECT_EQ(1u, supervisor_.active_job_count_for_testing());
}

TEST_F(ProfileToolSupervisorCancellationTest, AJobNoTaskOwnsIsNeverGuessed) {
  Start("direct", std::string());
  ASSERT_EQ(1u, started_jobs_.size());

  // An empty task id means the request was made outside any task. Matching it
  // against whichever task was cancelled would stop work nobody asked to stop.
  supervisor_.CancelTask("task-1", kGeneration);
  supervisor_.CancelTask(std::string_view(), kGeneration);
  EXPECT_TRUE(cancelled_workers_.empty());
  EXPECT_TRUE(results_.empty());
  EXPECT_EQ(1u, supervisor_.active_job_count_for_testing());
}

TEST_F(ProfileToolSupervisorCancellationTest, AStaleGenerationStopsNothing) {
  Start("a", "task-1");
  ASSERT_EQ(1u, started_jobs_.size());

  supervisor_.CancelTask("task-1", kGeneration + 1u);
  EXPECT_TRUE(cancelled_workers_.empty());
  EXPECT_EQ(1u, supervisor_.active_job_count_for_testing());
}

// The library seam end to end: a job reaches a worker carrying both halves of
// the claim, the record that says what the archive is and the descriptor that
// is it. An interpreter handed neither has an empty import path and can load
// nothing at all.
TEST_F(ProfileToolSupervisorCancellationTest,
       AWorkerReceivesTheLibraryItNeeds) {
  supervisor_.SetPythonLibraryPort(base::BindRepeating(
      [](ProfileToolSupervisorCancellationTest* test,
         tool_job_resources::PythonLibrarySource* resolved) {
        resolved->file = test->OpenScratch("python-stdlib");
        resolved->library_id = "python.stdlib";
        resolved->library_version = "3.14.0";
        resolved->byte_length = 12582912u;
        resolved->digest.assign(32u, 5u);
        return resolved->file.IsValid();
      },
      base::Unretained(this)));

  Start("library", "task-1");
  ASSERT_EQ(1u, started_jobs_.size());
  ASSERT_EQ(1u, started_resources_.size());
  EXPECT_EQ("python.stdlib", started_jobs_.front()->python_library->library_id);
  EXPECT_EQ(12582912u,
            started_jobs_.front()->python_library->archive_bytes);
  EXPECT_EQ(std::vector<uint8_t>(32u, 5u),
            started_jobs_.front()->python_library->archive_digest);
  EXPECT_TRUE(started_resources_.front()->python_library.IsValid());
}

// Without a library register there is nothing to open, and the job is handed
// an empty declaration rather than refused: the Python start port already
// answers that the runtime is unsupported, which is the accurate reason.
TEST_F(ProfileToolSupervisorCancellationTest,
       WithNoRegisterNoLibraryIsClaimed) {
  Start("bare", "task-1");
  ASSERT_EQ(1u, started_jobs_.size());
  ASSERT_EQ(1u, started_resources_.size());
  EXPECT_TRUE(started_jobs_.front()->python_library->library_id.empty());
  EXPECT_EQ(0u, started_jobs_.front()->python_library->archive_bytes);
  EXPECT_FALSE(started_resources_.front()->python_library.IsValid());
}

}  // namespace
}  // namespace taffy
