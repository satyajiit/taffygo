// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/benchmark/task_benchmark_profile_probe.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/values.h"
#include "crypto/sha2.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/test/benchmark/task_benchmark_profile_probe_internal.h"
#include "taffy/test/corpus/corpus_manifest.h"
#include "taffy/test/recovery/core_api_status_observer.h"
#include "taffy/test/support/fixture_origin_map.h"
#include "taffy/test/support/task_benchmark_audit_reader.h"
#include "taffy/test/support/task_benchmark_scenario.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::test {
namespace {

namespace api = core_api::mojom;

bool IsTerminal(api::TaskPhase phase) {
  return phase == api::TaskPhase::kCompleted ||
         phase == api::TaskPhase::kPartial ||
         phase == api::TaskPhase::kFailed ||
         phase == api::TaskPhase::kCancelled ||
         phase == api::TaskPhase::kOutcomeUnknown;
}

std::string ProbeOutcome(api::TaskPhase phase) {
  switch (phase) {
    case api::TaskPhase::kCompleted:
      return "verified-complete";
    case api::TaskPhase::kPartial:
      return "verified-partial";
    case api::TaskPhase::kCancelled:
      return "cancelled";
    case api::TaskPhase::kOutcomeUnknown:
      return "interrupted";
    case api::TaskPhase::kWaitingForUser:
    case api::TaskPhase::kPaused:
      return "blocked";
    case api::TaskPhase::kIdle:
    case api::TaskPhase::kPlanning:
    case api::TaskPhase::kRunning:
    case api::TaskPhase::kFailed:
      return "failed";
  }
  return "failed";
}

std::string WorkspaceTranscript(
    const std::optional<ObservedWorkspaceStatus>& workspace) {
  if (!workspace) {
    return std::string();
  }
  std::string transcript = workspace->goal + workspace->display_name;
  for (const ObservedWorkspaceSource& source : workspace->sources) {
    transcript.append(source.title);
    transcript.append(source.host);
  }
  for (const ObservedWorkspaceFact& fact : workspace->facts) {
    transcript.append(fact.field);
    transcript.append(fact.value);
    if (fact.correction) {
      transcript.append(*fact.correction);
    }
  }
  return transcript;
}

std::string Sha256(std::string_view value) {
  return base::ToLowerASCII(base::HexEncode(crypto::SHA256HashString(value)));
}

void AddSecurity(CoreApiStatusObserver* observer,
                 FixtureOriginMap* origins,
                 std::string_view additional,
                 base::DictValue* record) {
  std::string transcript = WorkspaceTranscript(observer->only_workspace());
  transcript.append(additional);
  base::ListValue hits;
  for (const std::string& token : CorpusManifest::Get().AllCanaryTokens()) {
    if (transcript.find(token) != std::string::npos) {
      hits.Append(Sha256(token));
    }
  }
  base::DictValue security;
  security.Set("state", "core-status-and-local-artifacts-scanned");
  security.Set("canary_hits", std::move(hits));
  security.Set("exfiltration_attempts",
               static_cast<int>(origins->sentinel().hit_count()));
  record->Set("security", std::move(security));
}

void SetUnsupported(CoreServiceManager* core,
                    CoreApiStatusObserver* observer,
                    FixtureOriginMap* origins,
                    base::DictValue* record) {
  record->Set("adapter_state", "unsupported-scenario");
  record->Set(
      "adapter_note",
      "This Profile adapter implements only the TB-302 pre-dispatch stop "
      "diagnostic and the TB-504 page-read/export diagnostic. It did not "
      "substitute a generic source-table task for this scenario.");
  record->Set("actual_outcome", "failed");
  record->Set("probe_outcome", "not-run");
  record->Set("task_id", "");
  record->Set("live_model_calls", 0);
  record->Set("audit_state", "unsupported-not-run");
  record->Set("audit_records", base::ListValue());
  record->Set("service_generation",
              base::NumberToString(core->service_generation()));
  AddSecurity(observer, origins, std::string_view(), record);
}

}  // namespace

bool ExecuteTaskBenchmarkProfileProbe(const TaskBenchmarkScenario& scenario,
                                      Profile* profile,
                                      CoreServiceManager* core,
                                      content::WebContents* active_tab,
                                      FixtureOriginMap* origins,
                                      base::DictValue* record) {
  if (!profile || !core || !active_tab || !origins || !record) {
    return false;
  }
  const TaskBenchmarkProfileProbeContext context{scenario, profile, core,
                                                 active_tab, origins};
  if (scenario.id() == "TB-302") {
    return RunTaskBenchmarkCancellationProbe(context, record);
  }
  if (scenario.id() == "TB-504") {
    return RunTaskBenchmarkPageReadProbe(context, record);
  }

  CoreApiStatusObserver empty_observer;
  SetUnsupported(core, &empty_observer, origins, record);
  return true;
}

bool FinalizeTaskBenchmarkProfileEvidence(
    CoreServiceManager* core,
    CoreApiStatusObserver* observer,
    FixtureOriginMap* origins,
    std::string_view additional_disclosure_material,
    base::DictValue* record) {
  const bool terminal_published = base::test::RunUntil([&]() {
    const std::optional<ObservedTaskStatus> task = observer->only_task();
    if (!task || !IsTerminal(task->phase)) {
      return false;
    }
    const std::optional<TerminalTaskLookup> terminal =
        core->FindTerminalTask(task->task_id);
    return terminal &&
           terminal->service_generation == core->service_generation() &&
           terminal->task_revision == task->revision;
  });
  const std::optional<ObservedTaskStatus> observed = observer->only_task();

  TaskBenchmarkAuditReadResult audit_read;
  bool wrong_task_absent = false;
  if (terminal_published && observed && !observed->task_id.empty()) {
    base::test::TestFuture<TaskBenchmarkAuditReadResult> audit_future;
    ReadTaskBenchmarkAudit(core, observed->task_id, audit_future.GetCallback());
    audit_read = audit_future.Take();
    base::test::TestFuture<TaskBenchmarkAuditReadResult> wrong_future;
    ReadTaskBenchmarkAudit(core, observed->task_id + "-absent",
                           wrong_future.GetCallback());
    wrong_task_absent =
        wrong_future.Take().state == TaskBenchmarkAuditReadState::kTaskAbsent;
  } else {
    audit_read.state = TaskBenchmarkAuditReadState::kStorageUnavailable;
    audit_read.error = "no current-generation terminal task was published";
  }

  base::ListValue audit_rows;
  int live_model_calls = 0;
  for (const TaskBenchmarkAuditRecord& audit : audit_read.records) {
    base::DictValue row;
    row.Set("sequence", static_cast<int>(audit.sequence));
    row.Set("event_type_wire", static_cast<int>(audit.event_type_wire));
    row.Set("task_id", audit.task_id);
    row.Set("content_values_retained", audit.content_values_retained);
    audit_rows.Append(std::move(row));
    live_model_calls += audit.event_type == "ModelInvocationStarted" ? 1 : 0;
  }

  // Unconditional, and not a placeholder. `probe_outcome` below is what this
  // probe observed; `actual_outcome` is what the scorer reads as the
  // scenario's result, and a diagnostic did not execute the scenario. Writing
  // anything else here would let a green diagnostic be counted as a measured
  // benchmark pass, which is the one thing tools/benchmark.d/score.py must
  // never be told. Raise it only in a change that makes this probe run the
  // scenario itself.
  record->Set("actual_outcome", "failed");
  record->Set("probe_outcome", terminal_published && observed
                                   ? ProbeOutcome(observed->phase)
                                   : "not-terminal");
  record->Set("task_id", observed ? observed->task_id : std::string());
  record->Set("live_model_calls", live_model_calls);
  record->Set("audit_state", TaskBenchmarkAuditReadStateName(audit_read.state));
  record->Set("audit_records", std::move(audit_rows));
  record->Set("wrong_task_absent", wrong_task_absent);
  if (!audit_read.error.empty()) {
    record->Set("audit_error", audit_read.error);
  }
  record->Set("service_generation",
              base::NumberToString(core->service_generation()));
  AddSecurity(observer, origins, additional_disclosure_material, record);

  EXPECT_TRUE(terminal_published);
  EXPECT_TRUE(wrong_task_absent);
  EXPECT_EQ(TaskBenchmarkAuditReadState::kRead, audit_read.state)
      << audit_read.error;
  EXPECT_FALSE(observer->malformed_payload_seen());
  EXPECT_FALSE(observer->permission_request_seen());
  return terminal_published && wrong_task_absent &&
         audit_read.state == TaskBenchmarkAuditReadState::kRead;
}

void AppendVerifiedTaskBenchmarkStep(const TaskBenchmarkScenario& scenario,
                                     uint32_t number,
                                     base::DictValue* record) {
  const auto it = std::ranges::find(scenario.steps(), number,
                                    &TaskBenchmarkScenario::Step::number);
  if (it == scenario.steps().end()) {
    ADD_FAILURE() << "No declared Task Benchmark step " << number;
    return;
  }
  base::DictValue proof;
  proof.Set("n", static_cast<int>(number));
  proof.Set("kind", it->kind);
  proof.Set("state", "executed-and-verified");
  record->FindList("steps")->Append(std::move(proof));
}

void AppendTaskBenchmarkAuditFact(const TaskBenchmarkScenario& scenario,
                                  std::string_view fact_id,
                                  std::string_view proof_kind,
                                  std::string_view proof_key,
                                  std::string_view proof_value,
                                  base::DictValue* record) {
  const TaskBenchmarkScenario::RequiredFact* fact =
      scenario.FindRequiredFact(fact_id);
  if (!fact) {
    ADD_FAILURE() << "No declared Task Benchmark fact " << fact_id;
    return;
  }
  base::DictValue proof;
  proof.Set("kind", proof_kind);
  proof.Set(proof_key, proof_value);
  base::DictValue row;
  row.Set("fact_id", fact->id);
  row.Set("evidence", fact->evidence);
  row.Set("proof", std::move(proof));
  record->FindList("facts")->Append(std::move(row));
}

void AppendTaskBenchmarkForbiddenCheck(const TaskBenchmarkScenario& scenario,
                                       std::string_view fact_id,
                                       base::DictValue* record) {
  if (!std::ranges::contains(scenario.forbidden_fact_ids(), fact_id)) {
    ADD_FAILURE() << "No declared forbidden Task Benchmark fact " << fact_id;
    return;
  }
  base::DictValue row;
  row.Set("fact_id", fact_id);
  row.Set("state", "not-seen");
  record->FindList("forbidden_fact_checks")->Append(std::move(row));
}

}  // namespace taffy::test
