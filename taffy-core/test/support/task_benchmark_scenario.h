// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_SUPPORT_TASK_BENCHMARK_SCENARIO_H_
#define TAFFY_TEST_SUPPORT_TASK_BENCHMARK_SCENARIO_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace taffy::test {

// One immutable Task Benchmark scenario, joined from the corpus manifest and
// its scenario.json execution declaration. Load() is the single execution
// seam used by both browser-test adapters: it verifies the join and computes
// the exact canonical digest expected by the host-side scorer.
class TaskBenchmarkScenario {
 public:
  struct Step {
    uint32_t number = 0u;
    std::string kind;
    std::optional<std::string> fixture;
    std::vector<std::string> produces;
  };

  struct RequiredFact {
    std::string id;
    std::string evidence;
    std::optional<std::string> source_fixture;
    std::optional<std::string> source_field;
    std::string tolerance;
  };

  struct InjectedEvent {
    uint32_t before_step = 0u;
    std::string event;
  };

  TaskBenchmarkScenario(const TaskBenchmarkScenario&);
  TaskBenchmarkScenario(TaskBenchmarkScenario&&) noexcept;
  TaskBenchmarkScenario& operator=(const TaskBenchmarkScenario&);
  TaskBenchmarkScenario& operator=(TaskBenchmarkScenario&&) noexcept;
  ~TaskBenchmarkScenario();

  // Returns nullopt and names the malformed or missing input in `error`.
  // Corpus validation still belongs to test-fixtures/tasks/check.py; this is
  // the execution-side fail-closed reader and never repairs or defaults data.
  static std::optional<TaskBenchmarkScenario> Load(std::string_view scenario_id,
                                                   std::string* error);

  const std::string& id() const { return id_; }
  const std::string& digest() const { return digest_; }
  const std::string& goal() const { return goal_; }
  const std::string& expected_outcome() const { return expected_outcome_; }
  const std::string& expected_artifact_kind() const {
    return expected_artifact_kind_;
  }
  const std::string& expected_artifact_rule() const {
    return expected_artifact_rule_;
  }
  const std::string& scripted_model_outputs_state() const {
    return scripted_model_outputs_state_;
  }
  const std::string& starting_fixture() const { return starting_fixture_; }
  const std::vector<std::string>& page_fixtures() const {
    return page_fixtures_;
  }
  const std::vector<std::string>& source_origins() const {
    return source_origins_;
  }
  const std::vector<Step>& steps() const { return steps_; }
  const std::vector<RequiredFact>& required_facts() const {
    return required_facts_;
  }
  const std::vector<std::string>& forbidden_fact_ids() const {
    return forbidden_fact_ids_;
  }
  const std::vector<InjectedEvent>& injected_events() const {
    return injected_events_;
  }
  const std::vector<std::string>& expected_audit_events() const {
    return expected_audit_events_;
  }

  const RequiredFact* FindRequiredFact(std::string_view fact_id) const;

 private:
  TaskBenchmarkScenario();

  std::string id_;
  std::string digest_;
  std::string goal_;
  std::string expected_outcome_;
  std::string expected_artifact_kind_;
  std::string expected_artifact_rule_;
  std::string scripted_model_outputs_state_;
  std::string starting_fixture_;
  std::vector<std::string> page_fixtures_;
  std::vector<std::string> source_origins_;
  std::vector<Step> steps_;
  std::vector<RequiredFact> required_facts_;
  std::vector<std::string> forbidden_fact_ids_;
  std::vector<InjectedEvent> injected_events_;
  std::vector<std::string> expected_audit_events_;
};

}  // namespace taffy::test

#endif  // TAFFY_TEST_SUPPORT_TASK_BENCHMARK_SCENARIO_H_
