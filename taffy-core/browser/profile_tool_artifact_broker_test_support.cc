// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/profile_tool_artifact_broker_test_support.h"

#include <utility>

#include "base/test/test_future.h"

namespace taffy::artifact_broker_test {

ArtifactObserver::ArtifactObserver(bool accepts) : accepts_(accepts) {}

ArtifactObserver::~ArtifactObserver() = default;

bool ArtifactObserver::OnTaskArtifactExport(
    const std::string& task_id,
    const std::string& artifact_id,
    mojom::TaskArtifactKind kind,
    const std::vector<uint8_t>& content) {
  task_id_ = task_id;
  artifact_id_ = artifact_id;
  kind_ = kind;
  content_ = content;
  return accepts_;
}

ProfileToolArtifactBrokerTest::ProfileToolArtifactBrokerTest()
    : supervisor_(1u,
                  ProfileToolSupervisor::PythonPorts::Unsupported(),
                  ProfileToolSupervisor::LocalModelPorts::Unsupported(),
                  ProfileToolSupervisor::MediaPorts::Unsupported()) {}

ProfileToolArtifactBrokerTest::~ProfileToolArtifactBrokerTest() = default;

void ProfileToolArtifactBrokerTest::SetUp() {
  ASSERT_TRUE(temp_.CreateUniqueTempDir());
  supervisor_.SetResourcePorts(handles_->GetResolvePort(),
                               tool_job_resources::ModelArtifactPort());
  ResetBroker(false);
}

void ProfileToolArtifactBrokerTest::ResetBroker(bool private_profile) {
  broker_ = std::make_unique<ProfileToolArtifactBroker>(
      &supervisor_, handles_, temp_.GetPath(), private_profile);
}

bool ProfileToolArtifactBrokerTest::Admit(const std::string& source_id,
                                          MediaTopLevelType type,
                                          uint64_t generation,
                                          const std::string& task_id) {
  return broker_->AdmitOpenedSource(
      {ProfileToolArtifactBroker::SourceKind::kExplicitPicker, task_id,
       source_id, "browser-session-1", generation, 8u, type},
      Source(temp_, 8u));
}

mojom::TaskEffectBindingPtr ProfileToolArtifactBrokerTest::Prepare(
    mojom::TaskEffectBindingPtr binding) {
  base::test::TestFuture<mojom::TaskEffectBindingPtr> future;
  broker_->PrepareTaskToolJob(std::move(binding), future.GetCallback());
  return future.Take();
}

mojom::TaskToolOutputReceiptPtr ProfileToolArtifactBrokerTest::RetainProbe(
    const mojom::TaskEffectBinding& binding,
    mojom::EffectResultPtr result) {
  base::test::TestFuture<mojom::TaskToolOutputReceiptPtr> future;
  broker_->RetainTaskToolOutput(binding, std::move(result),
                                future.GetCallback());
  return future.Take();
}

mojom::TaskToolOutputReceiptPtr
ProfileToolArtifactBrokerTest::TransformAndRetain(
    const std::string& source_id,
    const std::string& identity,
    const std::vector<uint8_t>& wave,
    const std::string& task_id) {
  EXPECT_TRUE(Admit(source_id, MediaTopLevelType::kVideo, 1u, task_id));
  mojom::TaskEffectBindingPtr prepared = Prepare(MediaBinding(
      source_id, mojom::ToolOperation::kExtractAudio, 1u, task_id, identity));
  EXPECT_TRUE(prepared);
  if (!prepared) {
    return nullptr;
  }
  const std::string input_handle =
      prepared->tool_job->job->audio_extract->input_handle;
  const std::string output_handle =
      prepared->tool_job->job->audio_extract->output_handle;
  EXPECT_TRUE(supervisor_.handle_broker()
                  .Resolve(prepared->tool_job->job_id, input_handle,
                           ToolHandleBroker::Mode::kRead)
                  .IsValid());
  base::File output = supervisor_.handle_broker().Resolve(
      prepared->tool_job->job_id, output_handle,
      ToolHandleBroker::Mode::kWrite);
  EXPECT_TRUE(output.WriteAndCheck(0, wave));
  output.Close();

  base::test::TestFuture<mojom::TaskToolOutputReceiptPtr> future;
  broker_->RetainTaskToolOutput(*prepared,
                                CompletedAudio(*prepared, output_handle, wave),
                                future.GetCallback());
  return future.Take();
}

}  // namespace taffy::artifact_broker_test
