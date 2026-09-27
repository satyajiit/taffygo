// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// SP-07's process evidence. These tests deliberately use the shipping launcher
// and PythonToolService in a real Chromium utility process. The fixture library
// can report booleans, consume allocator budget, or stay busy, but cannot add a
// worker entrypoint: every request still dispatches through the frozen
// document.build implementation.

#include <stdint.h>

#include <optional>
#include <string>
#include <utility>

#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/process/process_handle.h"
#include "base/run_loop.h"
#include "base/scoped_environment_variable_override.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/bind.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "content/public/browser/service_process_host.h"
#include "content/public/browser/service_process_info.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/content_browser_test.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "taffy/browser/profile_python_tool_launcher.h"
#include "taffy/contracts/tool-runtime/generated/mojom/tool_runtime.mojom.h"
#include "taffy/contracts/tool-runtime/tool_runtime_service.mojom.h"
#include "taffy/test/tool/python_tool_worker_sp07_test_support.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace mojom = tool_runtime::mojom;
namespace sp07 = python_sp07_test;

class JobClient final : public mojom::ToolRuntimeClient {
 public:
  mojo::PendingRemote<mojom::ToolRuntimeClient> Bind() {
    auto remote = receiver_.BindNewPipeAndPassRemote();
    receiver_.set_disconnect_handler(
        base::BindOnce(&JobClient::OnDisconnected, base::Unretained(this)));
    return remote;
  }

  void Progress(mojom::ToolProgressPtr progress) override {}
  void OutputChunk(mojom::ToolOutputChunkPtr chunk) override {}
  void Completed(mojom::ToolCompletionPtr completion) override {
    completion_ = std::move(completion);
    if (terminal_quit_) {
      std::move(terminal_quit_).Run();
    }
  }

  void WaitForCompletionOrDisconnect() {
    if (completion_ || disconnected_) {
      return;
    }
    base::RunLoop loop;
    terminal_quit_ = loop.QuitClosure();
    loop.Run();
  }

  void WaitForDisconnect() {
    if (disconnected_) {
      return;
    }
    base::RunLoop loop;
    disconnect_quit_ = loop.QuitClosure();
    loop.Run();
  }

  const mojom::ToolCompletionPtr& completion() const { return completion_; }

 private:
  void OnDisconnected() {
    disconnected_ = true;
    if (terminal_quit_) {
      std::move(terminal_quit_).Run();
    }
    if (disconnect_quit_) {
      std::move(disconnect_quit_).Run();
    }
  }

  mojo::Receiver<mojom::ToolRuntimeClient> receiver_{this};
  mojom::ToolCompletionPtr completion_;
  base::OnceClosure terminal_quit_;
  base::OnceClosure disconnect_quit_;
  bool disconnected_ = false;
};

class PythonProcessObserver final
    : public content::ServiceProcessHost::Observer {
 public:
  PythonProcessObserver() { content::ServiceProcessHost::AddObserver(this); }
  PythonProcessObserver(const PythonProcessObserver&) = delete;
  PythonProcessObserver& operator=(const PythonProcessObserver&) = delete;
  ~PythonProcessObserver() override {
    content::ServiceProcessHost::RemoveObserver(this);
  }

  void WaitForLaunch() {
    if (launched_) {
      return;
    }
    base::RunLoop loop;
    launch_quit_ = loop.QuitClosure();
    loop.Run();
  }

  void WaitForExit() {
    if (exited_) {
      return;
    }
    base::RunLoop loop;
    exit_quit_ = loop.QuitClosure();
    loop.Run();
  }

  base::ProcessId pid() const { return pid_; }
  bool crashed() const { return crashed_; }

 private:
  void OnServiceProcessLaunched(
      const content::ServiceProcessInfo& info) override {
    if (!info.IsService<mojom::PythonToolService>()) {
      return;
    }
    if (launched_) {
      return;
    }
    pid_ = info.GetProcess().Pid();
    launched_ = true;
    if (launch_quit_) {
      std::move(launch_quit_).Run();
    }
  }

  void OnServiceProcessTerminatedNormally(
      const content::ServiceProcessInfo& info) override {
    ObserveExit(info, false);
  }

  void OnServiceProcessCrashed(
      const content::ServiceProcessInfo& info) override {
    ObserveExit(info, true);
  }

  void ObserveExit(const content::ServiceProcessInfo& info, bool crashed) {
    if (!info.IsService<mojom::PythonToolService>()) {
      return;
    }
    if (exited_) {
      return;
    }
    crashed_ = crashed;
    exited_ = true;
    if (exit_quit_) {
      std::move(exit_quit_).Run();
    }
  }

  base::OnceClosure launch_quit_;
  base::OnceClosure exit_quit_;
  base::ProcessId pid_ = base::kNullProcessId;
  bool launched_ = false;
  bool exited_ = false;
  bool crashed_ = false;
};

struct TerminalRun {
  mojom::ToolAdmissionStatus admission =
      mojom::ToolAdmissionStatus::kInvalidJob;
  mojom::ToolCompletionPtr completion;
  base::ProcessId pid = base::kNullProcessId;
  bool process_crashed = false;
  base::TimeDelta elapsed;
};

class PythonToolWorkerSp07BrowserTest : public content::ContentBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    content::ContentBrowserTest::SetUpOnMainThread();
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    ASSERT_TRUE(library_.Initialize(temp_dir_.GetPath()));
    browser_secret_.emplace(sp07::kBrowserSecretEnvironmentName,
                            "must-not-cross-worker-boundary");
    ASSERT_TRUE(browser_secret_->IsOverridden());
    launcher_ = base::MakeRefCounted<ProfilePythonToolLauncher>();
  }

  void TearDownOnMainThread() override {
    launcher_.reset();
    browser_secret_.reset();
    content::ContentBrowserTest::TearDownOnMainThread();
  }

  TerminalRun RunToTerminal(std::string job_id,
                            std::string_view input,
                            uint64_t max_memory_bytes,
                            uint64_t max_cpu_ms) {
    PythonProcessObserver process;
    JobClient client;
    mojom::ToolAdmissionPtr admission;
    base::RunLoop admitted;
    const base::TimeTicks started = base::TimeTicks::Now();
    launcher_->GetStartPort().Run(
        library_.MakeJob(std::move(job_id), input, max_memory_bytes,
                         max_cpu_ms),
        library_.OpenResources(), client.Bind(),
        base::BindLambdaForTesting([&](mojom::ToolAdmissionPtr result) {
          admission = std::move(result);
          admitted.Quit();
        }));
    admitted.Run();
    process.WaitForLaunch();
    if (admission &&
        admission->status == mojom::ToolAdmissionStatus::kAccepted) {
      client.WaitForCompletionOrDisconnect();
    }
    process.WaitForExit();
    TerminalRun result;
    if (admission) {
      result.admission = admission->status;
    }
    if (client.completion()) {
      result.completion = client.completion().Clone();
    }
    result.pid = process.pid();
    result.process_crashed = process.crashed();
    result.elapsed = base::TimeTicks::Now() - started;
    return result;
  }

  void ExpectFreshRecovery(std::string job_id) {
    TerminalRun recovery = RunToTerminal(
        std::move(job_id), sp07::kRecoveryInput, 64u << 20, 5000u);
    ASSERT_EQ(mojom::ToolAdmissionStatus::kAccepted, recovery.admission);
    ASSERT_TRUE(recovery.completion);
    ASSERT_EQ(mojom::ToolTerminalStatus::kCompleted,
              recovery.completion->status);
    ASSERT_TRUE(recovery.completion->success);
    ASSERT_TRUE(recovery.completion->success->bundled_python);
    EXPECT_TRUE(sp07::OutputContains(
        recovery.completion->success->bundled_python->output,
        "fresh-worker-recovered=1"));
  }

  base::ScopedTempDir temp_dir_;
  sp07::ProbeLibrary library_;
  std::optional<base::ScopedEnvironmentVariableOverride> browser_secret_;
  scoped_refptr<ProfilePythonToolLauncher> launcher_;
};

IN_PROC_BROWSER_TEST_F(PythonToolWorkerSp07BrowserTest,
                       SandboxProbeHasNoAmbientAuthority) {
  static_assert(mojom::PythonToolService::kServiceSandbox ==
                sandbox::mojom::Sandbox::kService);
  TerminalRun run =
      RunToTerminal("job-sandbox", sp07::kSandboxProbeInput, 64u << 20, 5000u);
  ASSERT_EQ(mojom::ToolAdmissionStatus::kAccepted, run.admission);
  ASSERT_NE(base::kNullProcessId, run.pid);
  EXPECT_NE(base::GetCurrentProcId(), run.pid);
  EXPECT_FALSE(run.process_crashed);
  ASSERT_TRUE(run.completion);
  ASSERT_EQ(mojom::ToolTerminalStatus::kCompleted, run.completion->status);
  ASSERT_TRUE(run.completion->success);
  ASSERT_TRUE(run.completion->success->bundled_python);
  const auto& output = run.completion->success->bundled_python->output;
  constexpr std::string_view kRequiredProofs[] = {
      "sys-path-empty=1",
      "ambient-file-blocked=1",
      "browser-environment-hidden=1",
      "network-native-blocked=1",
      "ctypes-native-blocked=1",
      "subprocess-native-blocked=1",
      "subprocess-module-blocked=1",
      "unallowlisted-module-blocked=1",
  };
  for (std::string_view proof : kRequiredProofs) {
    EXPECT_TRUE(sp07::OutputContains(output, proof)) << proof;
  }
  EXPECT_EQ(0u, launcher_->running_process_count_for_testing());
  LOG(INFO) << "TAFFY_SP07 sandbox_probe_ms=" << run.elapsed.InMilliseconds();
}

IN_PROC_BROWSER_TEST_F(PythonToolWorkerSp07BrowserTest,
                       CpuAndMemoryLimitsEndOnlyTheirWorker) {
  struct LimitCase {
    std::string_view suffix;
    std::string_view input;
    uint64_t memory_bytes;
    uint64_t cpu_ms;
    mojom::ToolTerminalStatus terminal;
  };
  constexpr LimitCase kCases[] = {
      {"memory", sp07::kMemoryPressureInput, 64u << 20, 5000u,
       mojom::ToolTerminalStatus::kResourceLimit},
      {"cpu", sp07::kLongRunningInput, 64u << 20, 50u,
       mojom::ToolTerminalStatus::kDeadlineExceeded},
  };
  for (const LimitCase& limit : kCases) {
    TerminalRun run =
        RunToTerminal("job-" + std::string(limit.suffix), limit.input,
                      limit.memory_bytes, limit.cpu_ms);
    SCOPED_TRACE(limit.suffix);
    ASSERT_EQ(mojom::ToolAdmissionStatus::kAccepted, run.admission);
    EXPECT_FALSE(run.process_crashed);
    ASSERT_TRUE(run.completion);
    EXPECT_EQ(limit.terminal, run.completion->status);
    EXPECT_FALSE(run.completion->success);
    LOG(INFO) << "TAFFY_SP07 " << limit.suffix
              << "_limit_ms=" << run.elapsed.InMilliseconds();
  }
  EXPECT_EQ(0u, launcher_->running_process_count_for_testing());
  ExpectFreshRecovery("job-after-limits");
  EXPECT_EQ(3u, launcher_->total_process_launch_count_for_testing());
}

IN_PROC_BROWSER_TEST_F(PythonToolWorkerSp07BrowserTest,
                       CancellationKillsAdmittedWorkerAndRecovers) {
  PythonProcessObserver process;
  JobClient client;
  mojom::ToolAdmissionPtr admission;
  base::RunLoop admitted;
  launcher_->GetStartPort().Run(
      library_.MakeJob("job-cancel", sp07::kLongRunningInput, 64u << 20, 5000u),
      library_.OpenResources(), client.Bind(),
      base::BindLambdaForTesting([&](mojom::ToolAdmissionPtr result) {
        admission = std::move(result);
        admitted.Quit();
      }));
  admitted.Run();
  process.WaitForLaunch();
  ASSERT_TRUE(admission);
  ASSERT_EQ(mojom::ToolAdmissionStatus::kAccepted, admission->status);
  ASSERT_EQ(1u, launcher_->running_process_count_for_testing());

  // Admission is the worker's acknowledgement. A short dwell lets the posted
  // interpreter task enter the deliberately non-terminating Python loop; it
  // is not used to decide the outcome or to poll a race.
  base::RunLoop interpreter_dwell;
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, interpreter_dwell.QuitClosure(), base::Milliseconds(100));
  interpreter_dwell.Run();
  const base::TimeTicks cancelled = base::TimeTicks::Now();
  launcher_->GetCancelPort().Run("job-cancel");
  client.WaitForDisconnect();
  process.WaitForExit();
  const base::TimeDelta cancellation_elapsed =
      base::TimeTicks::Now() - cancelled;
  EXPECT_FALSE(client.completion());
  EXPECT_EQ(0u, launcher_->running_process_count_for_testing());
  EXPECT_LT(cancellation_elapsed, base::Seconds(5));
  LOG(INFO) << "TAFFY_SP07 cancellation_process_exit_ms="
            << cancellation_elapsed.InMilliseconds()
            << " crashed=" << process.crashed();

  ExpectFreshRecovery("job-after-cancel");
  EXPECT_EQ(2u, launcher_->total_process_launch_count_for_testing());
}

}  // namespace
}  // namespace taffy
