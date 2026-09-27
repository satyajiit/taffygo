// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/services/tool-runtime/supervisor/tool_job_resources.h"

#include <stdint.h>

#include <string>
#include <utility>
#include <vector>

#include "base/files/file.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "taffy/contracts/tool-runtime/generated/mojom/tool_runtime.mojom.h"
#include "taffy/contracts/tool-runtime/tool_runtime_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::tool_job_resources {
namespace {

namespace runtime = tool_runtime::mojom;

constexpr size_t kDigestBytes = 32u;

runtime::ToolModelArtifactPtr Artifact() {
  return runtime::ToolModelArtifact::New(
      "model.small", "2026-08-01", runtime::ToolModelArtifactKind::kLitertTflite,
      1u, std::vector<uint8_t>(kDigestBytes, 1u), "", 0u,
      std::vector<uint8_t>(kDigestBytes, 0u));
}

runtime::ToolJobPtr Job(runtime::ToolOperation operation) {
  auto job = runtime::ToolJob::New();
  job->operation = runtime::OperationEnvelope::New("operation-1", 1u, 1u,
                                                   1000u, "idempotency-1");
  job->job_id = "job-1";
  job->tool_id = "tool";
  job->tool_version = "1";
  job->operation_kind = operation;
  job->budget = runtime::ResourceBudget::New(
      runtime::kMaxJobInputBytes, 4096u, 4096u, 1000u, 0u, 4u);
  switch (operation) {
    case runtime::ToolOperation::kRunBundledPythonModule:
      job->runtime = runtime::ToolRuntimeKind::kPython;
      job->bundled_python = runtime::BundledPythonArguments::New(
          "entrypoint", std::vector<uint8_t>{1});
      break;
    case runtime::ToolOperation::kGenerateLocalModel:
      job->runtime = runtime::ToolRuntimeKind::kLocalModel;
      job->local_model = runtime::LocalModelArguments::New(
          "conversation", std::vector<uint8_t>{1}, 8u, Artifact());
      break;
    case runtime::ToolOperation::kProbeMedia:
      job->runtime = runtime::ToolRuntimeKind::kMedia;
      job->media_probe = runtime::MediaProbeArguments::New("unminted");
      break;
    default:
      break;
  }
  job->python_library = runtime::ToolPythonLibrary::New(
      std::string(), std::string(), 0u,
      std::vector<uint8_t>(kDigestBytes, 0u));
  return job;
}

class ToolJobResourcesTest : public testing::Test {
 public:
  void SetUp() override { ASSERT_TRUE(scratch_.CreateUniqueTempDir()); }

  base::File Open(const std::string& name) {
    return base::File(scratch_.GetPath().AppendASCII(name),
                      base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_READ |
                          base::File::FLAG_WRITE);
  }

  ToolHandleBroker::ResolvePort ResolvePort() {
    return base::BindRepeating(
        [](ToolJobResourcesTest* test, const std::string& handle_id,
           ToolHandleBroker::Mode) { return test->Open(handle_id); },
        base::Unretained(this));
  }

  // A register whose opened descriptor and facts exactly match Artifact().
  ModelArtifactPort RegisterPort() {
    return base::BindRepeating(
        [](ToolJobResourcesTest* test, const std::string& model_id,
           const std::string&, bool, ModelArtifactSource* resolved) {
          resolved->file = test->Open(model_id);
          resolved->byte_length = 1u;
          resolved->digest.assign(kDigestBytes, 1u);
          resolved->kind = runtime::ToolModelArtifactKind::kLitertTflite;
          return resolved->file.IsValid()
                     ? ModelArtifactResolution::kResolved
                     : ModelArtifactResolution::kMissing;
        },
        base::Unretained(this));
  }

  // The build's record of the one standard library it packaged. It takes no
  // identity because a job may not ask for a different one.
  PythonLibraryPort LibraryPort() {
    return base::BindRepeating(
        [](ToolJobResourcesTest* test, PythonLibrarySource* resolved) {
          resolved->file = test->Open("python-stdlib");
          resolved->library_id = "python.stdlib";
          resolved->library_version = "3.14.0";
          resolved->byte_length = 12582912u;
          resolved->digest.assign(kDigestBytes, 9u);
          return resolved->file.IsValid();
        },
        base::Unretained(this));
  }

 protected:
  base::ScopedTempDir scratch_;
  ToolHandleBroker broker_;
};

TEST_F(ToolJobResourcesTest, SmallInputStaysInTheMessage) {
  auto job = Job(runtime::ToolOperation::kRunBundledPythonModule);
  PlanPayloads(false, job.get());
  EXPECT_EQ(job->input_payload->transport, runtime::ToolInputTransport::kInline);
  EXPECT_EQ(job->input_payload->byte_length, 1u);
  EXPECT_EQ(job->input_payload->digest.size(), kDigestBytes);
}

TEST_F(ToolJobResourcesTest, InputPastTheInlineBoundPlansForSharedMemory) {
  auto job = Job(runtime::ToolOperation::kRunBundledPythonModule);
  const size_t length = runtime::kMaxInlinePayloadBytes + 1u;
  job->bundled_python->input.assign(length, 'x');
  PlanPayloads(false, job.get());
  EXPECT_EQ(job->input_payload->transport,
            runtime::ToolInputTransport::kSharedMemory);
  EXPECT_EQ(job->input_payload->byte_length, length);

  runtime::ToolJobResourcesPtr resources;
  ASSERT_EQ(BindResult::kBound,
            Bind(ModelArtifactPort(), PythonLibraryPort(), &broker_, job.get(),
                 &resources));
  ASSERT_TRUE(resources->bulk_input.IsValid());
  EXPECT_EQ(resources->bulk_input.GetSize(), length);
  // The two carriers are alternatives: the record no longer holds a copy.
  EXPECT_TRUE(job->bundled_python->input.empty());
}

TEST_F(ToolJobResourcesTest, NothingStreamsWhileNothingReceivesAStream) {
  auto job = Job(runtime::ToolOperation::kGenerateLocalModel);
  PlanPayloads(false, job.get());
  EXPECT_EQ(job->output_payload->transport,
            runtime::ToolOutputTransport::kInlineChunks);
  PlanPayloads(true, job.get());
  EXPECT_EQ(job->output_payload->transport,
            runtime::ToolOutputTransport::kStreamChunks);
  EXPECT_EQ(job->output_payload->max_byte_length, 4096u);
}

TEST_F(ToolJobResourcesTest, ThePlanNeverSelectsTheTransportWithNoDrain) {
  // DATA_PIPE is expressible on this wire and has no browser side: the drain,
  // its bounds and its failure modes do not exist. The plan therefore never
  // selects it, for any operation, with or without a stream sink — which is
  // what keeps `ProfileToolSupervisor::Start`'s refusal of it a backstop
  // rather than the only thing standing between a worker and a producer
  // nobody reads.
  for (const runtime::ToolOperation operation :
       {runtime::ToolOperation::kRunBundledPythonModule,
        runtime::ToolOperation::kGenerateLocalModel,
        runtime::ToolOperation::kProbeMedia}) {
    for (const bool streaming : {false, true}) {
      auto job = Job(operation);
      PlanPayloads(streaming, job.get());
      EXPECT_NE(job->output_payload->transport,
                runtime::ToolOutputTransport::kDataPipe);
    }
  }
}

TEST_F(ToolJobResourcesTest, OnlyGenerationStreams) {
  auto job = Job(runtime::ToolOperation::kRunBundledPythonModule);
  PlanPayloads(true, job.get());
  EXPECT_EQ(job->output_payload->transport,
            runtime::ToolOutputTransport::kInlineChunks);
}

TEST_F(ToolJobResourcesTest, AModelWithNoRegisterIsRefused) {
  auto job = Job(runtime::ToolOperation::kGenerateLocalModel);
  PlanPayloads(false, job.get());
  runtime::ToolJobResourcesPtr resources;
  EXPECT_EQ(BindResult::kModelArtifactMissing,
            Bind(ModelArtifactPort(), PythonLibraryPort(), &broker_, job.get(),
                 &resources));
  EXPECT_FALSE(resources);
}

TEST_F(ToolJobResourcesTest, ExactRegisteredFactsCarryTheOpenedModel) {
  auto job = Job(runtime::ToolOperation::kGenerateLocalModel);
  PlanPayloads(false, job.get());
  runtime::ToolJobResourcesPtr resources;
  ASSERT_EQ(BindResult::kBound,
            Bind(RegisterPort(), PythonLibraryPort(), &broker_, job.get(),
                 &resources));
  ASSERT_TRUE(resources->model_artifact.IsValid());
  // The declaration is preserved rather than silently repaired into another
  // job; the descriptor is admitted only because every fact matched.
  EXPECT_EQ(job->local_model->model->artifact_bytes, 1u);
  EXPECT_EQ(job->local_model->model->artifact_digest,
            std::vector<uint8_t>(kDigestBytes, 1u));
  EXPECT_EQ(job->local_model->model->kind,
            runtime::ToolModelArtifactKind::kLitertTflite);
  // The identity is the one thing the caller does choose.
  EXPECT_EQ(job->local_model->model->model_id, "model.small");
  // No adapter was named, so the declaration must already carry the exact
  // empty representation.
  EXPECT_FALSE(resources->model_adapter.IsValid());
  EXPECT_EQ(job->local_model->model->adapter_bytes, 0u);
  EXPECT_EQ(job->local_model->model->adapter_digest,
            std::vector<uint8_t>(kDigestBytes, 0u));
}

TEST_F(ToolJobResourcesTest, AWrongRegisteredDigestIsRefused) {
  auto job = Job(runtime::ToolOperation::kGenerateLocalModel);
  job->local_model->model->artifact_digest.assign(kDigestBytes, 2u);
  PlanPayloads(false, job.get());
  runtime::ToolJobResourcesPtr resources;
  EXPECT_EQ(BindResult::kModelArtifactIncompatible,
            Bind(RegisterPort(), PythonLibraryPort(), &broker_, job.get(),
                 &resources));
  EXPECT_FALSE(resources);
}

TEST_F(ToolJobResourcesTest, AWrongRegisteredLengthIsRefused) {
  auto job = Job(runtime::ToolOperation::kGenerateLocalModel);
  job->local_model->model->artifact_bytes = 2u;
  PlanPayloads(false, job.get());
  runtime::ToolJobResourcesPtr resources;
  EXPECT_EQ(BindResult::kModelArtifactIncompatible,
            Bind(RegisterPort(), PythonLibraryPort(), &broker_, job.get(),
                 &resources));
  EXPECT_FALSE(resources);
}

TEST_F(ToolJobResourcesTest, AWrongRegisteredFormatIsRefused) {
  auto job = Job(runtime::ToolOperation::kGenerateLocalModel);
  job->local_model->model->kind = runtime::ToolModelArtifactKind::kGguf;
  PlanPayloads(false, job.get());
  runtime::ToolJobResourcesPtr resources;
  EXPECT_EQ(BindResult::kModelArtifactIncompatible,
            Bind(RegisterPort(), PythonLibraryPort(), &broker_, job.get(),
                 &resources));
  EXPECT_FALSE(resources);
}

TEST_F(ToolJobResourcesTest, UnnamedAdapterFactsMustBeExactlyEmpty) {
  auto job = Job(runtime::ToolOperation::kGenerateLocalModel);
  job->local_model->model->adapter_bytes = 1u;
  PlanPayloads(false, job.get());
  runtime::ToolJobResourcesPtr resources;
  EXPECT_EQ(BindResult::kModelArtifactIncompatible,
            Bind(RegisterPort(), PythonLibraryPort(), &broker_, job.get(),
                 &resources));
  EXPECT_FALSE(resources);
}

TEST_F(ToolJobResourcesTest, AHandleNobodyMintedOpensNothing) {
  broker_.SetResolvePort(ResolvePort());
  auto job = Job(runtime::ToolOperation::kProbeMedia);
  PlanPayloads(false, job.get());
  runtime::ToolJobResourcesPtr resources;
  EXPECT_EQ(BindResult::kInvalidJob,
            Bind(ModelArtifactPort(), PythonLibraryPort(), &broker_, job.get(),
                 &resources));
}

TEST_F(ToolJobResourcesTest, AMintedHandleArrivesAsAnOpenDescriptor) {
  broker_.SetResolvePort(ResolvePort());
  const std::string handle =
      broker_.Mint("job-1", ToolHandleBroker::Mode::kRead);
  auto job = Job(runtime::ToolOperation::kProbeMedia);
  job->media_probe->input_handle = handle;
  PlanPayloads(false, job.get());
  runtime::ToolJobResourcesPtr resources;
  ASSERT_EQ(BindResult::kBound,
            Bind(ModelArtifactPort(), PythonLibraryPort(), &broker_, job.get(),
                 &resources));
  EXPECT_TRUE(resources->media_input.IsValid());
  // The worker still receives only the label; the bytes are the descriptor.
  EXPECT_EQ(job->media_probe->input_handle, handle);
}

// An interpreter reaching an empty import path with no archive imports
// nothing, so this is the field that decides whether a Python job can run at
// all. Until a library is packaged there is none to open, and the honest shape
// is an empty declaration with no descriptor beside it - not a job the browser
// calls malformed, because the job is not.
TEST_F(ToolJobResourcesTest, WithNoLibraryPackagedAPythonJobDeclaresNone) {
  auto job = Job(runtime::ToolOperation::kRunBundledPythonModule);
  PlanPayloads(false, job.get());
  runtime::ToolJobResourcesPtr resources;
  ASSERT_EQ(BindResult::kBound,
            Bind(ModelArtifactPort(), PythonLibraryPort(), &broker_, job.get(),
                 &resources));
  EXPECT_TRUE(job->python_library->library_id.empty());
  EXPECT_EQ(job->python_library->archive_bytes, 0u);
  EXPECT_EQ(job->python_library->archive_digest,
            std::vector<uint8_t>(kDigestBytes, 0u));
  EXPECT_FALSE(resources->python_library.IsValid());
}

TEST_F(ToolJobResourcesTest, TheBuildDescribesTheLibraryAndOpensIt) {
  auto job = Job(runtime::ToolOperation::kRunBundledPythonModule);
  PlanPayloads(false, job.get());
  runtime::ToolJobResourcesPtr resources;
  ASSERT_EQ(BindResult::kBound,
            Bind(ModelArtifactPort(), LibraryPort(), &broker_, job.get(),
                 &resources));
  // The declaration and the descriptor arrive together, and the declaration is
  // the build's rather than anything a caller could have written: it is what
  // lets a worker refuse an archive that is not the one it was promised.
  ASSERT_TRUE(resources->python_library.IsValid());
  EXPECT_EQ(job->python_library->library_id, "python.stdlib");
  EXPECT_EQ(job->python_library->library_version, "3.14.0");
  EXPECT_EQ(job->python_library->archive_bytes, 12582912u);
  EXPECT_EQ(job->python_library->archive_digest,
            std::vector<uint8_t>(kDigestBytes, 9u));
}

TEST_F(ToolJobResourcesTest, ALibraryNoRegisterCanOpenRefusesTheJob) {
  auto job = Job(runtime::ToolOperation::kRunBundledPythonModule);
  PlanPayloads(false, job.get());
  runtime::ToolJobResourcesPtr resources;
  // A port that exists and cannot answer is not the same as no port at all.
  // Declared-and-unopened is the pair this refuses, because a worker handed
  // half of it cannot tell which half it is missing.
  EXPECT_EQ(BindResult::kInvalidJob,
            Bind(ModelArtifactPort(),
                 base::BindRepeating(
                     [](PythonLibrarySource*) { return false; }),
                 &broker_, job.get(), &resources));
  EXPECT_FALSE(resources);
}

TEST_F(ToolJobResourcesTest, ALibraryIsOpenedOnlyForAnInterpreter) {
  auto job = Job(runtime::ToolOperation::kProbeMedia);
  broker_.SetResolvePort(ResolvePort());
  job->media_probe->input_handle =
      broker_.Mint("job-1", ToolHandleBroker::Mode::kRead);
  PlanPayloads(false, job.get());
  runtime::ToolJobResourcesPtr resources;
  ASSERT_EQ(BindResult::kBound,
            Bind(ModelArtifactPort(), LibraryPort(), &broker_, job.get(),
                 &resources));
  // The media runtime runs no interpreter, so a library port that would answer
  // is never asked and the descriptor is never opened.
  EXPECT_FALSE(resources->python_library.IsValid());
  EXPECT_TRUE(job->python_library->library_id.empty());
}

}  // namespace
}  // namespace taffy::tool_job_resources
