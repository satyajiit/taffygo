// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdint.h>

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/files/scoped_temp_dir.h"
#include "base/memory/ref_counted.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "taffy/browser/profile_tool_artifact_broker.h"
#include "taffy/browser/profile_tool_artifact_broker_test_support.h"
#include "taffy/browser/profile_tool_handle_store.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;
using artifact_broker_test::ArtifactObserver;

void AppendU16(std::vector<uint8_t>* out, uint16_t value) {
  out->push_back(static_cast<uint8_t>(value));
  out->push_back(static_cast<uint8_t>(value >> 8u));
}

void AppendU32(std::vector<uint8_t>* out, uint32_t value) {
  AppendU16(out, static_cast<uint16_t>(value));
  AppendU16(out, static_cast<uint16_t>(value >> 16u));
}

uint32_t Crc32(uint8_t byte) {
  uint32_t crc = 0xffffffffu ^ byte;
  for (int bit = 0; bit < 8; ++bit) {
    crc = (crc >> 1u) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

std::vector<uint8_t> Document() {
  struct Entry {
    std::string_view name;
    uint8_t content;
    uint32_t offset;
  };
  constexpr std::array<std::pair<std::string_view, uint8_t>, 3> kParts = {{
      {"[Content_Types].xml", 't'},
      {"_rels/.rels", 'r'},
      {"word/document.xml", 'd'},
  }};
  std::vector<uint8_t> out;
  std::vector<Entry> entries;
  for (const auto& [name, content] : kParts) {
    entries.push_back({name, content, static_cast<uint32_t>(out.size())});
    AppendU32(&out, 0x04034b50u);
    for (uint16_t field : {20u, 0u, 0u, 0u, 0u}) {
      AppendU16(&out, field);
    }
    AppendU32(&out, Crc32(content));
    AppendU32(&out, 1u);
    AppendU32(&out, 1u);
    AppendU16(&out, static_cast<uint16_t>(name.size()));
    AppendU16(&out, 0u);
    out.insert(out.end(), name.begin(), name.end());
    out.push_back(content);
  }
  const uint32_t directory_offset = static_cast<uint32_t>(out.size());
  for (const Entry& entry : entries) {
    AppendU32(&out, 0x02014b50u);
    for (uint16_t field : {20u, 20u, 0u, 0u, 0u, 0u}) {
      AppendU16(&out, field);
    }
    AppendU32(&out, Crc32(entry.content));
    AppendU32(&out, 1u);
    AppendU32(&out, 1u);
    AppendU16(&out, static_cast<uint16_t>(entry.name.size()));
    // Extra, comment, disk and internal attributes are the four halfwords
    // between the name length and the fullword external attributes; a fifth
    // here shifts the local offset two bytes past where a reader looks for it.
    for (int field = 0; field < 4; ++field) {
      AppendU16(&out, 0u);
    }
    AppendU32(&out, 0u);
    AppendU32(&out, entry.offset);
    out.insert(out.end(), entry.name.begin(), entry.name.end());
  }
  const uint32_t directory_bytes =
      static_cast<uint32_t>(out.size()) - directory_offset;
  AppendU32(&out, 0x06054b50u);
  AppendU16(&out, 0u);
  AppendU16(&out, 0u);
  AppendU16(&out, 3u);
  AppendU16(&out, 3u);
  AppendU32(&out, directory_bytes);
  AppendU32(&out, directory_offset);
  AppendU16(&out, 0u);
  return out;
}

mojom::TaskEffectBindingPtr PythonBinding(const std::string& task_id,
                                          const std::string& identity,
                                          uint64_t generation = 1u) {
  auto binding = mojom::TaskEffectBinding::New();
  binding->operation = mojom::OperationEnvelope::New(
      "op-" + identity, generation, 1u, 60'000u, "idem-" + identity);
  binding->effect_id = "effect-" + identity;
  binding->task_id = task_id;
  binding->kind = mojom::TaskReducerEffectKind::kRunToolJob;
  auto job = mojom::ToolJobEffect::New();
  job->job_id = "job-" + identity;
  job->runtime = mojom::ToolRuntimeKind::kPython;
  job->tool_id = "python.execute";
  job->tool_version = "1";
  job->operation_kind = mojom::ToolOperation::kRunBundledPythonModule;
  job->budget = mojom::ToolResourceBudget::New(262144u, 1u << 20, 64u << 20,
                                               5000u, 0u, 1u);
  job->bundled_python = mojom::BundledPythonArguments::New(
      "document.build", std::vector<uint8_t>{'{', '}'});
  job->task_id = task_id;
  binding->tool_job = mojom::TaskToolJobEffect::New(
      "action-" + identity, "job-" + identity, mojom::ToolRuntimeKind::kPython,
      std::move(job));
  return binding;
}

mojom::EffectResultPtr PythonResult(const mojom::TaskEffectBinding& binding) {
  auto result = mojom::EffectResult::New();
  result->operation = binding.operation.Clone();
  result->effect_id = binding.effect_id;
  result->kind = mojom::EffectKind::kToolJob;
  result->status = mojom::EffectStatus::kCompleted;
  result->tool = mojom::ToolEffectResult::New();
  result->tool->job_id = binding.tool_job->job_id;
  result->tool->status = mojom::ToolTerminalStatus::kCompleted;
  result->tool->success = mojom::ToolSuccess::New();
  result->tool->success->operation_kind =
      mojom::ToolOperation::kRunBundledPythonModule;
  result->tool->success->bundled_python =
      mojom::BundledPythonResult::New(Document());
  return result;
}

mojom::TaskToolOutputReceiptPtr Retain(
    ProfileToolArtifactBroker& broker,
    const mojom::TaskEffectBinding& binding) {
  mojom::EffectResultPtr result = PythonResult(binding);
  base::test::TestFuture<mojom::TaskToolOutputReceiptPtr> future;
  broker.RetainTaskToolOutput(binding, std::move(result),
                              future.GetCallback());
  return future.Take();
}

TEST(ProfileToolArtifactBrokerPythonTest,
     ValidationRejectsAChangedGenerationBeforeReply) {
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  ProfileToolSupervisor supervisor(
      1u, ProfileToolSupervisor::PythonPorts::Unsupported(),
      ProfileToolSupervisor::LocalModelPorts::Unsupported(),
      ProfileToolSupervisor::MediaPorts::Unsupported());
  auto handles = base::MakeRefCounted<ProfileToolHandleStore>();
  ProfileToolArtifactBroker broker(&supervisor, handles, temp.GetPath(), false);

  mojom::TaskEffectBindingPtr binding = PythonBinding("task-1", "python");
  mojom::EffectResultPtr result = PythonResult(*binding);

  base::test::TestFuture<mojom::TaskToolOutputReceiptPtr> future;
  broker.RetainTaskToolOutput(*binding, std::move(result),
                              future.GetCallback());
  // Validation replies asynchronously. A core restart between dispatch and
  // reply must make otherwise valid bytes ineligible for current custody.
  broker.SetActiveGeneration(2u);
  EXPECT_FALSE(future.Take());
  EXPECT_EQ(0u, broker.artifact_count_for_testing());
}

TEST(ProfileToolArtifactBrokerPythonTest,
     RetainedArtifactsAreBoundedPerTaskAndPerProfile) {
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  ProfileToolSupervisor supervisor(
      1u, ProfileToolSupervisor::PythonPorts::Unsupported(),
      ProfileToolSupervisor::LocalModelPorts::Unsupported(),
      ProfileToolSupervisor::MediaPorts::Unsupported());
  auto handles = base::MakeRefCounted<ProfileToolHandleStore>();
  ProfileToolArtifactBroker broker(&supervisor, handles, temp.GetPath(), false);

  for (size_t artifact = 0u; artifact < 4u; ++artifact) {
    const mojom::TaskEffectBindingPtr binding =
        PythonBinding("task-1", "task-1-" + std::to_string(artifact));
    EXPECT_TRUE(Retain(broker, *binding));
  }
  EXPECT_FALSE(Retain(broker, *PythonBinding("task-1", "task-1-overflow")));
  EXPECT_EQ(4u, broker.artifact_count_for_testing());

  for (size_t artifact = 4u; artifact < 16u; ++artifact) {
    const std::string identity = "profile-" + std::to_string(artifact);
    EXPECT_TRUE(Retain(broker, *PythonBinding(identity, identity)));
  }
  EXPECT_EQ(16u, broker.artifact_count_for_testing());
  EXPECT_FALSE(
      Retain(broker, *PythonBinding("profile-overflow", "profile-overflow")));

  broker.SettleTask("task-1", 2u,
                    ProfileToolArtifactBroker::TaskDisposition::kAbandoned);
  EXPECT_EQ(16u, broker.artifact_count_for_testing());
  broker.SettleTask("task-1", 1u,
                    ProfileToolArtifactBroker::TaskDisposition::kAbandoned);
  EXPECT_EQ(12u, broker.artifact_count_for_testing());
  EXPECT_TRUE(Retain(broker, *PythonBinding("replacement", "replacement")));
}

TEST(ProfileToolArtifactBrokerPythonTest,
     DocumentCustodyRequiresTheExactTaskIdentityKindAndEmptyEffect) {
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  ProfileToolSupervisor supervisor(
      1u, ProfileToolSupervisor::PythonPorts::Unsupported(),
      ProfileToolSupervisor::LocalModelPorts::Unsupported(),
      ProfileToolSupervisor::MediaPorts::Unsupported());
  auto handles = base::MakeRefCounted<ProfileToolHandleStore>();
  ProfileToolArtifactBroker broker(&supervisor, handles, temp.GetPath(), false);

  const mojom::TaskEffectBindingPtr binding =
      PythonBinding("task-1", "document");
  mojom::TaskToolOutputReceiptPtr receipt = Retain(broker, *binding);
  ASSERT_TRUE(receipt);
  ASSERT_TRUE(receipt->artifact);
  auto artifact = mojom::TaskArtifactEffect::New(
      mojom::TaskArtifactKind::kDocx, receipt->artifact->artifact_id, 1u,
      std::vector<uint8_t>());
  ArtifactObserver observer;
  base::ObserverList<CoreServiceObserver> observers;
  observers.AddObserver(&observer);

  EXPECT_FALSE(broker.DeliverArtifact(observers, "task-1", 2u, *artifact));
  EXPECT_FALSE(broker.DeliverArtifact(observers, "another-task", 1u,
                                      *artifact));
  artifact->workspace_revision = 2u;
  EXPECT_FALSE(broker.DeliverArtifact(observers, "task-1", 1u, *artifact));
  artifact->workspace_revision = 1u;
  artifact->artifact_id = "another-artifact";
  EXPECT_FALSE(broker.DeliverArtifact(observers, "task-1", 1u, *artifact));
  artifact->artifact_id = receipt->artifact->artifact_id;
  artifact->kind = mojom::TaskArtifactKind::kXlsx;
  EXPECT_FALSE(broker.DeliverArtifact(observers, "task-1", 1u, *artifact));
  artifact->kind = mojom::TaskArtifactKind::kDocx;
  artifact->content = Document();
  EXPECT_FALSE(broker.DeliverArtifact(observers, "task-1", 1u, *artifact));
  EXPECT_EQ(1u, broker.artifact_count_for_testing());

  artifact->content.clear();
  EXPECT_TRUE(broker.DeliverArtifact(observers, "task-1", 1u, *artifact));
  EXPECT_EQ(0u, broker.artifact_count_for_testing());
  EXPECT_EQ(Document(), observer.content_);
}

TEST(ProfileToolArtifactBrokerPythonTest,
     CancelAndRestartCannotBypassInFlightValidationQuota) {
  base::test::TaskEnvironment task_environment{
      base::test::TaskEnvironment::ThreadPoolExecutionMode::QUEUED};
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  ProfileToolSupervisor supervisor(
      1u, ProfileToolSupervisor::PythonPorts::Unsupported(),
      ProfileToolSupervisor::LocalModelPorts::Unsupported(),
      ProfileToolSupervisor::MediaPorts::Unsupported());
  auto handles = base::MakeRefCounted<ProfileToolHandleStore>();
  ProfileToolArtifactBroker broker(&supervisor, handles, temp.GetPath(), false);

  std::vector<
      std::unique_ptr<base::test::TestFuture<mojom::TaskToolOutputReceiptPtr>>>
      futures;
  const auto queue = [&](const std::string& task_id,
                         const std::string& identity) {
    auto future = std::make_unique<
        base::test::TestFuture<mojom::TaskToolOutputReceiptPtr>>();
    mojom::TaskEffectBindingPtr binding = PythonBinding(task_id, identity);
    mojom::EffectResultPtr result = PythonResult(*binding);
    broker.RetainTaskToolOutput(*binding, std::move(result),
                                future->GetCallback());
    futures.push_back(std::move(future));
  };

  queue("task-1", "task-1-a");
  queue("task-1", "task-1-b");
  queue("task-1", "task-1-overflow");
  ASSERT_EQ(3u, futures.size());
  EXPECT_TRUE(futures.back()->IsReady());
  EXPECT_FALSE(futures.back()->Take());
  futures.pop_back();
  EXPECT_EQ(2u, broker.pending_job_count_for_testing());
  broker.SettleTask("task-1", 1u,
                    ProfileToolArtifactBroker::TaskDisposition::kAbandoned);
  EXPECT_EQ(2u, broker.pending_job_count_for_testing());

  for (size_t job = 0u; job < 6u; ++job) {
    queue("task-" + std::to_string(job + 2u), "profile-" + std::to_string(job));
  }
  EXPECT_EQ(8u, broker.pending_job_count_for_testing());
  queue("task-profile-overflow", "profile-overflow");
  EXPECT_TRUE(futures.back()->IsReady());
  EXPECT_FALSE(futures.back()->Take());
  futures.pop_back();

  broker.SetActiveGeneration(2u);
  EXPECT_EQ(8u, broker.pending_job_count_for_testing());
  mojom::TaskEffectBindingPtr current =
      PythonBinding("current-task", "current", 2u);
  mojom::EffectResultPtr current_result = PythonResult(*current);
  base::test::TestFuture<mojom::TaskToolOutputReceiptPtr> refused_current;
  broker.RetainTaskToolOutput(*current, std::move(current_result),
                              refused_current.GetCallback());
  EXPECT_FALSE(refused_current.Take());

  task_environment.RunUntilIdle();
  EXPECT_EQ(0u, broker.pending_job_count_for_testing());
  for (const auto& future : futures) {
    EXPECT_TRUE(future->IsReady());
    EXPECT_FALSE(future->Take());
  }
  EXPECT_EQ(0u, broker.artifact_count_for_testing());
  mojom::EffectResultPtr retried_result = PythonResult(*current);
  base::test::TestFuture<mojom::TaskToolOutputReceiptPtr> retried;
  broker.RetainTaskToolOutput(*current, std::move(retried_result),
                              retried.GetCallback());
  task_environment.RunUntilIdle();
  EXPECT_TRUE(retried.Take());
}

}  // namespace
}  // namespace taffy
