// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <vector>

#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/core/rust_core_task_effect.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace bridge = core_bridge;
namespace mojom = core_service::mojom;

mojom::TaskEffectBindingPtr ToolBinding(mojom::ToolOperation operation) {
  auto binding = mojom::TaskEffectBinding::New();
  binding->operation = mojom::OperationEnvelope::New(
      "operation-1", 1u, 7u, 10'000u, "operation-key-1");
  binding->effect_id = "effect-1";
  binding->task_id = "task-1";
  binding->kind = mojom::TaskReducerEffectKind::kRunToolJob;
  binding->tool_job = mojom::TaskToolJobEffect::New();
  binding->tool_job->action_id = "action-1";
  binding->tool_job->job_id = "job-1";
  binding->tool_job->runtime = mojom::ToolRuntimeKind::kMedia;
  binding->tool_job->job = mojom::ToolJobEffect::New();
  auto& job = binding->tool_job->job;
  job->job_id = "job-1";
  job->tool_id = operation == mojom::ToolOperation::kProbeMedia
                     ? "media.probe"
                     : "media.audio.extract";
  job->runtime = mojom::ToolRuntimeKind::kMedia;
  job->tool_version = "1";
  job->operation_kind = operation;
  job->task_id = "task-1";
  job->budget = mojom::ToolResourceBudget::New(
      1'024u, 16u * 1'024u * 1'024u, 256u * 1'024u * 1'024u, 30'000u,
      0u, 1u);
  if (operation == mojom::ToolOperation::kProbeMedia) {
    job->media_probe = mojom::MediaProbeArguments::New("input-handle");
  } else {
    job->audio_extract = mojom::AudioExtractArguments::New(
        "input-handle", "output-handle", "audio.wav.pcm16.v1");
  }
  return binding;
}

mojom::TaskEffectBindingPtr PythonBinding() {
  auto binding = ToolBinding(mojom::ToolOperation::kProbeMedia);
  binding->tool_job->runtime = mojom::ToolRuntimeKind::kPython;
  auto& job = binding->tool_job->job;
  job->tool_id = "python.execute";
  job->runtime = mojom::ToolRuntimeKind::kPython;
  job->operation_kind = mojom::ToolOperation::kRunBundledPythonModule;
  job->media_probe.reset();
  job->bundled_python = mojom::BundledPythonArguments::New(
      "document.build", std::vector<uint8_t>{1u});
  return binding;
}

mojom::TaskEffectCompletionPtr ProbeCompletion(
    const mojom::TaskEffectBinding& binding) {
  auto completion = mojom::TaskEffectCompletion::New();
  completion->operation = binding.operation->Clone();
  completion->effect_id = binding.effect_id;
  completion->task_id = binding.task_id;
  completion->kind = binding.kind;
  completion->status = mojom::TaskEffectCompletionStatus::kSucceeded;
  completion->tool_output = mojom::TaskToolOutputReceipt::New(
      std::vector<uint8_t>(32u, 7u), 24u, 1u, nullptr);
  completion->effect_result = mojom::EffectResult::New();
  completion->effect_result->operation = binding.operation->Clone();
  completion->effect_result->effect_id = binding.effect_id;
  completion->effect_result->status = mojom::EffectStatus::kCompleted;
  completion->effect_result->kind = mojom::EffectKind::kToolJob;
  completion->effect_result->tool = mojom::ToolEffectResult::New();
  completion->effect_result->tool->job_id = "job-1";
  completion->effect_result->tool->status =
      mojom::ToolTerminalStatus::kCompleted;
  completion->effect_result->tool->success = mojom::ToolSuccess::New();
  completion->effect_result->tool->success->operation_kind =
      mojom::ToolOperation::kProbeMedia;
  completion->effect_result->tool->success->media_probe =
      mojom::MediaProbeResult::New(12'345u, 1u, 1u, 1'280u, 720u);
  return completion;
}

TEST(RustCoreTaskToolTerminalTest,
     ExactBoundedProbeFactsCrossBesideTheReceipt) {
  const auto binding = ToolBinding(mojom::ToolOperation::kProbeMedia);
  const auto completion = ProbeCompletion(*binding);

  const std::optional<bridge::BridgeTaskTerminal> terminal =
      core_service_internal::ToBridgeTaskTerminal(*binding, *completion);

  ASSERT_TRUE(terminal);
  EXPECT_TRUE(terminal->has_tool_output);
  EXPECT_TRUE(terminal->has_media_probe);
  EXPECT_EQ(12'345u, terminal->media_probe_duration_ms);
  EXPECT_EQ(1u, terminal->media_probe_audio_streams);
  EXPECT_EQ(1u, terminal->media_probe_video_streams);
  EXPECT_EQ(1'280u, terminal->media_probe_width_px);
  EXPECT_EQ(720u, terminal->media_probe_height_px);
}

TEST(RustCoreTaskToolTerminalTest,
     ProbeFactsMustMatchTheExactEffectJobAndOperation) {
  const auto binding = ToolBinding(mojom::ToolOperation::kProbeMedia);
  auto completion = ProbeCompletion(*binding);
  completion->effect_result->effect_id = "another-effect";
  EXPECT_FALSE(
      core_service_internal::ToBridgeTaskTerminal(*binding, *completion));

  completion = ProbeCompletion(*binding);
  completion->effect_result->tool->job_id = "another-job";
  EXPECT_FALSE(
      core_service_internal::ToBridgeTaskTerminal(*binding, *completion));

  completion = ProbeCompletion(*binding);
  completion->effect_result->tool->success->operation_kind =
      mojom::ToolOperation::kExtractAudio;
  EXPECT_FALSE(
      core_service_internal::ToBridgeTaskTerminal(*binding, *completion));
}

TEST(RustCoreTaskToolTerminalTest,
     MissingUnboundedAndNonMediaProbeResultsAreRefused) {
  const auto probe = ToolBinding(mojom::ToolOperation::kProbeMedia);
  auto completion = ProbeCompletion(*probe);
  completion->effect_result.reset();
  EXPECT_FALSE(core_service_internal::ToBridgeTaskTerminal(*probe, *completion));

  completion = ProbeCompletion(*probe);
  completion->effect_result->tool->success->media_probe->duration_ms =
      600'001u;
  EXPECT_FALSE(core_service_internal::ToBridgeTaskTerminal(*probe, *completion));

  completion = ProbeCompletion(*probe);
  completion->effect_result->tool->success->media_probe->audio_streams = 0u;
  completion->effect_result->tool->success->media_probe->video_streams = 0u;
  EXPECT_FALSE(core_service_internal::ToBridgeTaskTerminal(*probe, *completion));

  const auto transform = ToolBinding(mojom::ToolOperation::kExtractAudio);
  completion = ProbeCompletion(*transform);
  completion->tool_output->byte_count = 128u;
  completion->tool_output->artifact = mojom::TaskProducedArtifactReceipt::New(
      "artifact-action-1", mojom::TaskArtifactKind::kWaveAudio);
  EXPECT_FALSE(
      core_service_internal::ToBridgeTaskTerminal(*transform, *completion));

  const auto python = PythonBinding();
  completion = ProbeCompletion(*python);
  completion->tool_output->byte_count = 128u;
  completion->tool_output->artifact = mojom::TaskProducedArtifactReceipt::New(
      "artifact-action-1", mojom::TaskArtifactKind::kDocx);
  EXPECT_FALSE(
      core_service_internal::ToBridgeTaskTerminal(*python, *completion));
}

}  // namespace
}  // namespace taffy
