// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/profile_tool_artifact_broker.h"

#include <stdint.h>

#include <string>
#include <utility>
#include <vector>

#include "taffy/browser/profile_tool_artifact_broker_test_support.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
using artifact_broker_test::ArtifactObserver;
using artifact_broker_test::CompletedAudio;
using artifact_broker_test::CompletedProbe;
using artifact_broker_test::MediaBinding;
using artifact_broker_test::ProfileToolArtifactBrokerTest;
using artifact_broker_test::Wave;

TEST_F(ProfileToolArtifactBrokerTest, UnknownAndForgedSourcesAreRejected) {
  EXPECT_FALSE(
      Prepare(MediaBinding("unknown-guid", mojom::ToolOperation::kProbeMedia)));
  EXPECT_FALSE(Prepare(
      MediaBinding("/tmp/forged-path", mojom::ToolOperation::kProbeMedia)));
}

TEST_F(ProfileToolArtifactBrokerTest,
       CustodyKindsCoverPythonAndMediaOutputsOnly) {
  EXPECT_TRUE(ProfileToolArtifactBroker::RetainsArtifactKind(
      mojom::TaskArtifactKind::kDocx));
  EXPECT_TRUE(ProfileToolArtifactBroker::RetainsArtifactKind(
      mojom::TaskArtifactKind::kXlsx));
  EXPECT_TRUE(ProfileToolArtifactBroker::RetainsArtifactKind(
      mojom::TaskArtifactKind::kWaveAudio));
  EXPECT_TRUE(ProfileToolArtifactBroker::RetainsArtifactKind(
      mojom::TaskArtifactKind::kFrameArchive));
  EXPECT_FALSE(ProfileToolArtifactBroker::RetainsArtifactKind(
      mojom::TaskArtifactKind::kMarkdown));
  EXPECT_FALSE(ProfileToolArtifactBroker::RetainsArtifactKind(
      mojom::TaskArtifactKind::kCsv));
  EXPECT_FALSE(ProfileToolArtifactBroker::RetainsArtifactKind(
      mojom::TaskArtifactKind::kPdf));
  EXPECT_FALSE(ProfileToolArtifactBroker::RetainsArtifactKind(
      mojom::TaskArtifactKind::kPptx));
}

TEST_F(ProfileToolArtifactBrokerTest, WrongKindAndStaleSourcesAreRejected) {
  ASSERT_TRUE(Admit("audio-guid", MediaTopLevelType::kAudio));
  EXPECT_FALSE(
      Prepare(MediaBinding("audio-guid", mojom::ToolOperation::kSampleFrames)));
  broker_->SetActiveGeneration(2u);
  EXPECT_FALSE(Admit("stale-guid", MediaTopLevelType::kVideo, 1u));
  ASSERT_TRUE(Admit("current-guid", MediaTopLevelType::kVideo, 2u));
  broker_->SetActiveGeneration(3u);
  EXPECT_FALSE(Prepare(
      MediaBinding("current-guid", mojom::ToolOperation::kProbeMedia, 3u)));
}

TEST_F(ProfileToolArtifactBrokerTest,
       SourceAdmissionIsBoundedPerTaskAndPerProfile) {
  for (size_t source = 0u; source < 8u; ++source) {
    EXPECT_TRUE(Admit("task-1-source-" + std::to_string(source),
                      MediaTopLevelType::kVideo));
  }
  EXPECT_FALSE(
      Admit("task-1-source-8", MediaTopLevelType::kVideo, 1u, "task-1"));
  for (size_t task = 2u; task <= 4u; ++task) {
    for (size_t source = 0u; source < 8u; ++source) {
      EXPECT_TRUE(
          Admit("source-" + std::to_string(task) + "-" + std::to_string(source),
                MediaTopLevelType::kVideo, 1u, "task-" + std::to_string(task)));
    }
  }
  EXPECT_EQ(32u, broker_->source_count_for_testing());
  EXPECT_FALSE(
      Admit("profile-overflow", MediaTopLevelType::kVideo, 1u, "task-5"));

  broker_->SettleTask("task-1", 2u,
                      ProfileToolArtifactBroker::TaskDisposition::kAbandoned);
  EXPECT_EQ(32u, broker_->source_count_for_testing());
  broker_->SettleTask("task-1", 1u,
                      ProfileToolArtifactBroker::TaskDisposition::kAbandoned);
  EXPECT_EQ(24u, broker_->source_count_for_testing());
  EXPECT_TRUE(Admit("replacement", MediaTopLevelType::kVideo, 1u, "task-5"));
}

TEST_F(ProfileToolArtifactBrokerTest,
       PreparedJobsAreBoundedAndTaskCancellationReleasesTheirHandles) {
  for (size_t job = 0u; job < 2u; ++job) {
    const std::string identity = std::to_string(job);
    const std::string source = "task-1-source-" + identity;
    ASSERT_TRUE(Admit(source, MediaTopLevelType::kVideo));
    EXPECT_TRUE(Prepare(MediaBinding(source, mojom::ToolOperation::kProbeMedia,
                                     1u, "task-1", identity)));
  }
  ASSERT_TRUE(Admit("task-1-source-overflow", MediaTopLevelType::kVideo));
  EXPECT_FALSE(Prepare(MediaBinding("task-1-source-overflow",
                                    mojom::ToolOperation::kProbeMedia, 1u,
                                    "task-1", "overflow")));
  EXPECT_EQ(2u, broker_->pending_job_count_for_testing());
  EXPECT_EQ(2u, handles_->size_for_testing());
  EXPECT_EQ(2u, supervisor_.handle_broker().minted_count_for_testing());

  broker_->SettleTask("task-1", 2u,
                      ProfileToolArtifactBroker::TaskDisposition::kAbandoned);
  EXPECT_EQ(2u, broker_->pending_job_count_for_testing());
  broker_->SettleTask("task-1", 1u,
                      ProfileToolArtifactBroker::TaskDisposition::kAbandoned);
  EXPECT_EQ(0u, broker_->pending_job_count_for_testing());
  EXPECT_EQ(0u, handles_->size_for_testing());
  EXPECT_EQ(0u, supervisor_.handle_broker().minted_count_for_testing());
  EXPECT_EQ(0u, broker_->source_count_for_testing());
}

TEST_F(ProfileToolArtifactBrokerTest,
       PendingJobAdmissionHasAnExactProfileCeiling) {
  for (size_t job = 0u; job < 8u; ++job) {
    const std::string identity = std::to_string(job);
    const std::string task = "task-" + identity;
    const std::string source = "source-" + identity;
    ASSERT_TRUE(Admit(source, MediaTopLevelType::kVideo, 1u, task));
    EXPECT_TRUE(Prepare(MediaBinding(source, mojom::ToolOperation::kProbeMedia,
                                     1u, task, identity)));
  }
  ASSERT_TRUE(
      Admit("source-overflow", MediaTopLevelType::kVideo, 1u, "task-overflow"));
  EXPECT_FALSE(
      Prepare(MediaBinding("source-overflow", mojom::ToolOperation::kProbeMedia,
                           1u, "task-overflow", "overflow")));
  EXPECT_EQ(8u, broker_->pending_job_count_for_testing());

  broker_->SetActiveGeneration(2u);
  EXPECT_EQ(0u, broker_->pending_job_count_for_testing());
  EXPECT_EQ(0u, handles_->size_for_testing());
  EXPECT_EQ(0u, supervisor_.handle_broker().minted_count_for_testing());
}

TEST_F(ProfileToolArtifactBrokerTest,
       ProbeReceiptExistsOnlyForBoundedConsistentScalarFacts) {
  ASSERT_TRUE(Admit("video-guid", MediaTopLevelType::kVideo));
  const mojom::TaskEffectBindingPtr prepared =
      Prepare(MediaBinding("video-guid", mojom::ToolOperation::kProbeMedia));
  ASSERT_TRUE(prepared);

  mojom::TaskToolOutputReceiptPtr receipt = RetainProbe(
      *prepared, CompletedProbe(*prepared, 12'345u, 1u, 1u, 1'280u, 720u));
  ASSERT_TRUE(receipt);
  EXPECT_EQ(24u, receipt->byte_count);
  EXPECT_EQ(32u, receipt->digest.size());
  EXPECT_FALSE(receipt->artifact);

  EXPECT_FALSE(
      RetainProbe(*prepared, CompletedProbe(*prepared, 1u, 0u, 0u, 0u, 0u)));
  EXPECT_FALSE(RetainProbe(
      *prepared, CompletedProbe(*prepared, 600'001u, 1u, 0u, 0u, 0u)));
  EXPECT_FALSE(
      RetainProbe(*prepared, CompletedProbe(*prepared, 1u, 1u, 0u, 1u, 1u)));
}

TEST_F(ProfileToolArtifactBrokerTest,
       AToolTerminalMustMatchItsReservedOperationJobAndAction) {
  for (int field = 0; field < 5; ++field) {
    SCOPED_TRACE(field);
    const std::string identity = "operation-" + std::to_string(field);
    const std::string source = identity + "-source";
    ASSERT_TRUE(Admit(source, MediaTopLevelType::kVideo));
    auto operation = Prepare(MediaBinding(
        source, mojom::ToolOperation::kProbeMedia, 1u, "task-1", identity));
    ASSERT_TRUE(operation);
    switch (field) {
      case 0:
        operation->operation->operation_id = "another-operation";
        break;
      case 1:
        operation->operation->service_generation = 2u;
        break;
      case 2:
        ++operation->operation->task_revision;
        break;
      case 3:
        ++operation->operation->deadline_monotonic_ms;
        break;
      case 4:
        operation->operation->idempotency_key = "another-idempotency";
        break;
    }
    EXPECT_FALSE(RetainProbe(
        *operation, CompletedProbe(*operation, 1u, 1u, 0u, 0u, 0u)));
  }

  ASSERT_TRUE(Admit("job-source", MediaTopLevelType::kVideo));
  auto job = Prepare(MediaBinding("job-source", mojom::ToolOperation::kProbeMedia,
                                  1u, "task-1", "job"));
  ASSERT_TRUE(job);
  auto wrong_job = CompletedProbe(*job, 1u, 1u, 0u, 0u, 0u);
  wrong_job->tool->job_id = "another-job";
  EXPECT_FALSE(RetainProbe(*job, std::move(wrong_job)));

  ASSERT_TRUE(Admit("action-source", MediaTopLevelType::kVideo));
  auto action = Prepare(MediaBinding(
      "action-source", mojom::ToolOperation::kProbeMedia, 1u, "task-1",
      "action"));
  ASSERT_TRUE(action);
  auto wrong_action = action.Clone();
  wrong_action->tool_job->action_id = "another-action";
  EXPECT_FALSE(RetainProbe(
      *wrong_action,
      CompletedProbe(*wrong_action, 1u, 1u, 0u, 0u, 0u)));
  EXPECT_EQ(0u, broker_->pending_job_count_for_testing());
}

TEST_F(ProfileToolArtifactBrokerTest,
       VerifiedTransformBecomesExactSaveShareArtifact) {
  mojom::TaskToolOutputReceiptPtr receipt = TransformAndRetain();
  ASSERT_TRUE(receipt);
  ASSERT_TRUE(receipt->artifact);
  EXPECT_EQ("artifact-action-1", receipt->artifact->artifact_id);
  EXPECT_EQ(mojom::TaskArtifactKind::kWaveAudio, receipt->artifact->kind);
  EXPECT_EQ(32u, receipt->digest.size());
  EXPECT_EQ(1u, broker_->artifact_count_for_testing());

  auto artifact = mojom::TaskArtifactEffect::New(
      mojom::TaskArtifactKind::kWaveAudio, "artifact-action-1", 1u,
      std::vector<uint8_t>());
  ArtifactObserver observer;
  base::ObserverList<CoreServiceObserver> observers;
  observers.AddObserver(&observer);
  EXPECT_TRUE(broker_->DeliverArtifact(observers, "task-1", 1u, *artifact));
  EXPECT_EQ(Wave(), observer.content_);
  EXPECT_EQ(0u, broker_->artifact_count_for_testing());
  EXPECT_EQ(0u, broker_->artifact_bytes_for_testing());
  EXPECT_FALSE(broker_->DeliverArtifact(observers, "task-1", 1u, *artifact));
}

TEST_F(ProfileToolArtifactBrokerTest,
       RefusedDeliveryRetainsExactBytesForAnotherTrustedSurface) {
  ASSERT_TRUE(TransformAndRetain());
  auto artifact = mojom::TaskArtifactEffect::New(
      mojom::TaskArtifactKind::kWaveAudio, "artifact-action-1", 1u,
      std::vector<uint8_t>());
  ArtifactObserver refusing(/*accepts=*/false);
  base::ObserverList<CoreServiceObserver> refused;
  refused.AddObserver(&refusing);
  EXPECT_FALSE(broker_->DeliverArtifact(refused, "task-1", 1u, *artifact));
  EXPECT_EQ(1u, broker_->artifact_count_for_testing());

  ArtifactObserver accepting;
  base::ObserverList<CoreServiceObserver> accepted;
  accepted.AddObserver(&accepting);
  EXPECT_TRUE(broker_->DeliverArtifact(accepted, "task-1", 1u, *artifact));
  EXPECT_EQ(0u, broker_->artifact_count_for_testing());
  EXPECT_EQ(Wave(), accepting.content_);
}

TEST_F(ProfileToolArtifactBrokerTest,
       PauseFinishAndAbandonHaveDistinctCustodySemantics) {
  ASSERT_TRUE(TransformAndRetain());
  ASSERT_TRUE(Admit("reusable-source", MediaTopLevelType::kVideo));
  ASSERT_TRUE(Admit("pending-source", MediaTopLevelType::kVideo));
  ASSERT_TRUE(Prepare(MediaBinding("pending-source",
                                  mojom::ToolOperation::kProbeMedia, 1u,
                                  "task-1", "pending")));

  broker_->SettleTask("task-1", 1u,
                      ProfileToolArtifactBroker::TaskDisposition::kPaused);
  EXPECT_EQ(3u, broker_->source_count_for_testing());
  EXPECT_EQ(0u, broker_->pending_job_count_for_testing());
  EXPECT_EQ(1u, broker_->artifact_count_for_testing());
  ASSERT_TRUE(Prepare(MediaBinding("download-guid",
                                  mojom::ToolOperation::kProbeMedia, 1u,
                                  "task-1", "reused-after-pause")));
  EXPECT_EQ(1u, broker_->pending_job_count_for_testing());

  broker_->SettleTask("task-1", 1u,
                      ProfileToolArtifactBroker::TaskDisposition::kFinished);
  EXPECT_EQ(0u, broker_->source_count_for_testing());
  EXPECT_EQ(0u, broker_->pending_job_count_for_testing());
  EXPECT_EQ(1u, broker_->artifact_count_for_testing());

  broker_->SettleTask("task-1", 1u,
                      ProfileToolArtifactBroker::TaskDisposition::kAbandoned);
  EXPECT_EQ(0u, broker_->artifact_count_for_testing());
}

TEST_F(ProfileToolArtifactBrokerTest,
       RetainedArtifactBytesAreBoundedPerTaskAndReleasedAtTerminal) {
  constexpr size_t kTaskBytes = 16u * 1024u * 1024u;
  const std::vector<uint8_t> full = Wave(kTaskBytes - 44u);
  ASSERT_EQ(kTaskBytes, full.size());
  ASSERT_TRUE(TransformAndRetain("large-source", "large", full));
  EXPECT_EQ(kTaskBytes, broker_->artifact_bytes_for_testing());

  EXPECT_FALSE(TransformAndRetain("small-source", "small", Wave()));
  EXPECT_EQ(1u, broker_->artifact_count_for_testing());
  EXPECT_EQ(kTaskBytes, broker_->artifact_bytes_for_testing());

  broker_->SettleTask("task-1", 1u,
                      ProfileToolArtifactBroker::TaskDisposition::kAbandoned);
  EXPECT_EQ(0u, broker_->artifact_count_for_testing());
  EXPECT_EQ(0u, broker_->artifact_bytes_for_testing());
}

TEST_F(ProfileToolArtifactBrokerTest,
       RetainedArtifactBytesHaveAnExactProfileCeiling) {
  constexpr size_t kTaskBytes = 16u * 1024u * 1024u;
  const std::vector<uint8_t> full = Wave(kTaskBytes - 44u);
  for (size_t task = 0u; task < 4u; ++task) {
    const std::string identity = std::to_string(task);
    EXPECT_TRUE(TransformAndRetain("source-" + identity, identity, full,
                                  "task-" + identity));
  }
  EXPECT_EQ(64u * 1024u * 1024u, broker_->artifact_bytes_for_testing());
  EXPECT_FALSE(TransformAndRetain("overflow-source", "overflow", Wave(),
                                  "task-overflow"));
  EXPECT_EQ(64u * 1024u * 1024u, broker_->artifact_bytes_for_testing());
}

TEST_F(ProfileToolArtifactBrokerTest, PrivateProfileNeverExportsResidentBytes) {
  ResetBroker(true);
  ASSERT_TRUE(TransformAndRetain());
  auto artifact = mojom::TaskArtifactEffect::New(
      mojom::TaskArtifactKind::kWaveAudio, "artifact-action-1", 1u,
      std::vector<uint8_t>());
  ArtifactObserver observer;
  base::ObserverList<CoreServiceObserver> observers;
  observers.AddObserver(&observer);
  EXPECT_FALSE(broker_->DeliverArtifact(observers, "task-1", 1u, *artifact));
  EXPECT_TRUE(observer.content_.empty());
  EXPECT_EQ(1u, broker_->artifact_count_for_testing());
  broker_->SettleTask("task-1", 1u,
                      ProfileToolArtifactBroker::TaskDisposition::kFinished);
  EXPECT_EQ(0u, broker_->artifact_count_for_testing());
  EXPECT_EQ(0u, broker_->artifact_bytes_for_testing());
}

TEST(ProfileToolArtifactBrokerReservationTest,
     AbandonedPreparationRemainsChargedUntilItsCallbackReturns) {
  base::test::TaskEnvironment task_environment{
      base::test::TaskEnvironment::ThreadPoolExecutionMode::QUEUED};
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  ProfileToolSupervisor supervisor(
      1u, ProfileToolSupervisor::PythonPorts::Unsupported(),
      ProfileToolSupervisor::LocalModelPorts::Unsupported(),
      ProfileToolSupervisor::MediaPorts::Unsupported());
  auto handles = base::MakeRefCounted<ProfileToolHandleStore>();
  supervisor.SetResourcePorts(handles->GetResolvePort(),
                              tool_job_resources::ModelArtifactPort());
  ProfileToolArtifactBroker broker(&supervisor, handles, temp.GetPath(), false);
  ASSERT_TRUE(broker.AdmitOpenedSource(
      {ProfileToolArtifactBroker::SourceKind::kExplicitPicker, "task-1",
       "async-source", "browser-session-1", 1u, 8u,
       MediaTopLevelType::kVideo},
      artifact_broker_test::Source(temp, 8u)));

  base::test::TestFuture<mojom::TaskEffectBindingPtr> prepared;
  broker.PrepareTaskToolJob(
      MediaBinding("async-source", mojom::ToolOperation::kExtractAudio, 1u,
                   "task-1", "async"),
      prepared.GetCallback());
  EXPECT_FALSE(prepared.IsReady());
  EXPECT_EQ(1u, broker.pending_job_count_for_testing());

  broker.SettleTask("task-1", 1u,
                    ProfileToolArtifactBroker::TaskDisposition::kPaused);
  EXPECT_EQ(1u, broker.pending_job_count_for_testing());
  task_environment.RunUntilIdle();
  EXPECT_TRUE(prepared.IsReady());
  EXPECT_FALSE(prepared.Take());
  EXPECT_EQ(0u, broker.pending_job_count_for_testing());
  EXPECT_EQ(0u, handles->size_for_testing());
  EXPECT_EQ(0u, supervisor.handle_broker().minted_count_for_testing());
}

TEST(ProfileToolArtifactBrokerReservationTest,
     CancelledValidationRevokesHandlesButStaysChargedUntilReply) {
  base::test::TaskEnvironment task_environment{
      base::test::TaskEnvironment::ThreadPoolExecutionMode::QUEUED};
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  ProfileToolSupervisor supervisor(
      1u, ProfileToolSupervisor::PythonPorts::Unsupported(),
      ProfileToolSupervisor::LocalModelPorts::Unsupported(),
      ProfileToolSupervisor::MediaPorts::Unsupported());
  auto handles = base::MakeRefCounted<ProfileToolHandleStore>();
  supervisor.SetResourcePorts(handles->GetResolvePort(),
                              tool_job_resources::ModelArtifactPort());
  ProfileToolArtifactBroker broker(&supervisor, handles, temp.GetPath(), false);
  ASSERT_TRUE(broker.AdmitOpenedSource(
      {ProfileToolArtifactBroker::SourceKind::kExplicitPicker, "task-1",
       "validation-source", "browser-session-1", 1u, 8u,
       MediaTopLevelType::kVideo},
      artifact_broker_test::Source(temp, 8u)));

  base::test::TestFuture<mojom::TaskEffectBindingPtr> prepared;
  broker.PrepareTaskToolJob(
      MediaBinding("validation-source", mojom::ToolOperation::kExtractAudio),
      prepared.GetCallback());
  task_environment.RunUntilIdle();
  mojom::TaskEffectBindingPtr binding = prepared.Take();
  ASSERT_TRUE(binding);
  const std::string output_handle =
      binding->tool_job->job->audio_extract->output_handle;
  const std::vector<uint8_t> wave = Wave();
  base::File output = supervisor.handle_broker().Resolve(
      binding->tool_job->job_id, output_handle, ToolHandleBroker::Mode::kWrite);
  ASSERT_TRUE(output.WriteAndCheck(0, wave));
  output.Close();

  base::test::TestFuture<mojom::TaskToolOutputReceiptPtr> retained;
  broker.RetainTaskToolOutput(
      *binding, CompletedAudio(*binding, output_handle, wave),
      retained.GetCallback());
  ASSERT_FALSE(retained.IsReady());
  // The worker's half of the exchange already spent the output handle
  // above, and this store answers an identifier exactly once, so the
  // source handle is the one still outstanding here.
  EXPECT_EQ(1u, handles->size_for_testing());
  broker.AbandonTaskToolJob(binding->tool_job->job_id);
  EXPECT_EQ(1u, broker.pending_job_count_for_testing());
  EXPECT_EQ(0u, handles->size_for_testing());
  EXPECT_EQ(0u, supervisor.handle_broker().minted_count_for_testing());

  task_environment.RunUntilIdle();
  EXPECT_FALSE(retained.Take());
  EXPECT_EQ(0u, broker.pending_job_count_for_testing());
}

}  // namespace
}  // namespace taffy
