// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"

#include <stdint.h>

#include <array>
#include <memory>
#include <string>
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
#include "taffy/services/tool-runtime/supervisor/tool_handle_broker.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace core = core_service::mojom;
namespace runtime = tool_runtime::mojom;

uint64_t NowMilliseconds() {
  return static_cast<uint64_t>(
      base::TimeTicks::Now().since_origin().InMilliseconds());
}

core::OperationEnvelopePtr Operation(std::string suffix,
                                     uint64_t generation = 7u) {
  return core::OperationEnvelope::New("operation-" + suffix, generation, 1u,
                                      NowMilliseconds() + 10'000u,
                                      "idempotency-" + suffix);
}

core::ToolJobEffectPtr PythonJob(std::string suffix) {
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
  return job;
}

core::ToolModelArtifactPtr Artifact() {
  return core::ToolModelArtifact::New(
      "model.small", "2026-08-01", core::ToolModelArtifactKind::kLitertTflite,
      4u, std::vector<uint8_t>(32u, 9u), "", 0u, std::vector<uint8_t>(32u, 0u));
}

core::ToolJobEffectPtr LocalModelJob(std::string suffix) {
  auto job = PythonJob(std::move(suffix));
  job->runtime = core::ToolRuntimeKind::kLocalModel;
  job->operation_kind = core::ToolOperation::kGenerateLocalModel;
  job->bundled_python.reset();
  job->local_model = core::LocalModelArguments::New(
      "conversation-1", std::vector<uint8_t>{'h', 'i'}, 64u, Artifact());
  return job;
}

core::ToolJobEffectPtr EmbeddingJob() {
  auto job = LocalModelJob("embed");
  job->operation_kind = core::ToolOperation::kEmbedLocalModel;
  job->local_model.reset();
  job->local_embedding = core::LocalEmbeddingArguments::New(
      Artifact(), std::vector<uint8_t>{'h', 'i'}, 64u, true);
  return job;
}

core::ToolJobEffectPtr WasmJob() {
  auto job = PythonJob("wasm");
  job->runtime = core::ToolRuntimeKind::kWasm;
  job->operation_kind = core::ToolOperation::kRunSignedWasmTransform;
  job->bundled_python.reset();
  job->signed_wasm = core::SignedWasmArguments::New("signed-transform",
                                                    std::vector<uint8_t>{1});
  return job;
}

class ProfileToolSupervisorTest : public testing::Test {
 public:
  ProfileToolSupervisorTest()
      : supervisor_(
            7u,
            ProfileToolSupervisor::PythonPorts(
                base::BindRepeating(&ProfileToolSupervisorTest::StartPython,
                                    base::Unretained(this)),
                base::BindRepeating(&ProfileToolSupervisorTest::CancelPython,
                                    base::Unretained(this))),
            ProfileToolSupervisor::LocalModelPorts(
                base::BindRepeating(&ProfileToolSupervisorTest::StartPython,
                                    base::Unretained(this)),
                base::BindRepeating(&ProfileToolSupervisorTest::CancelPython,
                                    base::Unretained(this))),
            ProfileToolSupervisor::MediaPorts(
                base::BindRepeating(&ProfileToolSupervisorTest::StartPython,
                                    base::Unretained(this)),
                base::BindRepeating(&ProfileToolSupervisorTest::CancelPython,
                                    base::Unretained(this)))) {}

  void StartPython(runtime::ToolJobPtr job,
                   runtime::ToolJobResourcesPtr resources,
                   mojo::PendingRemote<runtime::ToolRuntimeClient> client,
                   ProfileToolSupervisor::AdmissionCallback callback) {
    started_jobs_.push_back(job->Clone());
    started_resources_.push_back(std::move(resources));
    clients_.push_back(
        std::make_unique<mojo::Remote<runtime::ToolRuntimeClient>>(
            std::move(client)));
    admissions_.push_back(std::move(callback));
  }

  // A resource port that opens one real file, so a resolved handle is proven
  // by an actual descriptor rather than by a mock that says yes.
  base::File OpenScratch(const std::string& name) {
    if (!scratch_.IsValid()) {
      CHECK(scratch_.CreateUniqueTempDir());
    }
    return base::File(scratch_.GetPath().AppendASCII(name),
                      base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_READ |
                          base::File::FLAG_WRITE);
  }

  void InstallResourcePorts() {
    supervisor_.SetResourcePorts(
        base::BindRepeating(
            [](ProfileToolSupervisorTest* test, const std::string& handle_id,
               ToolHandleBroker::Mode) { return test->OpenScratch(handle_id); },
            base::Unretained(this)),
        base::BindRepeating(
            [](ProfileToolSupervisorTest* test, const std::string& model_id,
               const std::string&, bool,
               tool_job_resources::ModelArtifactSource* resolved) {
              resolved->file = test->OpenScratch(model_id);
              resolved->byte_length = 4u;
              resolved->digest.assign(32u, 9u);
              return resolved->file.IsValid()
                         ? tool_job_resources::ModelArtifactResolution::kResolved
                         : tool_job_resources::ModelArtifactResolution::kMissing;
            },
            base::Unretained(this)));
  }

  void InstallStreamSink() {
    supervisor_.SetStreamSink(base::BindRepeating(
        [](std::vector<core::ToolStreamChunkPtr>* sink,
           core::ToolStreamChunkPtr chunk) { sink->push_back(std::move(chunk)); },
        base::Unretained(&streamed_)));
  }

  void CancelPython(const std::string& job_id) {
    cancelled_jobs_.push_back(job_id);
  }

  void Start(std::string suffix) {
    supervisor_.Start(Operation(suffix), "effect-" + suffix, PythonJob(suffix),
                      base::BindOnce(&ProfileToolSupervisorTest::Collect,
                                     base::Unretained(this)));
  }

  void Collect(core::ToolEffectResultPtr result) {
    results_.push_back(std::move(result));
  }

  void Admit(size_t index,
             runtime::ToolAdmissionStatus status =
                 runtime::ToolAdmissionStatus::kAccepted) {
    std::move(admissions_.at(index))
        .Run(runtime::ToolAdmission::New(started_jobs_.at(index)->job_id,
                                         status));
  }

  runtime::OperationEnvelopePtr WorkerOperation(size_t index) {
    return started_jobs_.at(index)->operation->Clone();
  }

 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  base::ScopedTempDir scratch_;
  std::vector<runtime::ToolJobPtr> started_jobs_;
  std::vector<runtime::ToolJobResourcesPtr> started_resources_;
  std::vector<core::ToolStreamChunkPtr> streamed_;
  std::vector<std::unique_ptr<mojo::Remote<runtime::ToolRuntimeClient>>>
      clients_;
  std::vector<ProfileToolSupervisor::AdmissionCallback> admissions_;
  std::vector<std::string> cancelled_jobs_;
  std::vector<core::ToolEffectResultPtr> results_;
  ProfileToolSupervisor supervisor_;
};

TEST_F(ProfileToolSupervisorTest, TranslatesTypedStreamAndCompletion) {
  Start("complete");
  ASSERT_EQ(started_jobs_.size(), 1u);
  Admit(0u);
  clients_[0]->get()->Progress(
      runtime::ToolProgress::New("job-complete", 1u, 5000u));
  auto chunk = runtime::ToolOutputChunk::New();
  chunk->operation = WorkerOperation(0u);
  chunk->job_id = "job-complete";
  chunk->sequence = 1u;
  chunk->kind = runtime::ToolChunkKind::kTextUtf8;
  chunk->text = runtime::TextOutputChunk::New(std::vector<uint8_t>{'o', 'k'});
  clients_[0]->get()->OutputChunk(std::move(chunk));

  auto success = runtime::ToolSuccess::New();
  success->operation_kind = runtime::ToolOperation::kRunBundledPythonModule;
  success->bundled_python =
      runtime::BundledPythonResult::New(std::vector<uint8_t>{4, 2});
  clients_[0]->get()->Completed(runtime::ToolCompletion::New(
      WorkerOperation(0u), "job-complete",
      runtime::ToolTerminalStatus::kCompleted, std::move(success)));
  task_environment_.RunUntilIdle();

  ASSERT_EQ(results_.size(), 1u);
  EXPECT_EQ(results_[0]->status, core::ToolTerminalStatus::kCompleted);
  EXPECT_EQ(results_[0]->progress.size(), 1u);
  EXPECT_EQ(results_[0]->chunks.size(), 1u);
  ASSERT_TRUE(results_[0]->success);
  EXPECT_TRUE(results_[0]->success->bundled_python);
  EXPECT_EQ(supervisor_.active_job_count_for_testing(), 0u);
}

TEST_F(ProfileToolSupervisorTest, WasmIsReservedAndHasNoExecutor) {
  supervisor_.Start(Operation("wasm"), "effect-wasm", WasmJob(),
                    base::BindOnce(&ProfileToolSupervisorTest::Collect,
                                   base::Unretained(this)));
  ASSERT_EQ(results_.size(), 1u);
  EXPECT_EQ(results_[0]->status, core::ToolTerminalStatus::kUnsupported);
  EXPECT_TRUE(started_jobs_.empty());
  EXPECT_FALSE(results_[0]->success);
}

TEST_F(ProfileToolSupervisorTest, AdmissionBackpressureIsResourceLimit) {
  Start("backpressure");
  Admit(0u, runtime::ToolAdmissionStatus::kBackpressure);
  ASSERT_EQ(results_.size(), 1u);
  EXPECT_EQ(results_[0]->status, core::ToolTerminalStatus::kResourceLimit);
  EXPECT_FALSE(results_[0]->success);
}

TEST_F(ProfileToolSupervisorTest, DuplicateActiveIdentityFailsClosed) {
  Start("duplicate");
  supervisor_.Start(Operation("other"), "effect-duplicate", PythonJob("other"),
                    base::BindOnce(&ProfileToolSupervisorTest::Collect,
                                   base::Unretained(this)));

  ASSERT_EQ(started_jobs_.size(), 1u);
  ASSERT_EQ(results_.size(), 1u);
  EXPECT_EQ(results_[0]->status, core::ToolTerminalStatus::kInvalidInput);
  EXPECT_FALSE(results_[0]->success);
}

TEST_F(ProfileToolSupervisorTest, NinthConcurrentJobIsRefused) {
  for (size_t index = 0u; index < 8u; ++index) {
    Start(std::to_string(index));
  }
  Start("overflow");

  ASSERT_EQ(started_jobs_.size(), 8u);
  ASSERT_EQ(results_.size(), 1u);
  EXPECT_EQ(results_[0]->status, core::ToolTerminalStatus::kResourceLimit);
  EXPECT_FALSE(results_[0]->success);
}

TEST_F(ProfileToolSupervisorTest, OutputOverBudgetFailsOnceAndCancels) {
  Start("output-limit");
  Admit(0u);
  auto chunk = runtime::ToolOutputChunk::New();
  chunk->operation = WorkerOperation(0u);
  chunk->job_id = "job-output-limit";
  chunk->sequence = 1u;
  chunk->kind = runtime::ToolChunkKind::kTextUtf8;
  chunk->text = runtime::TextOutputChunk::New(std::vector<uint8_t>(1025u, 'x'));
  clients_[0]->get()->OutputChunk(std::move(chunk));
  task_environment_.RunUntilIdle();

  ASSERT_EQ(results_.size(), 1u);
  EXPECT_EQ(results_[0]->status, core::ToolTerminalStatus::kResourceLimit);
  EXPECT_EQ(cancelled_jobs_, std::vector<std::string>{"job-output-limit"});
  EXPECT_FALSE(results_[0]->success);
}

TEST_F(ProfileToolSupervisorTest, OutOfOrderProgressFailsOnceAndCancels) {
  Start("sequence");
  Admit(0u);
  clients_[0]->get()->Progress(
      runtime::ToolProgress::New("job-sequence", 2u, 100u));
  clients_[0]->get()->Progress(
      runtime::ToolProgress::New("job-sequence", 1u, 200u));
  task_environment_.RunUntilIdle();
  ASSERT_EQ(results_.size(), 1u);
  EXPECT_EQ(results_[0]->status, core::ToolTerminalStatus::kInvalidInput);
  EXPECT_EQ(cancelled_jobs_, std::vector<std::string>{"job-sequence"});
  EXPECT_GE(supervisor_.rejected_message_count_for_testing(), 1u);
}

TEST_F(ProfileToolSupervisorTest, DisconnectIsRuntimeCrashedWithoutBody) {
  Start("disconnect");
  Admit(0u);
  clients_[0].reset();
  task_environment_.RunUntilIdle();
  ASSERT_EQ(results_.size(), 1u);
  EXPECT_EQ(results_[0]->status, core::ToolTerminalStatus::kRuntimeCrashed);
  EXPECT_FALSE(results_[0]->success);
}

TEST_F(ProfileToolSupervisorTest, DeadlineCancelsWorker) {
  auto operation = Operation("deadline");
  operation->deadline_monotonic_ms = NowMilliseconds() + 5u;
  supervisor_.Start(std::move(operation), "effect-deadline",
                    PythonJob("deadline"),
                    base::BindOnce(&ProfileToolSupervisorTest::Collect,
                                   base::Unretained(this)));
  Admit(0u);
  task_environment_.FastForwardBy(base::Milliseconds(5));
  ASSERT_EQ(results_.size(), 1u);
  EXPECT_EQ(results_[0]->status, core::ToolTerminalStatus::kDeadlineExceeded);
  EXPECT_EQ(cancelled_jobs_, std::vector<std::string>{"job-deadline"});
}

TEST_F(ProfileToolSupervisorTest, GenerationChangeCancelsProfileJobs) {
  Start("generation");
  Admit(0u);
  supervisor_.SetActiveGeneration(8u);
  ASSERT_EQ(results_.size(), 1u);
  EXPECT_EQ(results_[0]->status, core::ToolTerminalStatus::kCancelled);
  EXPECT_EQ(cancelled_jobs_, std::vector<std::string>{"job-generation"});
}

TEST_F(ProfileToolSupervisorTest, StreamedChunksLeaveAtOnceAndEndOnce) {
  InstallResourcePorts();
  InstallStreamSink();
  supervisor_.Start(Operation("stream"), "effect-stream",
                    LocalModelJob("stream"),
                    base::BindOnce(&ProfileToolSupervisorTest::Collect,
                                   base::Unretained(this)));
  ASSERT_EQ(started_jobs_.size(), 1u);
  EXPECT_EQ(started_jobs_[0]->output_payload->transport,
            runtime::ToolOutputTransport::kStreamChunks);
  // The browser resolved the artifact, so what the worker holds is the
  // register's description of the model rather than the caller's.
  ASSERT_TRUE(started_resources_[0]->model_artifact.IsValid());
  EXPECT_EQ(started_jobs_[0]->local_model->model->artifact_bytes, 4u);
  Admit(0u);

  for (uint32_t sequence = 1u; sequence <= 2u; ++sequence) {
    auto chunk = runtime::ToolOutputChunk::New();
    chunk->operation = WorkerOperation(0u);
    chunk->job_id = "job-stream";
    chunk->sequence = sequence;
    chunk->kind = runtime::ToolChunkKind::kTextUtf8;
    chunk->text = runtime::TextOutputChunk::New(std::vector<uint8_t>{'o'});
    chunk->is_final = sequence == 2u;
    clients_[0]->get()->OutputChunk(std::move(chunk));
  }
  task_environment_.RunUntilIdle();
  ASSERT_EQ(streamed_.size(), 2u);
  EXPECT_FALSE(streamed_[0]->chunk->is_final);
  EXPECT_TRUE(streamed_[1]->chunk->is_final);
  EXPECT_EQ(streamed_[1]->effect_id, "effect-stream");

  // A chunk after the one that ended the stream is a defect, not more output.
  auto late = runtime::ToolOutputChunk::New();
  late->operation = WorkerOperation(0u);
  late->job_id = "job-stream";
  late->sequence = 3u;
  late->kind = runtime::ToolChunkKind::kTextUtf8;
  late->text = runtime::TextOutputChunk::New(std::vector<uint8_t>{'k'});
  clients_[0]->get()->OutputChunk(std::move(late));
  task_environment_.RunUntilIdle();

  ASSERT_EQ(results_.size(), 1u);
  EXPECT_EQ(results_[0]->status, core::ToolTerminalStatus::kInvalidInput);
  EXPECT_EQ(streamed_.size(), 2u);
  // Streamed chunks are counted, not kept: the terminal result says how many
  // already left rather than carrying them a second time.
  EXPECT_EQ(results_[0]->streamed_chunks, 2u);
  EXPECT_TRUE(results_[0]->chunks.empty());
}

TEST_F(ProfileToolSupervisorTest, AnEmbeddingReturnsOneBoundedVector) {
  InstallResourcePorts();
  supervisor_.Start(Operation("embed"), "effect-embed", EmbeddingJob(),
                    base::BindOnce(&ProfileToolSupervisorTest::Collect,
                                   base::Unretained(this)));
  ASSERT_EQ(started_jobs_.size(), 1u);
  Admit(0u);
  auto success = runtime::ToolSuccess::New();
  success->operation_kind = runtime::ToolOperation::kEmbedLocalModel;
  success->local_embedding = runtime::LocalEmbeddingResult::New(
      2u, runtime::EmbeddingElementKind::kFloat32Le,
      std::vector<uint8_t>(8u, 0u), 3u);
  clients_[0]->get()->Completed(runtime::ToolCompletion::New(
      WorkerOperation(0u), "job-embed",
      runtime::ToolTerminalStatus::kCompleted, std::move(success)));
  task_environment_.RunUntilIdle();

  ASSERT_EQ(results_.size(), 1u);
  EXPECT_EQ(results_[0]->status, core::ToolTerminalStatus::kCompleted);
  ASSERT_TRUE(results_[0]->success);
  ASSERT_TRUE(results_[0]->success->local_embedding);
  EXPECT_EQ(results_[0]->success->local_embedding->dimensions, 2u);
  EXPECT_EQ(results_[0]->success->local_embedding->values.size(), 8u);
}

TEST_F(ProfileToolSupervisorTest, AVectorWhoseLengthContradictsItsBytesFails) {
  InstallResourcePorts();
  supervisor_.Start(Operation("embed"), "effect-embed", EmbeddingJob(),
                    base::BindOnce(&ProfileToolSupervisorTest::Collect,
                                   base::Unretained(this)));
  ASSERT_EQ(started_jobs_.size(), 1u);
  Admit(0u);
  auto success = runtime::ToolSuccess::New();
  success->operation_kind = runtime::ToolOperation::kEmbedLocalModel;
  success->local_embedding = runtime::LocalEmbeddingResult::New(
      2u, runtime::EmbeddingElementKind::kFloat32Le,
      std::vector<uint8_t>(7u, 0u), 3u);
  clients_[0]->get()->Completed(runtime::ToolCompletion::New(
      WorkerOperation(0u), "job-embed",
      runtime::ToolTerminalStatus::kCompleted, std::move(success)));
  task_environment_.RunUntilIdle();

  ASSERT_EQ(results_.size(), 1u);
  EXPECT_EQ(results_[0]->status, core::ToolTerminalStatus::kInvalidInput);
  EXPECT_FALSE(results_[0]->success);
}

}  // namespace
}  // namespace taffy
