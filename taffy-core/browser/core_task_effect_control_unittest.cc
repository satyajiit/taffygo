// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdint.h>

#include <string>
#include <vector>

#include "taffy/browser/core_task_effect.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = core_service::mojom;

constexpr uint64_t kGeneration = 5u;
constexpr uint64_t kRevision = 9u;
constexpr uint64_t kNow = 10'000u;

mojom::TaskEffectBindingPtr EmptyBinding(mojom::TaskReducerEffectKind kind) {
  auto binding = mojom::TaskEffectBinding::New();
  binding->operation = mojom::OperationEnvelope::New(
      "task-effect-operation", kGeneration, kRevision, 13'000u,
      "task-effect-idempotency");
  binding->effect_id = "task-effect-1";
  binding->task_id = "task-1";
  binding->ordinal = 0u;
  binding->kind = kind;
  return binding;
}

mojom::ToolJobEffectPtr PythonJob() {
  auto job = mojom::ToolJobEffect::New();
  job->job_id = "job-task-1-action-1";
  job->task_id = "task-1";
  job->runtime = mojom::ToolRuntimeKind::kPython;
  job->tool_id = "python.execute";
  job->tool_version = "1";
  job->operation_kind = mojom::ToolOperation::kRunBundledPythonModule;
  job->budget = mojom::ToolResourceBudget::New(256u, 1024u, 1024u, 100u, 0u,
                                                1u);
  job->bundled_python =
      mojom::BundledPythonArguments::New("document.build", std::vector<uint8_t>{'{', '}'});
  return job;
}

TEST(CoreTaskEffectControlTest, BrowserOwnedEffectsRequireExactBodies) {
  auto approval = EmptyBinding(mojom::TaskReducerEffectKind::kRequestApproval);
  approval->approval =
      mojom::TaskApprovalEffect::New("action-1", std::string(64u, 'b'),
                                     nullptr);
  EXPECT_TRUE(IsStructurallyValidTaskEffectBinding(*approval, kGeneration,
                                                   kRevision, kNow));

  auto permission =
      EmptyBinding(mojom::TaskReducerEffectKind::kRequestPermission);
  permission->permission = mojom::TaskPermissionEffect::New(
      "permission-1", mojom::PlatformPermission::kCamera, 12'500u,
      1'800'000'002'500u, "browser-session-1");
  EXPECT_TRUE(IsStructurallyValidTaskEffectBinding(*permission, kGeneration,
                                                   kRevision, kNow));
  permission->permission->deadline_monotonic_ms = 13'001u;
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(*permission, kGeneration,
                                                    kRevision, kNow));

  auto missing_utc = permission.Clone();
  missing_utc->permission->deadline_monotonic_ms = 12'500u;
  missing_utc->permission->deadline_utc_ms = 0u;
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(*missing_utc, kGeneration,
                                                    kRevision, kNow));

  auto missing_session = permission.Clone();
  missing_session->permission->deadline_monotonic_ms = 12'500u;
  missing_session->permission->browser_session_id.clear();
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(
      *missing_session, kGeneration, kRevision, kNow));

  auto settlement =
      EmptyBinding(mojom::TaskReducerEffectKind::kAwaitInFlightWork);
  settlement->settlement =
      mojom::TaskSettlementEffect::New(mojom::TaskSettlementKind::kCancel);
  EXPECT_TRUE(IsStructurallyValidTaskEffectBinding(*settlement, kGeneration,
                                                   kRevision, kNow));

  auto release = EmptyBinding(mojom::TaskReducerEffectKind::kReleaseTaskTabs);
  release->release_tabs = mojom::TaskReleaseTabsEffect::New(kRevision);
  EXPECT_TRUE(IsStructurallyValidTaskEffectBinding(*release, kGeneration,
                                                   kRevision, kNow));
  release->release_tabs->terminal_revision = kRevision - 1u;
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(*release, kGeneration,
                                                    kRevision, kNow));
}

TEST(CoreTaskEffectControlTest, MemoryToolsRequireExactClosedBodies) {
  auto search = EmptyBinding(mojom::TaskReducerEffectKind::kRunMemoryTool);
  search->memory_tool = mojom::TaskMemoryToolEffect::New(
      "action-1", mojom::TaskActionOperationKind::kMemorySearch);
  EXPECT_TRUE(IsStructurallyValidTaskEffectBinding(*search, kGeneration,
                                                   kRevision, kNow));

  auto delete_memory = search.Clone();
  delete_memory->memory_tool->operation_kind =
      mojom::TaskActionOperationKind::kMemoryDelete;
  EXPECT_TRUE(IsStructurallyValidTaskEffectBinding(*delete_memory, kGeneration,
                                                   kRevision, kNow));

  auto wrong_operation = search.Clone();
  wrong_operation->memory_tool->operation_kind =
      mojom::TaskActionOperationKind::kLibrarySearch;
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(
      *wrong_operation, kGeneration, kRevision, kNow));

  auto two_bodies = search.Clone();
  two_bodies->library_tool = mojom::TaskLibraryToolEffect::New(
      "action-1", mojom::TaskActionOperationKind::kLibrarySearch);
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(*two_bodies, kGeneration,
                                                    kRevision, kNow));
}

TEST(CoreTaskEffectControlTest,
     HandoverCarriesAnIdentityAndAWindowAndNothingElse) {
  auto handover = EmptyBinding(mojom::TaskReducerEffectKind::kAwaitHandover);
  handover->handover =
      mojom::TaskHandoverEffect::New("turn-1-handover-0", 300'000u);
  EXPECT_TRUE(IsStructurallyValidTaskEffectBinding(*handover, kGeneration,
                                                   kRevision, kNow));

  auto no_window = handover.Clone();
  no_window->handover->window_ms = 0u;
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(*no_window, kGeneration,
                                                    kRevision, kNow));

  auto no_identity = handover.Clone();
  no_identity->handover->handover_id.clear();
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(*no_identity, kGeneration,
                                                    kRevision, kNow));

  auto wrong_body = EmptyBinding(mojom::TaskReducerEffectKind::kAwaitHandover);
  wrong_body->settlement =
      mojom::TaskSettlementEffect::New(mojom::TaskSettlementKind::kPause);
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(*wrong_body, kGeneration,
                                                    kRevision, kNow));

  auto two_bodies = handover.Clone();
  two_bodies->settlement =
      mojom::TaskSettlementEffect::New(mojom::TaskSettlementKind::kPause);
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(*two_bodies, kGeneration,
                                                    kRevision, kNow));
}

TEST(CoreTaskEffectControlTest,
     ArtifactCarriesExactIdentityFormatAndWorkspaceRevision) {
  auto generated =
      EmptyBinding(mojom::TaskReducerEffectKind::kGenerateArtifact);
  generated->generate_artifact = mojom::TaskArtifactEffect::New(
      mojom::TaskArtifactKind::kMarkdown, "artifact-turn-1-call-0", 7u,
      std::vector<uint8_t>());
  EXPECT_TRUE(IsStructurallyValidTaskEffectBinding(*generated, kGeneration,
                                                   kRevision, kNow));

  auto exported = EmptyBinding(mojom::TaskReducerEffectKind::kExportArtifact);
  exported->export_artifact = generated->generate_artifact.Clone();
  exported->export_artifact->content = {'#', ' ', 'R', 'e', 'p', 'o', 'r', 't'};
  EXPECT_TRUE(IsStructurallyValidTaskEffectBinding(*exported, kGeneration,
                                                   kRevision, kNow));

  auto invalid_text = exported.Clone();
  invalid_text->export_artifact->content = {0xffu};
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(
      *invalid_text, kGeneration, kRevision, kNow));

  auto no_identity = generated.Clone();
  no_identity->generate_artifact->artifact_id.clear();
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(*no_identity, kGeneration,
                                                    kRevision, kNow));

  auto no_revision = generated.Clone();
  no_revision->generate_artifact->workspace_revision = 0u;
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(*no_revision, kGeneration,
                                                    kRevision, kNow));

  auto rich_export = EmptyBinding(mojom::TaskReducerEffectKind::kExportArtifact);
  rich_export->export_artifact = mojom::TaskArtifactEffect::New(
      mojom::TaskArtifactKind::kXlsx, "artifact-workbook", 7u,
      std::vector<uint8_t>{'P', 'K', 0x03u, 0x04u});
  EXPECT_TRUE(IsStructurallyValidTaskEffectBinding(
      *rich_export, kGeneration, kRevision, kNow));

  auto wrong_magic = rich_export.Clone();
  wrong_magic->export_artifact->content = {'N', 'O', 'P', 'E'};
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(
      *wrong_magic, kGeneration, kRevision, kNow));

  auto bytes_on_generate = generated.Clone();
  bytes_on_generate->generate_artifact->kind =
      mojom::TaskArtifactKind::kPdf;
  bytes_on_generate->generate_artifact->content = {'%', 'P', 'D', 'F', '-'};
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(
      *bytes_on_generate, kGeneration, kRevision, kNow));
}

TEST(CoreTaskEffectControlTest, ToolJobCarriesTwoIdentitiesAndARuntime) {
  auto tool_job = EmptyBinding(mojom::TaskReducerEffectKind::kRunToolJob);
  tool_job->tool_job = mojom::TaskToolJobEffect::New(
      "action-1", "job-task-1-action-1", mojom::ToolRuntimeKind::kPython,
      PythonJob());
  EXPECT_TRUE(IsStructurallyValidTaskEffectBinding(*tool_job, kGeneration,
                                                   kRevision, kNow));

  auto no_action = tool_job.Clone();
  no_action->tool_job->action_id.clear();
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(*no_action, kGeneration,
                                                    kRevision, kNow));

  auto no_job = tool_job.Clone();
  no_job->tool_job->job_id.clear();
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(*no_job, kGeneration,
                                                    kRevision, kNow));

  auto no_body = EmptyBinding(mojom::TaskReducerEffectKind::kRunToolJob);
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(*no_body, kGeneration,
                                                    kRevision, kNow));

  auto wrong_body = EmptyBinding(mojom::TaskReducerEffectKind::kRunToolJob);
  wrong_body->settlement =
      mojom::TaskSettlementEffect::New(mojom::TaskSettlementKind::kPause);
  EXPECT_FALSE(IsStructurallyValidTaskEffectBinding(*wrong_body, kGeneration,
                                                    kRevision, kNow));
}

}  // namespace
}  // namespace taffy
