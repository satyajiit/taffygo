// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "base/memory/raw_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/values.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test_utils.h"
#include "crypto/sha2.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "taffy/browser/core_api/profile_core_api_facade.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/test/benchmark/task_benchmark_profile_probe_internal.h"
#include "taffy/test/recovery/core_api_status_observer.h"
#include "taffy/test/recovery/workspace_vertical_test_support.h"
#include "taffy/test/support/fixture_origin_map.h"
#include "taffy/test/support/task_benchmark_scenario.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy::test {
namespace {

namespace api = core_api::mojom;

constexpr int kTaskBenchmarkSourceTabId = 504;

bool Fail(std::string_view reason, base::DictValue* record) {
  ADD_FAILURE() << reason;
  record->Set("adapter_state", "diagnostic-failed-before-attestation");
  record->Set("adapter_error", reason);
  record->Set("actual_outcome", "failed");
  record->Set("probe_outcome", "failed");
  record->Set("audit_state", "not-read");
  record->Set("audit_records", base::ListValue());
  record->Set("task_id", "");
  record->Set("live_model_calls", 0);
  return false;
}

class ScopedTaskSource final {
 public:
  explicit ScopedTaskSource(CoreServiceManager* core) : core_(core) {}
  ScopedTaskSource(const ScopedTaskSource&) = delete;
  ScopedTaskSource& operator=(const ScopedTaskSource&) = delete;
  ~ScopedTaskSource() {
    if (window_token_ == 0u) {
      return;
    }
    core_->DeactivateTaskSourceWindow(window_token_);
    core_->UnregisterTaskSourceWindow(window_token_);
  }

  bool Register(content::WebContents* tab) {
    window_token_ = core_->RegisterTaskSourceWindow();
    return window_token_ != 0u &&
           core_->RegisterTaskSourceTab(window_token_,
                                        kTaskBenchmarkSourceTabId, tab) &&
           core_->SelectTaskSourceTab(window_token_, kTaskBenchmarkSourceTabId,
                                      tab) &&
           core_->ActivateTaskSourceWindow(window_token_);
  }

 private:
  const raw_ptr<CoreServiceManager> core_;
  uint64_t window_token_ = 0u;
};

std::string Sha256(std::string_view value) {
  return base::ToLowerASCII(base::HexEncode(crypto::SHA256HashString(value)));
}

}  // namespace

bool RunTaskBenchmarkPageReadProbe(
    const TaskBenchmarkProfileProbeContext& context,
    base::DictValue* record) {
  const GURL source_url =
      context.origins->FixtureUrl(context.scenario->starting_fixture());
  if (!content::NavigateToURL(context.active_tab, source_url) ||
      context.active_tab->GetLastCommittedURL() != source_url) {
    return Fail("TB-504 did not commit its declared starting fixture", record);
  }
  TaffyPageIntelligenceHost* const page_host =
      TaffyPageIntelligenceHost::FromWebContents(context.active_tab);
  if (!page_host) {
    return Fail("TB-504 had no shipping PageIntelligence host", record);
  }
  ScopedTaskSource source(context.core);
  if (!source.Register(context.active_tab)) {
    return Fail("The TB-504 fixture could not become the selected product tab",
                record);
  }

  ProfileCoreApiFacade facade(context.core);
  mojo::Remote<api::TaffyProfileCoreApi> remote;
  mojo::Receiver<api::TaffyProfileCoreApi> receiver(
      &facade, remote.BindNewPipeAndPassReceiver());
  CoreApiStatusObserver observer;
  remote->Observe(observer.BindNewPipeAndPassRemote());
  if (!base::test::RunUntil([&]() {
        return observer.snapshot_seen() &&
               observer.availability() == api::CoreAvailability::kReady;
      })) {
    return Fail("The TB-504 CoreStatus never became ready", record);
  }

  auto consent = api::TaskConsentPreview::New();
  consent->source_hosts = {std::string(source_url.host())};
  consent->source_discovery_enabled = false;
  consent->new_source_cap = 0u;
  consent->provider_route = api::TaskProviderRoute::kNoModelRequired;
  base::test::TestFuture<api::CoreApiSubmissionStatus> start;
  remote->StartTask(context.scenario->goal(),
                    api::TaskTemplateId::kBuildSourceTable, std::nullopt,
                    std::move(consent), std::nullopt, start.GetCallback());
  if (start.Get() != api::CoreApiSubmissionStatus::kAccepted) {
    return Fail("The TB-504 selected-page task was not accepted", record);
  }
  if (!base::test::RunUntil([&observer]() {
        const std::optional<ObservedTaskStatus> task = observer.only_task();
        return task && task->pending_action_id.has_value();
      })) {
    return Fail("TB-504 never published its exact page-read approval", record);
  }
  const std::optional<ObservedTaskStatus> pending = observer.only_task();
  if (!pending || !pending->pending_action_id) {
    return Fail("TB-504 published no actionable approval identity", record);
  }
  base::test::TestFuture<api::CoreApiSubmissionStatus> approval;
  remote->ApproveAction(pending->task_id, *pending->pending_action_id,
                        approval.GetCallback());
  if (approval.Get() != api::CoreApiSubmissionStatus::kAccepted) {
    return Fail("The exact TB-504 page-read approval was refused", record);
  }
  if (!base::test::RunUntil([&]() {
        const std::optional<ObservedTaskStatus> task = observer.only_task();
        const std::optional<ObservedWorkspaceStatus> workspace =
            observer.only_workspace();
        return task && task->phase == api::TaskPhase::kCompleted &&
               task->workspace_id && workspace &&
               workspace->workspace_id == *task->workspace_id &&
               workspace->phase == api::WorkspacePhase::kDone &&
               !workspace->facts.empty();
      })) {
    return Fail("TB-504 did not publish a completed source-backed workspace",
                record);
  }
  const std::optional<ObservedWorkspaceStatus> workspace =
      observer.only_workspace();
  if (!workspace || page_host->observations_submitted() != 1u) {
    return Fail("TB-504 did not submit exactly one selected-page observation",
                record);
  }
  ExpectSingleDomSourceWorkspace(*workspace, std::string(source_url.host()));
  const std::optional<ObservedWorkspaceExports> exports =
      RequestObservedWorkspaceExports(&remote, &observer, "tb504-diagnostic",
                                      *workspace,
                                      /*require_source_backed_content=*/true);
  if (!exports) {
    return Fail("TB-504 could not export its observed workspace", record);
  }

  base::DictValue artifact;
  artifact.Set("declared_kind", context.scenario->expected_artifact_kind());
  artifact.Set("observed_kind", "workspace-markdown-and-csv");
  artifact.Set("contract_match", false);
  artifact.Set("source_count", static_cast<int>(workspace->sources.size()));
  artifact.Set("fact_count", static_cast<int>(workspace->facts.size()));
  artifact.Set("markdown_bytes",
               static_cast<int>(exports->markdown.content.size()));
  artifact.Set("markdown_sha256", Sha256(exports->markdown.content));
  artifact.Set("csv_bytes", static_cast<int>(exports->csv.content.size()));
  artifact.Set("csv_sha256", Sha256(exports->csv.content));
  record->Set("artifact_attestation", std::move(artifact));
  record->Set("adapter_state", "page-read-artifact-diagnostic");
  record->Set(
      "adapter_note",
      "TB-504 ran the real selected-page observation and exported its exact "
      "source-backed workspace. That verifies the page-read path only. The "
      "declared artifact is an answer, while this deterministic task produces "
      "workspace Markdown and CSV; it also asks for a real read approval. The "
      "adapter therefore leaves the scenario outcome, forbidden facts, and "
      "answer attestation unverified.");
  record->Set("observations_submitted",
              static_cast<int>(page_host->observations_submitted()));
  record->Set("workspace_source_count",
              static_cast<int>(workspace->sources.size()));
  record->Set("workspace_fact_count",
              static_cast<int>(workspace->facts.size()));

  const std::string disclosure_material =
      exports->markdown.content + exports->csv.content;
  return FinalizeTaskBenchmarkProfileEvidence(
      context.core, &observer, context.origins, disclosure_material, record);
}

}  // namespace taffy::test
