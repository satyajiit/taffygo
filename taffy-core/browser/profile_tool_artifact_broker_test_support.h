// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_PROFILE_TOOL_ARTIFACT_BROKER_TEST_SUPPORT_H_
#define TAFFY_BROWSER_PROFILE_TOOL_ARTIFACT_BROKER_TEST_SUPPORT_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/ref_counted.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "crypto/hash.h"
#include "taffy/browser/profile_tool_artifact_broker.h"
#include "taffy/browser/profile_tool_handle_store.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::artifact_broker_test {

namespace mojom = core_service::mojom;

inline void AppendU16(std::vector<uint8_t>* out, uint16_t value) {
  out->push_back(static_cast<uint8_t>(value));
  out->push_back(static_cast<uint8_t>(value >> 8u));
}

inline void AppendU32(std::vector<uint8_t>* out, uint32_t value) {
  AppendU16(out, static_cast<uint16_t>(value));
  AppendU16(out, static_cast<uint16_t>(value >> 16u));
}

inline std::vector<uint8_t> Wave(size_t data_bytes = 2u) {
  std::vector<uint8_t> out;
  const auto tag = [&](std::string_view value) {
    out.insert(out.end(), value.begin(), value.end());
  };
  tag("RIFF");
  AppendU32(&out, static_cast<uint32_t>(36u + data_bytes));
  tag("WAVE");
  tag("fmt ");
  AppendU32(&out, 16u);
  AppendU16(&out, 1u);
  AppendU16(&out, 1u);
  AppendU32(&out, 8000u);
  AppendU32(&out, 16000u);
  AppendU16(&out, 2u);
  AppendU16(&out, 16u);
  tag("data");
  AppendU32(&out, static_cast<uint32_t>(data_bytes));
  out.insert(out.end(), data_bytes, 0u);
  return out;
}

inline base::File Source(base::ScopedTempDir& temp, size_t bytes) {
  base::FilePath path;
  if (!base::CreateTemporaryFileInDir(temp.GetPath(), &path) ||
      !base::WriteFile(path, std::vector<uint8_t>(bytes, 7u))) {
    return base::File();
  }
  return base::File(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
}

inline mojom::TaskEffectBindingPtr MediaBinding(
    const std::string& source_id,
    mojom::ToolOperation operation,
    uint64_t generation = 1u,
    const std::string& task_id = "task-1",
    const std::string& identity = "1") {
  auto binding = mojom::TaskEffectBinding::New();
  binding->operation = mojom::OperationEnvelope::New(
      "op-" + identity, generation, 1u, 60'000u, "idem-" + identity);
  binding->effect_id = "effect-" + identity;
  binding->task_id = task_id;
  binding->kind = mojom::TaskReducerEffectKind::kRunToolJob;
  binding->tool_job = mojom::TaskToolJobEffect::New();
  binding->tool_job->action_id = "action-" + identity;
  binding->tool_job->job_id = "job-" + identity;
  binding->tool_job->runtime = mojom::ToolRuntimeKind::kMedia;
  auto job = mojom::ToolJobEffect::New();
  job->job_id = "job-" + identity;
  job->runtime = mojom::ToolRuntimeKind::kMedia;
  job->tool_version = "1";
  job->operation_kind = operation;
  job->task_id = task_id;
  job->budget = mojom::ToolResourceBudget::New(
      8u, 16u * 1024u * 1024u, 256u * 1024u * 1024u, 30'000u, 0u, 1u);
  if (operation == mojom::ToolOperation::kExtractAudio) {
    job->tool_id = "media.audio.extract";
    job->audio_extract = mojom::AudioExtractArguments::New(
        source_id, "browser-custody-output", "audio.wav.pcm16.v1");
  } else if (operation == mojom::ToolOperation::kSampleFrames) {
    job->tool_id = "media.frames.sample";
    job->frame_sample = mojom::FrameSampleArguments::New(
        source_id, "browser-custody-output", "frames.png.zip.v1", 2u);
  } else {
    job->tool_id = "media.probe";
    job->media_probe = mojom::MediaProbeArguments::New(source_id);
  }
  binding->tool_job->job = std::move(job);
  return binding;
}

inline mojom::EffectResultPtr CompletedProbe(
    const mojom::TaskEffectBinding& binding,
    uint64_t duration_ms,
    uint32_t audio_streams,
    uint32_t video_streams,
    uint32_t width,
    uint32_t height) {
  auto result = mojom::EffectResult::New();
  result->operation = binding.operation.Clone();
  result->effect_id = binding.effect_id;
  result->status = mojom::EffectStatus::kCompleted;
  result->kind = mojom::EffectKind::kToolJob;
  result->tool = mojom::ToolEffectResult::New();
  result->tool->job_id = binding.tool_job->job_id;
  result->tool->status = mojom::ToolTerminalStatus::kCompleted;
  result->tool->success = mojom::ToolSuccess::New();
  result->tool->success->operation_kind = mojom::ToolOperation::kProbeMedia;
  result->tool->success->media_probe = mojom::MediaProbeResult::New(
      duration_ms, audio_streams, video_streams, width, height);
  return result;
}

inline mojom::EffectResultPtr CompletedAudio(
    const mojom::TaskEffectBinding& binding,
    const std::string& output_handle,
    const std::vector<uint8_t>& content) {
  auto result = mojom::EffectResult::New();
  result->operation = binding.operation.Clone();
  result->effect_id = binding.effect_id;
  result->status = mojom::EffectStatus::kCompleted;
  result->kind = mojom::EffectKind::kToolJob;
  result->tool = mojom::ToolEffectResult::New();
  result->tool->job_id = binding.tool_job->job_id;
  result->tool->status = mojom::ToolTerminalStatus::kCompleted;
  result->tool->success = mojom::ToolSuccess::New();
  result->tool->success->operation_kind = mojom::ToolOperation::kExtractAudio;
  const auto digest = crypto::hash::Sha256(content);
  result->tool->success->audio_extract = mojom::AudioExtractResult::New(
      output_handle, content.size(),
      std::vector<uint8_t>(digest.begin(), digest.end()));
  return result;
}

class ArtifactObserver final : public CoreServiceObserver {
 public:
  explicit ArtifactObserver(bool accepts = true);
  ~ArtifactObserver() override;

  bool OnTaskArtifactExport(const std::string& task_id,
                            const std::string& artifact_id,
                            mojom::TaskArtifactKind kind,
                            const std::vector<uint8_t>& content) override;

  std::string task_id_;
  std::string artifact_id_;
  mojom::TaskArtifactKind kind_ = mojom::TaskArtifactKind::kMarkdown;
  std::vector<uint8_t> content_;

 private:
  const bool accepts_;
};

class ProfileToolArtifactBrokerTest : public testing::Test {
 protected:
  ProfileToolArtifactBrokerTest();
  ~ProfileToolArtifactBrokerTest() override;

  void SetUp() override;

  void ResetBroker(bool private_profile);

  bool Admit(const std::string& source_id,
             MediaTopLevelType type,
             uint64_t generation = 1u,
             const std::string& task_id = "task-1");

  mojom::TaskEffectBindingPtr Prepare(mojom::TaskEffectBindingPtr binding);

  mojom::TaskToolOutputReceiptPtr RetainProbe(
      const mojom::TaskEffectBinding& binding,
      mojom::EffectResultPtr result);

  mojom::TaskToolOutputReceiptPtr TransformAndRetain(
      const std::string& source_id = "download-guid",
      const std::string& identity = "1",
      const std::vector<uint8_t>& wave = Wave(),
      const std::string& task_id = "task-1");

  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir temp_;
  scoped_refptr<ProfileToolHandleStore> handles_ =
      base::MakeRefCounted<ProfileToolHandleStore>();
  ProfileToolSupervisor supervisor_;
  std::unique_ptr<ProfileToolArtifactBroker> broker_;
};

}  // namespace taffy::artifact_broker_test

#endif  // TAFFY_BROWSER_PROFILE_TOOL_ARTIFACT_BROKER_TEST_SUPPORT_H_
