// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The exact browser-to-worker path for one registered Python invocation. The
// archive below is a browser-owned test fixture, not request data: the request
// still carries only an entrypoint identifier and bounded declarative JSON.

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/observer_list.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/content_browser_test.h"
#include "crypto/hash.h"
#include "taffy/browser/profile_python_tool_launcher.h"
#include "taffy/browser/profile_tool_artifact_broker.h"
#include "taffy/browser/profile_tool_handle_store.h"
#include "taffy/contracts/core-service/generated/mojom/core_service.mojom.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"
#include "taffy/services/tool-runtime/supervisor/tool_job_resources.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/zlib/google/zip.h"

namespace taffy {
namespace {

constexpr char kInput[] =
    R"({"document":{"title":"Hello","sections":[{"heading":"One","paragraphs":["Body"]}]}})";
constexpr char kSpreadsheetInput[] =
    R"({"sheet_name":"Data","csv":"A,B\n1,2"})";

bool OutputContains(base::span<const uint8_t> output,
                    std::string_view expected) {
  return std::string_view(reinterpret_cast<const char*>(output.data()),
                          output.size())
             .find(expected) != std::string_view::npos;
}

class ArtifactObserver final : public CoreServiceObserver {
 public:
  bool OnTaskArtifactExport(const std::string& task_id,
                            const std::string& artifact_id,
                            core_service::mojom::TaskArtifactKind kind,
                            const std::vector<uint8_t>& content) override {
    seen_task_id = task_id;
    seen_artifact_id = artifact_id;
    seen_kind = kind;
    seen_content = content;
    return true;
  }

  std::string seen_task_id;
  std::string seen_artifact_id;
  core_service::mojom::TaskArtifactKind seen_kind =
      core_service::mojom::TaskArtifactKind::kMarkdown;
  std::vector<uint8_t> seen_content;
};

core_service::mojom::ToolJobEffectPtr CorePythonJob(std::string job_id,
                                                    std::string_view entrypoint,
                                                    std::string_view input) {
  auto job = core_service::mojom::ToolJobEffect::New();
  job->job_id = std::move(job_id);
  job->runtime = core_service::mojom::ToolRuntimeKind::kPython;
  job->tool_id = "python.execute";
  job->tool_version = "1";
  job->operation_kind =
      core_service::mojom::ToolOperation::kRunBundledPythonModule;
  job->budget = core_service::mojom::ToolResourceBudget::New(
      262144u, 1u << 20, 64u << 20, 5000u, 0u, 1u);
  const auto bytes = base::as_byte_span(input);
  job->bundled_python = core_service::mojom::BundledPythonArguments::New(
      std::string(entrypoint),
      std::vector<uint8_t>(bytes.begin(), bytes.end()));
  job->task_id = "task-python";
  return job;
}

core_service::mojom::TaskEffectBindingPtr ToolBinding(
    std::string suffix,
    core_service::mojom::ToolJobEffectPtr job) {
  auto binding = core_service::mojom::TaskEffectBinding::New();
  binding->operation = core_service::mojom::OperationEnvelope::New(
      "operation-" + suffix, 1u, 1u,
      static_cast<uint64_t>(
          base::TimeTicks::Now().since_origin().InMilliseconds()) +
          60'000u,
      "idempotency-" + suffix);
  binding->effect_id = "effect-" + suffix;
  binding->task_id = "task-python";
  binding->kind = core_service::mojom::TaskReducerEffectKind::kRunToolJob;
  binding->tool_job = core_service::mojom::TaskToolJobEffect::New(
      "action-" + suffix, job->job_id,
      core_service::mojom::ToolRuntimeKind::kPython, std::move(job));
  return binding;
}

class PythonToolWorkerBrowserTest : public content::ContentBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    content::ContentBrowserTest::SetUpOnMainThread();
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    const base::FilePath source = temp_dir_.GetPath().AppendASCII("library");
    ASSERT_TRUE(base::CreateDirectory(source));
    // Only the frozen entrypoint selects this module. The request cannot name
    // it and cannot contribute source; this tiny parser keeps the process test
    // independent of the separately delivered production library.
    constexpr char kJsonModule[] = R"PY(
class JSONDecodeError(ValueError):
    pass
def loads(value):
    if value == '{"document":{"title":"Hello","sections":[{"heading":"One","paragraphs":["Body"]}]}}':
        return {"document": {"title": "Hello", "sections": [
            {"heading": "One", "paragraphs": ["Body"]}]}}
    if value == '{"sheet_name":"Data","csv":"A,B\\n1,2"}':
        return {"sheet_name": "Data", "csv": "A,B\n1,2"}
    raise JSONDecodeError("unexpected test input")
)PY";
    ASSERT_TRUE(base::WriteFile(source.AppendASCII("json.py"), kJsonModule));
    archive_ = temp_dir_.GetPath().AppendASCII("stdlib.zip");
    ASSERT_TRUE(zip::Zip(source, archive_, /*include_hidden_files=*/false));
    const std::optional<std::vector<uint8_t>> library =
        base::ReadFileToBytes(archive_);
    ASSERT_TRUE(library);
    library_ = *library;
    launcher_ = base::MakeRefCounted<ProfilePythonToolLauncher>();
  }

  void TearDownOnMainThread() override {
    launcher_.reset();
    content::ContentBrowserTest::TearDownOnMainThread();
  }

 public:
  bool OpenLibrary(tool_job_resources::PythonLibrarySource* source) {
    base::ScopedAllowBlockingForTesting allow_blocking;
    if (!source) {
      return false;
    }
    source->file =
        base::File(archive_, base::File::FLAG_OPEN | base::File::FLAG_READ);
    if (!source->file.IsValid()) {
      return false;
    }
    source->library_id = "python-stdlib";
    source->library_version = "test-1";
    source->byte_length = library_.size();
    const auto digest = crypto::hash::Sha256(base::span(library_));
    source->digest.assign(digest.begin(), digest.end());
    return true;
  }

 protected:
  base::ScopedTempDir temp_dir_;
  base::FilePath archive_;
  std::vector<uint8_t> library_;
  scoped_refptr<ProfilePythonToolLauncher> launcher_;
};

IN_PROC_BROWSER_TEST_F(
    PythonToolWorkerBrowserTest,
    RegisteredBuildersSurviveWorkerResultConsumptionInBrowserCustody) {
  namespace service = core_service::mojom;
  ProfileToolSupervisor supervisor(
      1u,
      ProfileToolSupervisor::PythonPorts(launcher_->GetStartPort(),
                                         launcher_->GetCancelPort()),
      ProfileToolSupervisor::LocalModelPorts::Unsupported(),
      ProfileToolSupervisor::MediaPorts::Unsupported());
  supervisor.SetPythonLibraryPort(base::BindRepeating(
      &PythonToolWorkerBrowserTest::OpenLibrary, base::Unretained(this)));
  auto handles = base::MakeRefCounted<ProfileToolHandleStore>();
  ProfileToolArtifactBroker artifacts(
      &supervisor, handles, temp_dir_.GetPath().AppendASCII("outputs"),
      /*private_profile=*/false);

  struct BuildCase {
    std::string_view suffix;
    std::string_view entrypoint;
    std::string_view input;
    service::TaskArtifactKind kind;
    std::string_view archive_part;
    std::string_view expected_text;
  };
  constexpr BuildCase kCases[] = {
      {"document", "document.build", kInput, service::TaskArtifactKind::kDocx,
       "word/document.xml", "Hello"},
      {"spreadsheet", "spreadsheet.build", kSpreadsheetInput,
       service::TaskArtifactKind::kXlsx, "xl/workbook.xml", "Data"},
  };
  service::TaskArtifactEffectPtr last_artifact;
  for (const BuildCase& build : kCases) {
    service::TaskEffectBindingPtr binding;
    base::RunLoop prepared;
    artifacts.PrepareTaskToolJob(
        ToolBinding(std::string(build.suffix),
                    CorePythonJob("job-" + std::string(build.suffix),
                                  build.entrypoint, build.input)),
        base::BindLambdaForTesting([&](service::TaskEffectBindingPtr result) {
          binding = std::move(result);
          prepared.Quit();
        }));
    prepared.Run();
    ASSERT_TRUE(binding);

    service::ToolEffectResultPtr tool_result;
    base::RunLoop completed;
    supervisor.Start(
        binding->operation.Clone(), binding->effect_id,
        binding->tool_job->job.Clone(),
        base::BindLambdaForTesting([&](service::ToolEffectResultPtr result) {
          tool_result = std::move(result);
          completed.Quit();
        }));
    completed.Run();
    // Completion is observable only after the one-job process remote and its
    // relay have closed. Waiting for a later disconnect would reintroduce the
    // cross-pipe ordering race this launcher owns.
    EXPECT_EQ(0u, launcher_->running_process_count_for_testing());
    ASSERT_TRUE(tool_result);
    ASSERT_EQ(service::ToolTerminalStatus::kCompleted, tool_result->status);
    ASSERT_TRUE(tool_result->success);
    ASSERT_TRUE(tool_result->success->bundled_python);
    const std::vector<uint8_t> worker_output =
        tool_result->success->bundled_python->output;
    EXPECT_TRUE(
        OutputContains(worker_output, std::string_view("PK\x03\x04", 4u)));
    EXPECT_TRUE(OutputContains(worker_output, build.archive_part));
    EXPECT_TRUE(OutputContains(worker_output, build.expected_text));

    auto effect_result = service::EffectResult::New();
    effect_result->operation = binding->operation.Clone();
    effect_result->effect_id = binding->effect_id;
    effect_result->kind = service::EffectKind::kToolJob;
    effect_result->status = service::EffectStatus::kCompleted;
    effect_result->tool = std::move(tool_result);
    service::TaskToolOutputReceiptPtr receipt;
    base::RunLoop retained;
    artifacts.RetainTaskToolOutput(
        *binding, std::move(effect_result),
        base::BindLambdaForTesting(
            [&](service::TaskToolOutputReceiptPtr result) {
              receipt = std::move(result);
              retained.Quit();
            }));
    retained.Run();
    ASSERT_TRUE(receipt);
    ASSERT_TRUE(receipt->artifact);
    EXPECT_EQ(build.kind, receipt->artifact->kind);
    EXPECT_EQ("artifact-action-" + std::string(build.suffix),
              receipt->artifact->artifact_id);
    EXPECT_EQ(worker_output.size(), receipt->byte_count);
    EXPECT_EQ(1u, receipt->chunk_count);
    const auto digest = crypto::hash::Sha256(base::span(worker_output));
    EXPECT_EQ(std::vector<uint8_t>(digest.begin(), digest.end()),
              receipt->digest);

    // The worker result is gone before Save/Share asks for the file. Only the
    // browser-resident verified copy can satisfy this export.
    last_artifact = service::TaskArtifactEffect::New(
        receipt->artifact->kind, receipt->artifact->artifact_id, 1u,
        std::vector<uint8_t>());
    ArtifactObserver observer;
    base::ObserverList<CoreServiceObserver> observers;
    observers.AddObserver(&observer);
    EXPECT_TRUE(artifacts.DeliverArtifact(observers, "task-python", 1u,
                                          *last_artifact));
    EXPECT_EQ("task-python", observer.seen_task_id);
    EXPECT_EQ(receipt->artifact->artifact_id, observer.seen_artifact_id);
    EXPECT_EQ(build.kind, observer.seen_kind);
    EXPECT_EQ(worker_output, observer.seen_content);
  }

  // Each builder used a fresh interpreter process. A process pool would make
  // the isolation and hard-cancellation claim a different contract.
  EXPECT_EQ(2u, launcher_->total_process_launch_count_for_testing());
  EXPECT_EQ(1u, launcher_->peak_process_count_for_testing());
  ASSERT_TRUE(last_artifact);
  artifacts.SetActiveGeneration(2u);
  EXPECT_EQ(0u, artifacts.artifact_count_for_testing());
  ArtifactObserver stale_observer;
  base::ObserverList<CoreServiceObserver> stale_observers;
  stale_observers.AddObserver(&stale_observer);
  EXPECT_FALSE(artifacts.DeliverArtifact(stale_observers, "task-python", 1u,
                                         *last_artifact));
}

}  // namespace
}  // namespace taffy
