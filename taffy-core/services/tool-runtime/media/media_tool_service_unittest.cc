// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/tool-runtime/media/media_tool_service.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "media/base/test_data_util.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "taffy/services/tool-runtime/media/media_transform.h"
#include "taffy/services/tool-runtime/media/test/synthetic_media.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = tool_runtime::mojom;

// Records what a worker publishes, so a test can assert on the terminal result
// rather than on the fact that something was called.
class RecordingClient final : public mojom::ToolRuntimeClient {
 public:
  explicit RecordingClient(
      mojo::PendingReceiver<mojom::ToolRuntimeClient> receiver)
      : receiver_(this, std::move(receiver)) {}

  void Progress(mojom::ToolProgressPtr progress) override { ++progress_count_; }
  void OutputChunk(mojom::ToolOutputChunkPtr chunk) override { ++chunk_count_; }
  void Completed(mojom::ToolCompletionPtr completion) override {
    completions_.push_back(std::move(completion));
    if (!completed_.IsReady()) {
      completed_.SetValue(true);
    }
  }

  const std::vector<mojom::ToolCompletionPtr>& completions() const {
    return completions_;
  }
  size_t progress_count() const { return progress_count_; }
  size_t chunk_count() const { return chunk_count_; }
  bool WaitForCompletion() { return completed_.Wait(); }

 private:
  mojo::Receiver<mojom::ToolRuntimeClient> receiver_;
  std::vector<mojom::ToolCompletionPtr> completions_;
  size_t progress_count_ = 0u;
  size_t chunk_count_ = 0u;
  base::test::TestFuture<bool> completed_;
};

// Everything a media job needs, minus the task environment: the two fixtures
// below differ only in whether the thread pool runs on its own.
class MediaToolServiceTestBase : public ::testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

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
    // This runtime runs no interpreter, so the library declaration is the
    // empty one every job outside the Python family carries.
    job->python_library = mojom::ToolPythonLibrary::New();
    job->python_library->archive_digest.resize(32u);
    return job;
  }

  mojom::ToolJobResourcesPtr ProbeResources(uint32_t duration_ms) {
    auto resources = mojom::ToolJobResources::New();
    resources->media_input = media_tool::test::WriteReadOnlyFile(
        temp_dir_.GetPath(), media_tool::test::BuildSilentWave(duration_ms));
    return resources;
  }

  mojom::ToolJobPtr TransformJob(const std::string& job_id,
                                 mojom::ToolOperation operation) {
    mojom::ToolJobPtr job = ProbeJob(job_id);
    job->media_probe.reset();
    job->operation_kind = operation;
    job->budget->max_output_bytes = 16u << 20;
    job->budget->max_temporary_bytes = 16u << 20;
    job->output_payload->max_byte_length = 16u << 20;
    if (operation == mojom::ToolOperation::kExtractAudio) {
      job->tool_id = "media.audio.extract";
      job->audio_extract = mojom::AudioExtractArguments::New(
          "input-" + job_id, "output-" + job_id,
          std::string(media_tool::kAudioExtractPreset));
    } else if (operation == mojom::ToolOperation::kSampleFrames) {
      job->tool_id = "media.frames.sample";
      job->frame_sample = mojom::FrameSampleArguments::New(
          "input-" + job_id, "output-" + job_id,
          std::string(media_tool::kFrameSamplePreset), 3u);
    } else if (operation == mojom::ToolOperation::kTranscodePreset) {
      job->tool_id = "media.transcode";
      job->transcode = mojom::TranscodeArguments::New(
          "input-" + job_id, "output-" + job_id,
          std::string(media_tool::kTranscodePreset));
    }
    return job;
  }

  mojom::ToolJobResourcesPtr TransformResources(base::span<const uint8_t> input,
                                                base::FilePath* output_path) {
    auto resources = mojom::ToolJobResources::New();
    resources->media_input =
        media_tool::test::WriteReadOnlyFile(temp_dir_.GetPath(), input);
    if (!base::CreateTemporaryFileInDir(temp_dir_.GetPath(), output_path)) {
      return resources;
    }
    resources->media_output =
        base::File(*output_path, base::File::FLAG_OPEN | base::File::FLAG_READ |
                                     base::File::FLAG_WRITE);
    return resources;
  }

  base::ScopedTempDir temp_dir_;
};

class MediaToolServiceTest : public MediaToolServiceTestBase {
 protected:
  base::test::TaskEnvironment task_environment_;
};

TEST_F(MediaToolServiceTest, ProbesOneJobAndPublishesExactlyOneCompletion) {
  mojo::Remote<mojom::MediaToolService> service;
  MediaToolServiceImpl impl(service.BindNewPipeAndPassReceiver());
  mojo::PendingRemote<mojom::ToolRuntimeClient> client_remote;
  RecordingClient client(client_remote.InitWithNewPipeAndPassReceiver());

  base::test::TestFuture<mojom::ToolAdmissionPtr> admitted;
  service->Start(ProbeJob("job-1"), ProbeResources(1500u),
                 std::move(client_remote), admitted.GetCallback());
  const mojom::ToolAdmissionPtr admission = admitted.Take();
  ASSERT_TRUE(admission);
  EXPECT_EQ("job-1", admission->job_id);
  ASSERT_EQ(mojom::ToolAdmissionStatus::kAccepted, admission->status);

  ASSERT_TRUE(client.WaitForCompletion());
  task_environment_.RunUntilIdle();
  ASSERT_EQ(1u, client.completions().size());
  const mojom::ToolCompletionPtr& completion = client.completions().front();
  EXPECT_EQ("job-1", completion->job_id);
  EXPECT_EQ(mojom::ToolTerminalStatus::kCompleted, completion->status);
  ASSERT_TRUE(completion->operation);
  EXPECT_EQ("op-job-1", completion->operation->operation_id);
  ASSERT_TRUE(completion->success);
  EXPECT_EQ(mojom::ToolOperation::kProbeMedia,
            completion->success->operation_kind);
  ASSERT_TRUE(completion->success->media_probe);
  EXPECT_EQ(1u, completion->success->media_probe->audio_streams);
  EXPECT_EQ(0u, completion->success->media_probe->video_streams);
  // A probe carries its whole answer in the terminal result; a chunk stream
  // would be output nothing on this seam drains.
  EXPECT_EQ(0u, client.chunk_count());
}

TEST_F(MediaToolServiceTest, RefusesASecondJobRatherThanQueueingIt) {
  mojo::Remote<mojom::MediaToolService> service;
  MediaToolServiceImpl impl(service.BindNewPipeAndPassReceiver());
  mojo::PendingRemote<mojom::ToolRuntimeClient> first_remote;
  RecordingClient first(first_remote.InitWithNewPipeAndPassReceiver());
  mojo::PendingRemote<mojom::ToolRuntimeClient> second_remote;
  RecordingClient second(second_remote.InitWithNewPipeAndPassReceiver());

  base::test::TestFuture<mojom::ToolAdmissionPtr> one;
  service->Start(ProbeJob("job-1"), ProbeResources(1000u),
                 std::move(first_remote), one.GetCallback());
  ASSERT_EQ(mojom::ToolAdmissionStatus::kAccepted, one.Take()->status);

  base::test::TestFuture<mojom::ToolAdmissionPtr> two;
  service->Start(ProbeJob("job-2"), ProbeResources(1000u),
                 std::move(second_remote), two.GetCallback());
  const mojom::ToolAdmissionPtr refused = two.Take();
  EXPECT_EQ("job-2", refused->job_id);
  EXPECT_EQ(mojom::ToolAdmissionStatus::kBackpressure, refused->status);

  ASSERT_TRUE(first.WaitForCompletion());
  task_environment_.RunUntilIdle();
  EXPECT_EQ(1u, first.completions().size());
  // The refused job is owed no completion: it never started.
  EXPECT_TRUE(second.completions().empty());
}

TEST_F(MediaToolServiceTest, RefusesEveryOperationThisRuntimeCannotHonour) {
  struct Case {
    const char* name;
    mojom::ToolOperation operation;
  } const cases[] = {
      {"python", mojom::ToolOperation::kRunBundledPythonModule},
      {"generate", mojom::ToolOperation::kGenerateLocalModel},
      {"embed", mojom::ToolOperation::kEmbedLocalModel},
      {"wasm", mojom::ToolOperation::kRunSignedWasmTransform},
  };
  for (const Case& test_case : cases) {
    SCOPED_TRACE(test_case.name);
    mojo::Remote<mojom::MediaToolService> service;
    MediaToolServiceImpl impl(service.BindNewPipeAndPassReceiver());
    mojo::PendingRemote<mojom::ToolRuntimeClient> client_remote;
    RecordingClient client(client_remote.InitWithNewPipeAndPassReceiver());
    mojom::ToolJobPtr job = ProbeJob(test_case.name);
    job->operation_kind = test_case.operation;
    base::test::TestFuture<mojom::ToolAdmissionPtr> admitted;
    service->Start(std::move(job), ProbeResources(500u),
                   std::move(client_remote), admitted.GetCallback());
    EXPECT_EQ(mojom::ToolAdmissionStatus::kUnsupported,
              admitted.Take()->status);
  }
}

TEST_F(MediaToolServiceTest, ExtractsStereoAudioIntoCanonicalPcmWave) {
  mojo::Remote<mojom::MediaToolService> service;
  MediaToolServiceImpl impl(service.BindNewPipeAndPassReceiver());
  mojo::PendingRemote<mojom::ToolRuntimeClient> client_remote;
  RecordingClient client(client_remote.InitWithNewPipeAndPassReceiver());
  base::FilePath output_path;
  mojom::ToolJobResourcesPtr resources = TransformResources(
      media_tool::test::BuildSilentWave(500u, 2u), &output_path);

  base::test::TestFuture<mojom::ToolAdmissionPtr> admitted;
  service->Start(TransformJob("extract", mojom::ToolOperation::kExtractAudio),
                 std::move(resources), std::move(client_remote),
                 admitted.GetCallback());
  ASSERT_EQ(mojom::ToolAdmissionStatus::kAccepted, admitted.Take()->status);
  ASSERT_TRUE(client.WaitForCompletion());
  task_environment_.RunUntilIdle();
  ASSERT_EQ(1u, client.completions().size());
  const mojom::ToolCompletionPtr& completion = client.completions().front();
  ASSERT_EQ(mojom::ToolTerminalStatus::kCompleted, completion->status);
  ASSERT_TRUE(completion->success);
  ASSERT_TRUE(completion->success->audio_extract);
  EXPECT_EQ("output-extract",
            completion->success->audio_extract->output_handle);
  const std::optional<std::vector<uint8_t>> bytes =
      base::ReadFileToBytes(output_path);
  ASSERT_TRUE(bytes);
  ASSERT_GE(bytes->size(), 44u);
  EXPECT_EQ('R', (*bytes)[0]);
  EXPECT_EQ('W', (*bytes)[8]);
  EXPECT_EQ(2u, (*bytes)[22]);
  EXPECT_EQ(bytes->size(), completion->success->audio_extract->output_bytes);
}

TEST_F(MediaToolServiceTest, TranscodePresetDownmixesStereoToMono) {
  mojo::Remote<mojom::MediaToolService> service;
  MediaToolServiceImpl impl(service.BindNewPipeAndPassReceiver());
  mojo::PendingRemote<mojom::ToolRuntimeClient> client_remote;
  RecordingClient client(client_remote.InitWithNewPipeAndPassReceiver());
  base::FilePath output_path;
  mojom::ToolJobResourcesPtr resources = TransformResources(
      media_tool::test::BuildSilentWave(500u, 2u), &output_path);

  base::test::TestFuture<mojom::ToolAdmissionPtr> admitted;
  service->Start(
      TransformJob("transcode", mojom::ToolOperation::kTranscodePreset),
      std::move(resources), std::move(client_remote), admitted.GetCallback());
  ASSERT_EQ(mojom::ToolAdmissionStatus::kAccepted, admitted.Take()->status);
  ASSERT_TRUE(client.WaitForCompletion());
  task_environment_.RunUntilIdle();
  ASSERT_EQ(mojom::ToolTerminalStatus::kCompleted,
            client.completions().front()->status);
  ASSERT_TRUE(client.completions().front()->success->transcode);
  const std::optional<std::vector<uint8_t>> bytes =
      base::ReadFileToBytes(output_path);
  ASSERT_TRUE(bytes);
  ASSERT_GE(bytes->size(), 44u);
  EXPECT_EQ(1u, (*bytes)[22]);
  EXPECT_EQ(bytes->size(),
            client.completions().front()->success->transcode->output_bytes);
}

TEST_F(MediaToolServiceTest, SamplesBoundedPngFramesIntoDeterministicZip) {
  mojo::Remote<mojom::MediaToolService> service;
  MediaToolServiceImpl impl(service.BindNewPipeAndPassReceiver());
  mojo::PendingRemote<mojom::ToolRuntimeClient> client_remote;
  RecordingClient client(client_remote.InitWithNewPipeAndPassReceiver());
  auto resources = mojom::ToolJobResources::New();
  resources->media_input =
      base::File(media::GetTestDataFilePath("bear.webm"),
                 base::File::FLAG_OPEN | base::File::FLAG_READ);
  base::FilePath output_path;
  ASSERT_TRUE(
      base::CreateTemporaryFileInDir(temp_dir_.GetPath(), &output_path));
  resources->media_output =
      base::File(output_path, base::File::FLAG_OPEN | base::File::FLAG_READ |
                                  base::File::FLAG_WRITE);

  base::test::TestFuture<mojom::ToolAdmissionPtr> admitted;
  service->Start(TransformJob("frames", mojom::ToolOperation::kSampleFrames),
                 std::move(resources), std::move(client_remote),
                 admitted.GetCallback());
  ASSERT_EQ(mojom::ToolAdmissionStatus::kAccepted, admitted.Take()->status);
  ASSERT_TRUE(client.WaitForCompletion());
  task_environment_.RunUntilIdle();
  ASSERT_EQ(mojom::ToolTerminalStatus::kCompleted,
            client.completions().front()->status);
  ASSERT_TRUE(client.completions().front()->success->frame_sample);
  EXPECT_EQ(3u,
            client.completions().front()->success->frame_sample->frame_count);
  const std::optional<std::vector<uint8_t>> bytes =
      base::ReadFileToBytes(output_path);
  ASSERT_TRUE(bytes);
  ASSERT_GE(bytes->size(), 4u);
  EXPECT_EQ(0x50u, (*bytes)[0]);
  EXPECT_EQ(0x4bu, (*bytes)[1]);
  EXPECT_EQ(bytes->size(),
            client.completions().front()->success->frame_sample->output_bytes);
}

TEST_F(MediaToolServiceTest, RefusesAnUnreviewedMediaPresetBeforeReading) {
  mojo::Remote<mojom::MediaToolService> service;
  MediaToolServiceImpl impl(service.BindNewPipeAndPassReceiver());
  mojo::PendingRemote<mojom::ToolRuntimeClient> client_remote;
  RecordingClient client(client_remote.InitWithNewPipeAndPassReceiver());
  mojom::ToolJobPtr job =
      TransformJob("extract", mojom::ToolOperation::kExtractAudio);
  job->audio_extract->preset_id = "ffmpeg-flags-from-model";
  base::FilePath output_path;
  mojom::ToolJobResourcesPtr resources =
      TransformResources(media_tool::test::BuildSilentWave(500u), &output_path);

  base::test::TestFuture<mojom::ToolAdmissionPtr> admitted;
  service->Start(std::move(job), std::move(resources), std::move(client_remote),
                 admitted.GetCallback());
  EXPECT_EQ(mojom::ToolAdmissionStatus::kUnsupported, admitted.Take()->status);
  EXPECT_TRUE(client.completions().empty());
}

TEST_F(MediaToolServiceTest, RefusesAResourceTheOperationHasNoUseFor) {
  mojo::Remote<mojom::MediaToolService> service;
  MediaToolServiceImpl impl(service.BindNewPipeAndPassReceiver());
  mojo::PendingRemote<mojom::ToolRuntimeClient> client_remote;
  RecordingClient client(client_remote.InitWithNewPipeAndPassReceiver());

  mojom::ToolJobResourcesPtr resources = ProbeResources(500u);
  // A writable descriptor beside a probe is authority the operation never
  // spends, so it is refused rather than ignored.
  resources->media_output = media_tool::test::WriteReadOnlyFile(
      temp_dir_.GetPath(), std::vector<uint8_t>(16u, 0u));
  ASSERT_TRUE(resources->media_output.IsValid());

  base::test::TestFuture<mojom::ToolAdmissionPtr> admitted;
  service->Start(ProbeJob("job-1"), std::move(resources),
                 std::move(client_remote), admitted.GetCallback());
  EXPECT_EQ(mojom::ToolAdmissionStatus::kInvalidJob, admitted.Take()->status);
}

TEST_F(MediaToolServiceTest, RefusesAJobWithNoResourceAtAll) {
  mojo::Remote<mojom::MediaToolService> service;
  MediaToolServiceImpl impl(service.BindNewPipeAndPassReceiver());
  mojo::PendingRemote<mojom::ToolRuntimeClient> client_remote;
  RecordingClient client(client_remote.InitWithNewPipeAndPassReceiver());

  base::test::TestFuture<mojom::ToolAdmissionPtr> admitted;
  service->Start(ProbeJob("job-1"), mojom::ToolJobResources::New(),
                 std::move(client_remote), admitted.GetCallback());
  EXPECT_EQ(mojom::ToolAdmissionStatus::kInvalidJob, admitted.Take()->status);
}

TEST_F(MediaToolServiceTest, RefusesAPlanThatDoesNotDescribeAProbe) {
  mojo::Remote<mojom::MediaToolService> service;
  MediaToolServiceImpl impl(service.BindNewPipeAndPassReceiver());
  mojo::PendingRemote<mojom::ToolRuntimeClient> client_remote;
  RecordingClient client(client_remote.InitWithNewPipeAndPassReceiver());

  mojom::ToolJobPtr job = ProbeJob("job-1");
  job->output_payload->transport = mojom::ToolOutputTransport::kDataPipe;
  base::test::TestFuture<mojom::ToolAdmissionPtr> admitted;
  service->Start(std::move(job), ProbeResources(500u), std::move(client_remote),
                 admitted.GetCallback());
  EXPECT_EQ(mojom::ToolAdmissionStatus::kInvalidJob, admitted.Take()->status);
}

// The cancel cases run with the thread pool held back, so the order the two
// halves arrive in is the test's rather than the scheduler's. Without it a
// parse that happens to finish first turns "cancel ends the job" into a coin
// toss, and a green run would mean nothing.
class MediaToolServiceCancelTest : public MediaToolServiceTestBase {
 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadPoolExecutionMode::QUEUED};
};

TEST_F(MediaToolServiceCancelTest, ACancelForAnotherJobEndsNothing) {
  mojo::Remote<mojom::MediaToolService> service;
  MediaToolServiceImpl impl(service.BindNewPipeAndPassReceiver());
  mojo::PendingRemote<mojom::ToolRuntimeClient> client_remote;
  RecordingClient client(client_remote.InitWithNewPipeAndPassReceiver());

  base::test::TestFuture<mojom::ToolAdmissionPtr> admitted;
  service->Start(ProbeJob("job-1"), ProbeResources(1000u),
                 std::move(client_remote), admitted.GetCallback());
  ASSERT_EQ(mojom::ToolAdmissionStatus::kAccepted, admitted.Take()->status);

  // Whichever way the two are ordered, an identifier that is not this job's
  // ends nothing: the reading is published, once.
  service->Cancel("job-2");
  task_environment_.RunUntilIdle();
  ASSERT_EQ(1u, client.completions().size());
  EXPECT_EQ(mojom::ToolTerminalStatus::kCompleted,
            client.completions().front()->status);
}

TEST_F(MediaToolServiceCancelTest,
       ACancelEndsTheJobOnceAndTheReadingIsDropped) {
  mojo::Remote<mojom::MediaToolService> service;
  MediaToolServiceImpl impl(service.BindNewPipeAndPassReceiver());
  mojo::PendingRemote<mojom::ToolRuntimeClient> client_remote;
  RecordingClient client(client_remote.InitWithNewPipeAndPassReceiver());

  base::test::TestFuture<mojom::ToolAdmissionPtr> admitted;
  service->Start(ProbeJob("job-1"), ProbeResources(4000u),
                 std::move(client_remote), admitted.GetCallback());
  ASSERT_EQ(mojom::ToolAdmissionStatus::kAccepted, admitted.Take()->status);

  // The parse is queued and cannot run yet, so the cancel is unambiguously
  // first.
  service->Cancel("job-1");
  ASSERT_TRUE(client.WaitForCompletion());
  ASSERT_EQ(1u, client.completions().size());
  EXPECT_EQ(mojom::ToolTerminalStatus::kCancelled,
            client.completions().front()->status);
  EXPECT_FALSE(client.completions().front()->success);

  // Now let the parse finish. Its reading arrives after the job already ended,
  // and is dropped rather than published as a second terminal result.
  task_environment_.RunUntilIdle();
  EXPECT_EQ(1u, client.completions().size());
}

}  // namespace
}  // namespace taffy
