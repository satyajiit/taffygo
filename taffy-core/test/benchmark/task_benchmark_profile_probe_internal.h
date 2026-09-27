// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_BENCHMARK_TASK_BENCHMARK_PROFILE_PROBE_INTERNAL_H_
#define TAFFY_TEST_BENCHMARK_TASK_BENCHMARK_PROFILE_PROBE_INTERNAL_H_

#include <stdint.h>

#include <string>
#include <string_view>

#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/values.h"

class Profile;

namespace content {
class WebContents;
}

namespace taffy {
class CoreServiceManager;
}

namespace taffy::test {

class CoreApiStatusObserver;
class FixtureOriginMap;
class TaskBenchmarkScenario;

// Everything one Profile adapter probe borrows for a single run: the scenario
// it is proving, and the four live objects the browser test already owns. The
// context owns none of them and every one of them outlives the probe.
//
// The scenario is held as a raw_ref rather than a native reference because a
// borrowed member is checked in the configurations that check borrows. The
// constructor is what keeps the caller writing a plain reference: raw_ref's
// constructor is explicit, so an aggregate initializer could not bind one.
struct TaskBenchmarkProfileProbeContext {
  TaskBenchmarkProfileProbeContext(const TaskBenchmarkScenario& scenario,
                                   Profile* profile,
                                   CoreServiceManager* core,
                                   content::WebContents* active_tab,
                                   FixtureOriginMap* origins)
      : scenario(scenario),
        profile(profile),
        core(core),
        active_tab(active_tab),
        origins(origins) {}

  const raw_ref<const TaskBenchmarkScenario> scenario;
  const raw_ptr<Profile> profile;
  const raw_ptr<CoreServiceManager> core;
  const raw_ptr<content::WebContents> active_tab;
  const raw_ptr<FixtureOriginMap> origins;
};

bool RunTaskBenchmarkCancellationProbe(
    const TaskBenchmarkProfileProbeContext& context,
    base::DictValue* record);
bool RunTaskBenchmarkPageReadProbe(
    const TaskBenchmarkProfileProbeContext& context,
    base::DictValue* record);

// Waits for the exact browser-private terminal revision in the current core
// generation, reads its durable audit in storage order, and adds a disclosure
// scan over the workspace plus any additional local artifact bytes.
bool FinalizeTaskBenchmarkProfileEvidence(
    CoreServiceManager* core,
    CoreApiStatusObserver* observer,
    FixtureOriginMap* origins,
    std::string_view additional_disclosure_material,
    base::DictValue* record);

void AppendVerifiedTaskBenchmarkStep(const TaskBenchmarkScenario& scenario,
                                     uint32_t number,
                                     base::DictValue* record);
void AppendTaskBenchmarkAuditFact(const TaskBenchmarkScenario& scenario,
                                  std::string_view fact_id,
                                  std::string_view proof_kind,
                                  std::string_view proof_key,
                                  std::string_view proof_value,
                                  base::DictValue* record);
void AppendTaskBenchmarkForbiddenCheck(const TaskBenchmarkScenario& scenario,
                                       std::string_view fact_id,
                                       base::DictValue* record);

}  // namespace taffy::test

#endif  // TAFFY_TEST_BENCHMARK_TASK_BENCHMARK_PROFILE_PROBE_INTERNAL_H_
