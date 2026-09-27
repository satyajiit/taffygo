// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/profile_local_model_tool_launcher.h"

#include <string>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/files/file.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/bind.h"
#include "taffy/contracts/tool-runtime/generated/mojom/tool_runtime.mojom.h"
#include "taffy/contracts/tool-runtime/tool_runtime_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = tool_runtime::mojom;

constexpr size_t kDigestBytes = 32u;

mojom::ToolJobPtr LocalJob(mojom::ToolModelArtifactKind format) {
  auto job = mojom::ToolJob::New();
  job->operation = mojom::OperationEnvelope::New("operation", 1u, 1u, 1'000u,
                                                 "idempotency");
  job->job_id = "job";
  job->runtime = mojom::ToolRuntimeKind::kLocalModel;
  job->tool_id = "model.generate";
  job->tool_version = "1";
  job->operation_kind = mojom::ToolOperation::kGenerateLocalModel;
  job->budget = mojom::ResourceBudget::New(16u, 16u, 1024u, 100u, 0u, 1u);
  job->local_model = mojom::LocalModelArguments::New(
      "conversation", std::vector<uint8_t>{'h', 'i'}, 4u,
      mojom::ToolModelArtifact::New(
          "model.small", "2026-09-01", format, 1u,
          std::vector<uint8_t>(kDigestBytes, 1u), "", 0u,
          std::vector<uint8_t>(kDigestBytes, 0u)));
  job->input_payload = mojom::ToolPayloadInput::New(
      mojom::ToolInputTransport::kInline, 2u,
      std::vector<uint8_t>(kDigestBytes, 2u));
  job->output_payload =
      mojom::ToolPayloadOutput::New(mojom::ToolOutputTransport::kInlineChunks,
                                    16u);
  job->python_library = mojom::ToolPythonLibrary::New(
      "", "", 0u, std::vector<uint8_t>(kDigestBytes, 0u));
  return job;
}

mojo::PendingRemote<mojom::ToolRuntimeClient> DisconnectedClient() {
  mojo::PendingRemote<mojom::ToolRuntimeClient> client;
  auto receiver = client.InitWithNewPipeAndPassReceiver();
  receiver.reset();
  return client;
}

class ProfileLocalModelToolLauncherTest : public testing::Test {
 public:
  void SetUp() override { ASSERT_TRUE(scratch_.CreateUniqueTempDir()); }

  mojom::ToolJobResourcesPtr Resources(bool with_model) {
    auto resources = mojom::ToolJobResources::New();
    if (with_model) {
      resources->model_artifact = base::File(
          scratch_.GetPath().AppendASCII("model"),
          base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_READ |
              base::File::FLAG_WRITE);
      CHECK(resources->model_artifact.IsValid());
    }
    return resources;
  }

  ProfileToolSupervisor::AdmissionCallback Admission() {
    return base::BindLambdaForTesting([&](mojom::ToolAdmissionPtr admission) {
      ASSERT_TRUE(admission);
      status_ = admission->status;
    });
  }

 protected:
  base::ScopedTempDir scratch_;
  mojom::ToolAdmissionStatus status_ = mojom::ToolAdmissionStatus::kAccepted;
};

TEST_F(ProfileLocalModelToolLauncherTest,
       MissingOpenedArtifactIsDistinctAndStartsNothing) {
  int starts = 0;
  auto launcher = base::MakeRefCounted<ProfileLocalModelToolLauncher>(
      std::vector<mojom::ToolModelArtifactKind>{
          mojom::ToolModelArtifactKind::kGguf},
      base::BindLambdaForTesting(
          [&](mojom::ToolJobPtr, mojom::ToolJobResourcesPtr,
              mojo::PendingRemote<mojom::ToolRuntimeClient>,
              ProfileToolSupervisor::AdmissionCallback) { ++starts; }),
      base::BindRepeating([](const std::string&) {}));
  launcher->GetStartPort().Run(
      LocalJob(mojom::ToolModelArtifactKind::kGguf), Resources(false),
      DisconnectedClient(), Admission());

  EXPECT_EQ(mojom::ToolAdmissionStatus::kModelArtifactMissing, status_);
  EXPECT_EQ(0, starts);
}

TEST_F(ProfileLocalModelToolLauncherTest,
       BuildWithoutRuntimeAdapterIsDistinctAndStartsNothing) {
  auto launcher = base::MakeRefCounted<ProfileLocalModelToolLauncher>();
  launcher->GetStartPort().Run(
      LocalJob(mojom::ToolModelArtifactKind::kGguf), Resources(true),
      DisconnectedClient(), Admission());

  EXPECT_EQ(mojom::ToolAdmissionStatus::kLocalRuntimeUnavailable, status_);
}

TEST_F(ProfileLocalModelToolLauncherTest,
       IncompatibleFormatNeverReachesTheRuntimeAdapter) {
  int starts = 0;
  auto launcher = base::MakeRefCounted<ProfileLocalModelToolLauncher>(
      std::vector<mojom::ToolModelArtifactKind>{
          mojom::ToolModelArtifactKind::kLitertTflite},
      base::BindLambdaForTesting(
          [&](mojom::ToolJobPtr, mojom::ToolJobResourcesPtr,
              mojo::PendingRemote<mojom::ToolRuntimeClient>,
              ProfileToolSupervisor::AdmissionCallback) { ++starts; }),
      base::BindRepeating([](const std::string&) {}));
  launcher->GetStartPort().Run(
      LocalJob(mojom::ToolModelArtifactKind::kGguf), Resources(true),
      DisconnectedClient(), Admission());

  EXPECT_EQ(mojom::ToolAdmissionStatus::kModelArtifactIncompatible, status_);
  EXPECT_EQ(starts, 0);
}

TEST_F(ProfileLocalModelToolLauncherTest,
       CompatibleArtifactReachesOnlyTheInjectedSandboxAdapter) {
  int starts = 0;
  int cancels = 0;
  auto launcher = base::MakeRefCounted<ProfileLocalModelToolLauncher>(
      std::vector<mojom::ToolModelArtifactKind>{
          mojom::ToolModelArtifactKind::kGguf},
      base::BindLambdaForTesting(
          [&](mojom::ToolJobPtr job, mojom::ToolJobResourcesPtr,
              mojo::PendingRemote<mojom::ToolRuntimeClient>,
              ProfileToolSupervisor::AdmissionCallback callback) {
            ++starts;
            std::move(callback).Run(mojom::ToolAdmission::New(
                job->job_id, mojom::ToolAdmissionStatus::kAccepted));
          }),
      base::BindLambdaForTesting(
          [&](const std::string& job_id) { cancels += job_id == "job"; }));
  launcher->GetStartPort().Run(
      LocalJob(mojom::ToolModelArtifactKind::kGguf), Resources(true),
      DisconnectedClient(), Admission());
  launcher->GetCancelPort().Run("job");

  EXPECT_EQ(mojom::ToolAdmissionStatus::kAccepted, status_);
  EXPECT_EQ(1, starts);
  EXPECT_EQ(1, cancels);
}

}  // namespace
}  // namespace taffy
