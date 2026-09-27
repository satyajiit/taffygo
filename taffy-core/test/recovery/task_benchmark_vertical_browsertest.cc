// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <string_view>

#include "base/test/test_future.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/test/base/chrome_test_utils.h"
#include "chrome/test/base/platform_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "net/dns/mock_host_resolver.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_factory.h"
#include "taffy/test/benchmark/task_benchmark_profile_probe.h"
#include "taffy/test/recovery/empty_vault_profile_platform_adapter.h"
#include "taffy/test/support/fixture_origin_map.h"
#include "taffy/test/support/task_benchmark_record.h"
#include "taffy/test/support/task_benchmark_scenario.h"
#include "testing/gtest/include/gtest/gtest.h"

// The Chrome-profile half of the Task Benchmark. It dispatches only the two
// scenario-specific probes implemented by task_benchmark_profile_probe.cc.
// Every other row emits an explicit unsupported result; no generic workflow
// is allowed to impersonate the immutable scenario.

namespace taffy::test {
namespace {

class TaskBenchmarkVerticalBrowserTest : public PlatformBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    origins_.Start();
  }

  content::WebContents* active_tab_contents() {
    return chrome_test_utils::GetActiveWebContents(this);
  }

  void ExecuteScenario(std::string_view scenario_id) {
    std::string error;
    std::optional<TaskBenchmarkScenario> scenario =
        TaskBenchmarkScenario::Load(scenario_id, &error);
    ASSERT_TRUE(scenario) << error;

    content::WebContents* const active_tab = active_tab_contents();
    ASSERT_TRUE(active_tab);
    Profile* const profile =
        Profile::FromBrowserContext(active_tab->GetBrowserContext());
    ASSERT_TRUE(profile);
    CoreServiceManager* const core =
        CoreServiceManagerFactory::GetForProfile(profile);
    ASSERT_TRUE(core);
    core->BindPlatformAdapter(platform_adapter_.BindNewPipeAndPassRemote());
    base::test::TestFuture<bool> prepared;
    core->PrepareForCoreApi(prepared.GetCallback());
    ASSERT_TRUE(prepared.Get());
    ASSERT_EQ(CoreServiceManager::Availability::kReady, core->availability());

    base::DictValue record = NewTaskBenchmarkRecord(*scenario, "task");
    EXPECT_TRUE(ExecuteTaskBenchmarkProfileProbe(
        *scenario, profile, core, active_tab, &origins_, &record));
    ASSERT_TRUE(EmitTaskBenchmarkRecord(record));
  }

 private:
  FixtureOriginMap origins_;
  EmptyVaultProfilePlatformAdapter platform_adapter_;
};

#define TAFFY_TASK_BENCHMARK_CASE(test_name, scenario_id)               \
  IN_PROC_BROWSER_TEST_F(TaskBenchmarkVerticalBrowserTest, test_name) { \
    ExecuteScenario(scenario_id);                                       \
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
