// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "crypto/sha2.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/test/corpus/corpus_manifest.h"
#include "taffy/test/support/bip_graph_payload_reader.h"
#include "taffy/test/support/taffy_observation_test_base.h"
#include "taffy/test/support/task_benchmark_page_evidence.h"
#include "taffy/test/support/task_benchmark_record.h"
#include "taffy/test/support/task_benchmark_scenario.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/origin.h"

// The page-facing half of the Task Benchmark adapter. Every emitted proof is
// derived from a live navigation or BIP observation of the selected immutable
// scenario. Steps this binary cannot execute remain absent, so score.py keeps
// the run red instead of treating a partial adapter as a complete benchmark.

namespace taffy::test {
namespace {

std::string Sha256(std::string_view value) {
  return base::ToLowerASCII(base::HexEncode(crypto::SHA256HashString(value)));
}

class TaskBenchmarkObservationBrowserTest : public TaffyObservationTestBase {
 protected:
  content::RenderFrameHost* FrameForFixture(std::string_view fixture_id) {
    const GURL expected = FixtureUrl(fixture_id);
    content::RenderFrameHost* match = nullptr;
    bool ambiguous = false;
    web_contents()->GetPrimaryMainFrame()->ForEachRenderFrameHost(
        [&](content::RenderFrameHost* frame) {
          if (frame->GetLastCommittedURL() != expected) {
            return;
          }
          ambiguous = match != nullptr;
          match = frame;
        });
    return ambiguous ? nullptr : match;
  }

  bool NavigateAndVerify(std::string_view fixture_id) {
    const GURL expected = FixtureUrl(fixture_id);
    return content::NavigateToURL(web_contents(), expected) &&
           web_contents()->GetLastCommittedURL() == expected;
  }

  std::optional<ObservationEnvelope> ObserveFixtureFrame(
      std::string_view fixture_id) {
    content::RenderFrameHost* const frame = FrameForFixture(fixture_id);
    if (!frame) {
      return std::nullopt;
    }
    const url::Origin committed_origin = frame->GetLastCommittedOrigin();
    if (committed_origin.opaque()) {
      return std::nullopt;
    }

    BeginNewTask();
    Origin allowed;
    allowed.kind = OriginKind::kTuple;
    allowed.serialization = committed_origin.Serialize();
    GrantObservation({std::move(allowed)},
                     /*may_include_child_frames=*/false);
    return client().Observe(builder().Observation(
        broker()->GetOrAssignFrameId(frame), ObservationScope::kDocument));
  }

  void AppendStep(base::DictValue* record,
                  const TaskBenchmarkScenario::Step& step,
                  std::string_view state) {
    base::DictValue proof;
    proof.Set("n", static_cast<int>(step.number));
    proof.Set("kind", step.kind);
    proof.Set("state", state);
    record->FindList("steps")->Append(std::move(proof));
  }

  void AppendPageFacts(base::DictValue* record,
                       const TaskBenchmarkScenario& scenario,
                       const TaskBenchmarkScenario::Step& step,
                       const GraphPayload& graph,
                       bool graph_complete,
                       std::set<std::string>* emitted_fact_ids) {
    for (const std::string& fact_id : step.produces) {
      const TaskBenchmarkScenario::RequiredFact* const fact =
          scenario.FindRequiredFact(fact_id);
      if (!fact || fact->evidence != "page-field" || !fact->source_fixture ||
          !fact->source_field || *fact->source_fixture != *step.fixture ||
          emitted_fact_ids->contains(fact_id)) {
        continue;
      }

      const std::optional<TaskBenchmarkPageFieldEvidence> extracted =
          ExtractTaskBenchmarkPageFieldEvidence(*fact, *step.fixture,
                                                graph_complete, graph);
      if (!extracted) {
        continue;
      }
      base::DictValue evidence;
      evidence.Set("state", "expected-value-observed");
      evidence.Set("expected_value_sha256", extracted->expected_value_sha256);
      evidence.Set("fact_id", fact_id);
      evidence.Set("evidence", fact->evidence);
      evidence.Set("fixture", *fact->source_fixture);
      evidence.Set("field", *fact->source_field);
      evidence.Set("tolerance", fact->tolerance);
      record->FindList("facts")->Append(std::move(evidence));
      emitted_fact_ids->insert(fact_id);
    }
  }

  void AppendObservationMetrics(base::DictValue* record,
                                const TaskBenchmarkScenario::Step& step,
                                const ObservationEnvelope& envelope,
                                bool graph_decoded) {
    base::DictValue metrics;
    metrics.Set("step", static_cast<int>(step.number));
    metrics.Set("fixture", *step.fixture);
    metrics.Set("result_code", static_cast<int>(envelope.code));
    metrics.Set("encoding", static_cast<int>(envelope.encoding));
    metrics.Set("node_count", static_cast<int>(envelope.node_count));
    metrics.Set("total_bytes", static_cast<int>(envelope.total_bytes));
    metrics.Set("graph_payload_bytes",
                static_cast<int>(envelope.graph_payload.size()));
    metrics.Set("frame_count", static_cast<int>(envelope.frames.size()));
    metrics.Set("graph_revision",
                base::NumberToString(envelope.graph_revision));
    metrics.Set("truncated", envelope.truncation.truncated);
    metrics.Set("omitted_node_count",
                static_cast<int>(envelope.truncation.omitted_node_count));
    metrics.Set("omitted_text_bytes",
                static_cast<int>(envelope.truncation.omitted_text_bytes));
    metrics.Set("omitted_frame_count",
                static_cast<int>(envelope.truncation.omitted_frame_count));
    metrics.Set("may_change_answer", envelope.truncation.may_change_answer);
    metrics.Set("redacted_field_count",
                static_cast<int>(envelope.redaction.redacted_field_count));
    metrics.Set(
        "suppressed_secret_value_count",
        static_cast<int>(envelope.redaction.suppressed_secret_value_count));
    metrics.Set("sensitive_zone_count",
                static_cast<int>(envelope.redaction.sensitive_zone_count));
    metrics.Set(
        "policy_filtered_frame_count",
        static_cast<int>(envelope.redaction.policy_filtered_frame_count));
    metrics.Set("warning_count",
                static_cast<int>(envelope.warning_codes.size()));
    metrics.Set("graph_decoded", graph_decoded);
    record->FindList("observations")->Append(std::move(metrics));
  }

  bool StepProducesPageFact(const TaskBenchmarkScenario& scenario,
                            const TaskBenchmarkScenario::Step& step) {
    return std::ranges::any_of(step.produces, [&](const std::string& fact_id) {
      const TaskBenchmarkScenario::RequiredFact* const fact =
          scenario.FindRequiredFact(fact_id);
      return fact && fact->evidence == "page-field";
    });
  }

  void AddSecurityResult(base::DictValue* record) {
    const std::string transcript = client().TranscriptForLeakScan();
    base::ListValue canary_hits;
    for (const std::string& token : CorpusManifest::Get().AllCanaryTokens()) {
      if (transcript.find(token) != std::string::npos) {
        // Never copy a seeded secret into the evidence artifact. Its digest
        // identifies the failed canary without making the record another leak.
        canary_hits.Append(Sha256(token));
      }
    }
    base::DictValue security;
    security.Set("state", "live-observation-transcript-scanned");
    security.Set("canary_hits", std::move(canary_hits));
    security.Set("exfiltration_attempts",
                 static_cast<int>(origins().sentinel().hit_count()));
    record->Set("security", std::move(security));
  }

  void ExecuteScenario(std::string_view scenario_id) {
    std::string error;
    std::optional<TaskBenchmarkScenario> scenario =
        TaskBenchmarkScenario::Load(scenario_id, &error);
    ASSERT_TRUE(scenario) << error;

    base::DictValue record = NewTaskBenchmarkRecord(*scenario, "observation");
    record.Set("adapter_state", "page-steps-executed");
    record.Set(
        "adapter_note",
        "This component drove declared NAVIGATE, OBSERVE, and page-field "
        "EXTRACT steps through the fixture server and live BIP endpoint. "
        "Non-page task steps and forbidden-result adjudication remain for "
        "the task component.");
    record.Set("observations", base::ListValue());

    bool path_valid = true;
    if (scenario->steps().front().kind != "step:NAVIGATE") {
      path_valid = NavigateAndVerify(scenario->starting_fixture());
    }

    std::set<std::string> emitted_fact_ids;
    size_t observation_steps_attempted = 0u;
    size_t observations_returned = 0u;
    size_t observations_decoded = 0u;
    for (const TaskBenchmarkScenario::Step& step : scenario->steps()) {
      const bool event_before_step = std::ranges::any_of(
          scenario->injected_events(), [&step](const auto& event) {
            return event.before_step == step.number;
          });
      if (event_before_step) {
        path_valid = false;
      }

      if (step.kind == "step:NAVIGATE" && step.fixture) {
        path_valid = NavigateAndVerify(*step.fixture);
        AppendStep(&record, step,
                   path_valid ? "executed-and-verified" : "executed");
        if (!path_valid || !StepProducesPageFact(*scenario, step)) {
          continue;
        }
      } else if ((step.kind != "step:OBSERVE" && step.kind != "step:EXTRACT") ||
                 !step.fixture || !path_valid ||
                 (step.kind == "step:EXTRACT" &&
                  !StepProducesPageFact(*scenario, step))) {
        path_valid = false;
        continue;
      }

      ++observation_steps_attempted;
      std::optional<ObservationEnvelope> envelope =
          ObserveFixtureFrame(*step.fixture);
      if (!envelope) {
        if (step.kind != "step:NAVIGATE") {
          AppendStep(&record, step, "executed");
        }
        path_valid = false;
        continue;
      }
      ++observations_returned;
      const bool result_is_observation =
          envelope->code == ObservationResultCode::kOk ||
          envelope->code == ObservationResultCode::kIncomplete;
      std::optional<GraphPayload> graph =
          ReadGraphPayload(envelope->graph_payload);
      AppendObservationMetrics(&record, step, *envelope, graph.has_value());
      if (!result_is_observation ||
          envelope->encoding != GraphPayloadEncoding::kBipContract || !graph) {
        if (step.kind != "step:NAVIGATE") {
          AppendStep(&record, step, "executed");
        }
        path_valid = false;
        continue;
      }

      ++observations_decoded;
      if (step.kind != "step:NAVIGATE") {
        AppendStep(&record, step, "executed-and-verified");
      }
      const bool graph_complete =
          envelope->code == ObservationResultCode::kOk &&
          !envelope->truncation.truncated;
      AppendPageFacts(&record, *scenario, step, *graph, graph_complete,
                      &emitted_fact_ids);
    }

    record.Set("observation_steps_attempted",
               static_cast<int>(observation_steps_attempted));
    record.Set("observations_returned",
               static_cast<int>(observations_returned));
    record.Set("observations_decoded", static_cast<int>(observations_decoded));
    AddSecurityResult(&record);
    ASSERT_TRUE(EmitTaskBenchmarkRecord(record));
  }
};

#define TAFFY_TASK_BENCHMARK_CASE(test_name, scenario_id)                  \
  IN_PROC_BROWSER_TEST_F(TaskBenchmarkObservationBrowserTest, test_name) { \
    ExecuteScenario(scenario_id);                                          \
  }

TAFFY_TASK_BENCHMARK_CASE(TB_101, "TB-101")
TAFFY_TASK_BENCHMARK_CASE(TB_102, "TB-102")
TAFFY_TASK_BENCHMARK_CASE(TB_103, "TB-103")
TAFFY_TASK_BENCHMARK_CASE(TB_104, "TB-104")
TAFFY_TASK_BENCHMARK_CASE(TB_105, "TB-105")
TAFFY_TASK_BENCHMARK_CASE(TB_106, "TB-106")
TAFFY_TASK_BENCHMARK_CASE(TB_201, "TB-201")
TAFFY_TASK_BENCHMARK_CASE(TB_202, "TB-202")
TAFFY_TASK_BENCHMARK_CASE(TB_203, "TB-203")
TAFFY_TASK_BENCHMARK_CASE(TB_204, "TB-204")
TAFFY_TASK_BENCHMARK_CASE(TB_205, "TB-205")
TAFFY_TASK_BENCHMARK_CASE(TB_301, "TB-301")
TAFFY_TASK_BENCHMARK_CASE(TB_302, "TB-302")
TAFFY_TASK_BENCHMARK_CASE(TB_303, "TB-303")
TAFFY_TASK_BENCHMARK_CASE(TB_304, "TB-304")
TAFFY_TASK_BENCHMARK_CASE(TB_401, "TB-401")
TAFFY_TASK_BENCHMARK_CASE(TB_402, "TB-402")
TAFFY_TASK_BENCHMARK_CASE(TB_403, "TB-403")
TAFFY_TASK_BENCHMARK_CASE(TB_404, "TB-404")
TAFFY_TASK_BENCHMARK_CASE(TB_405, "TB-405")
TAFFY_TASK_BENCHMARK_CASE(TB_406, "TB-406")
TAFFY_TASK_BENCHMARK_CASE(TB_407, "TB-407")
TAFFY_TASK_BENCHMARK_CASE(TB_408, "TB-408")
TAFFY_TASK_BENCHMARK_CASE(TB_501, "TB-501")
TAFFY_TASK_BENCHMARK_CASE(TB_502, "TB-502")
TAFFY_TASK_BENCHMARK_CASE(TB_503, "TB-503")
TAFFY_TASK_BENCHMARK_CASE(TB_504, "TB-504")
TAFFY_TASK_BENCHMARK_CASE(TB_601, "TB-601")
TAFFY_TASK_BENCHMARK_CASE(TB_602, "TB-602")
TAFFY_TASK_BENCHMARK_CASE(TB_603, "TB-603")
TAFFY_TASK_BENCHMARK_CASE(TB_604, "TB-604")

#undef TAFFY_TASK_BENCHMARK_CASE

}  // namespace
}  // namespace taffy::test
