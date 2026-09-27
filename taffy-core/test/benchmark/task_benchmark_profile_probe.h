// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_TEST_BENCHMARK_TASK_BENCHMARK_PROFILE_PROBE_H_
#define TAFFY_TEST_BENCHMARK_TASK_BENCHMARK_PROFILE_PROBE_H_

namespace base {
class DictValue;
}

class Profile;

namespace content {
class WebContents;
}

namespace taffy {
class CoreServiceManager;
}

namespace taffy::test {

class FixtureOriginMap;
class TaskBenchmarkScenario;

// Executes only the scenario-specific Profile paths this adapter genuinely
// implements. TB-302 is a local-script candidate diagnostic and TB-504 is a
// deterministic page-read/workspace-export diagnostic. Every other scenario
// is emitted as unsupported without substituting a generic task.
bool ExecuteTaskBenchmarkProfileProbe(const TaskBenchmarkScenario& scenario,
                                      Profile* profile,
                                      CoreServiceManager* core,
                                      content::WebContents* active_tab,
                                      FixtureOriginMap* origins,
                                      base::DictValue* record);

}  // namespace taffy::test

#endif  // TAFFY_TEST_BENCHMARK_TASK_BENCHMARK_PROFILE_PROBE_H_
