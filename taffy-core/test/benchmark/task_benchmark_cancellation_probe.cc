// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "base/values.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test_utils.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "taffy/browser/core_api/profile_core_api_facade.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/common/public/bip_identity.h"
#include "taffy/components/security/browser/action_authority.h"
#include "taffy/contracts/core-api/generated/mojom/core_api.mojom.h"
#include "taffy/test/benchmark/task_benchmark_dispatch_gate.h"
#include "taffy/test/benchmark/task_benchmark_model_script.h"
#include "taffy/test/benchmark/task_benchmark_profile_probe_internal.h"
#include "taffy/test/recovery/core_api_status_observer.h"
#include "taffy/test/support/fixture_origin_map.h"
#include "taffy/test/support/task_benchmark_audit_reader.h"
#include "taffy/test/support/task_benchmark_scenario.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy::test {
namespace {

namespace api = core_api::mojom;

constexpr int kTaskBenchmarkSourceTabId = 302;

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

const TaskBenchmarkScenario::Step* Step(const TaskBenchmarkScenario& scenario,
                                        uint32_t number) {
  for (const TaskBenchmarkScenario::Step& step : scenario.steps()) {
    if (step.number == number) {
      return &step;
    }
  }
  return nullptr;
}

void AppendNoEventAfterFact(const TaskBenchmarkScenario& scenario,
                            std::string_view fact_id,
                            std::string_view anchor,
                            std::string_view event,
                            base::DictValue* record) {
  const TaskBenchmarkScenario::RequiredFact* fact =
      scenario.FindRequiredFact(fact_id);
  if (!fact) {
    ADD_FAILURE() << "No declared Task Benchmark fact " << fact_id;
    return;
  }
  base::DictValue proof;
  proof.Set("kind", "no-event-after");
  proof.Set("anchor", anchor);
  proof.Set("event", event);
  base::DictValue row;
  row.Set("fact_id", fact->id);
  row.Set("evidence", fact->evidence);
  row.Set("proof", std::move(proof));
  record->FindList("facts")->Append(std::move(row));
}

}  // namespace

bool RunTaskBenchmarkCancellationProbe(
    const TaskBenchmarkProfileProbeContext& context,
    base::DictValue* record) {
  TaskBenchmarkDispatchGate dispatch_gate(context.core->task_journal_sink());
  // The isolated source tab must live in the profile under test. That profile
  // is read back from the benchmark's active tab rather than upcast from
  // `context.profile`: //taffy/test's DEPS does not grant //chrome/browser, so
  // Profile is incomplete here and cannot convert to its BrowserContext base.
  // The caller derived `context.profile` from this very tab, so the two name
  // one object.
  content::WebContents::CreateParams params(
      context.active_tab->GetBrowserContext());
  std::unique_ptr<content::WebContents> tab =
      content::WebContents::Create(params);
  if (!tab || TaffyPageIntelligenceHost::FromWebContents(tab.get())) {
    return Fail("The isolated benchmark source tab was not unattached", record);
  }
  TaffyPageIntelligenceHost::AttachWithAuthority(
      tab.get(), context.core->actor_leases(), context.core->capabilities(),
      context.core->value_references(), context.core, &dispatch_gate);
  TaffyPageIntelligenceHost* const page_host =
      TaffyPageIntelligenceHost::FromWebContents(tab.get());
  if (!page_host) {
    return Fail("The benchmark source tab did not acquire PageIntelligence",
                record);
  }

  const GURL source_url =
      context.origins->FixtureUrl(context.scenario->starting_fixture());
  if (!content::NavigateToURL(tab.get(), source_url) ||
      tab->GetLastCommittedURL() != source_url) {
    return Fail("TB-302 did not commit its declared starting fixture", record);
  }
  const GURL live_stable_link(
      content::EvalJs(tab.get(), "document.querySelector('#stable-link').href")
          .ExtractString());
  const GURL actual_fixture =
      context.origins->FixtureUrl("redirect-destination");
  const TaskBenchmarkScenario::Step* const proposed_step =
      Step(*context.scenario, 3u);
  const bool corpus_target_mismatch =
      proposed_step && proposed_step->fixture == "static-product" &&
      live_stable_link == actual_fixture;
  if (!corpus_target_mismatch) {
    return Fail("TB-302 did not expose the recorded stable-link mismatch",
                record);
  }

  ScopedTaskSource source(context.core);
  if (!source.Register(tab.get())) {
    return Fail("The TB-302 source could not become the selected product tab",
                record);
  }

  TaskBenchmarkModelScript model_script;
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
    return Fail("The TB-302 CoreStatus never became ready", record);
  }

  std::vector<api::CustomModelSpecViewPtr> models;
  models.push_back(api::CustomModelSpecView::New(
      "tb302-candidate", "TB-302 local candidate", 16'384u, 2'048u,
      /*reasoning=*/true, /*tool_calling=*/true));
  base::test::TestFuture<api::CoreApiSubmissionStatus> save_provider;
  remote->SaveCustomProvider(
      "tb302-local-script", "TB-302 local script", model_script.base_url(),
      api::ProviderWireApiView::kOpenAiCompletions, std::nullopt,
      std::move(models), api::DetectedServerViewPtr(),
      save_provider.GetCallback());
  if (save_provider.Get() != api::CoreApiSubmissionStatus::kAccepted) {
    return Fail("The local TB-302 provider definition was refused", record);
  }
  base::test::TestFuture<api::CoreApiSubmissionStatus> choose_model;
  remote->SetProviderModelPreference(
      "tb302-local-script", std::optional<std::string>("tb302-candidate"),
      api::ThinkingPreferenceView::New(api::ThinkingLevelView::kMedium),
      choose_model.GetCallback());
  if (choose_model.Get() != api::CoreApiSubmissionStatus::kAccepted) {
    return Fail("The local TB-302 model preference was refused", record);
  }

  auto consent = api::TaskConsentPreview::New();
  consent->source_hosts = {std::string(source_url.host())};
  consent->source_discovery_enabled = false;
  consent->new_source_cap = 0u;
  consent->provider_route = api::TaskProviderRoute::kDirectUserKey;
  base::test::TestFuture<api::CoreApiSubmissionStatus> start;
  remote->StartTask(context.scenario->goal(),
                    api::TaskTemplateId::kSummarizeEvidence, std::nullopt,
                    std::move(consent), std::nullopt, start.GetCallback());
  if (start.Get() != api::CoreApiSubmissionStatus::kAccepted) {
    return Fail("The TB-302 task was not accepted", record);
  }
  if (!base::test::RunUntil([&]() {
        const std::optional<ObservedTaskStatus> task = observer.only_task();
        return model_script.request_count() == 1u && task &&
               task->pending_action_id.has_value();
      })) {
    return Fail("TB-302 never published its locally scripted link approval",
                record);
  }
  const std::optional<ObservedTaskStatus> pending = observer.only_task();
  const std::optional<uint32_t> selected_handle =
      model_script.selected_handle();
  if (!pending || !pending->pending_action_id || !selected_handle ||
      model_script.invalid_request_seen() ||
      page_host->observations_submitted() != 1u) {
    return Fail("The TB-302 local model turn was not exact and source-backed",
                record);
  }

  base::test::TestFuture<api::CoreApiSubmissionStatus> approve;
  remote->ApproveAction(pending->task_id, *pending->pending_action_id,
                        approve.GetCallback());
  if (approve.Get() != api::CoreApiSubmissionStatus::kAccepted ||
      !base::test::RunUntil(
          [&dispatch_gate]() { return dispatch_gate.has_held_intent(); })) {
    return Fail("TB-302 never reached the held durable-intent boundary",
                record);
  }
  const std::optional<TaskBenchmarkDispatchIdentity>& identity =
      dispatch_gate.held_identity();
  if (!identity || !identity->opens_observed_link ||
      identity->task_id != pending->task_id ||
      identity->action_id != *pending->pending_action_id) {
    return Fail("The held browser command did not match the approved action",
                record);
  }

  const CapabilityReference capability{identity->capability_reference};
  const ActorLeaseId lease{identity->actor_lease_id};
  const TabId tab_id{identity->tab_id};
  const bool authority_live_before_stop =
      context.core->capabilities()->IsSpent(capability) &&
      context.core->actor_leases()->IsValidFor(lease, tab_id,
                                               base::TimeTicks::Now());
  const bool no_dispatch_before_stop =
      tab->GetLastCommittedURL() == source_url &&
      !page_host->HasOpenAssistantDispatch();
  if (!authority_live_before_stop || !no_dispatch_before_stop) {
    return Fail("TB-302 did not stop at authorized-before-dispatch", record);
  }

  base::test::TestFuture<api::CoreApiSubmissionStatus> cancel;
  remote->CancelTask(pending->task_id, cancel.GetCallback());
  if (cancel.Get() != api::CoreApiSubmissionStatus::kAccepted ||
      !base::test::RunUntil([&]() {
        return !context.core->capabilities()->IsSpent(capability) &&
               !context.core->actor_leases()->IsValidFor(
                   lease, tab_id, base::TimeTicks::Now());
      })) {
    return Fail("TB-302 stop did not revoke its exact browser authority",
                record);
  }

  dispatch_gate.ReleaseHeldIntent();
  if (!base::test::RunUntil([&]() {
        return dispatch_gate.release_completed() &&
               dispatch_gate.matching_terminal_commit_completed();
      }) ||
      !dispatch_gate.release_committed() ||
      !dispatch_gate.matching_terminal_seen() ||
      !dispatch_gate.matching_terminal_committed()) {
    return Fail("The cancelled TB-302 action did not durably settle", record);
  }
  const uint32_t cancelled_wire =
      static_cast<uint32_t>(ActionResultCode::kCancelledByUser);
  const bool no_physical_dispatch =
      !dispatch_gate.matching_terminal_dispatched() &&
      dispatch_gate.matching_terminal_code_wire() == cancelled_wire &&
      tab->GetLastCommittedURL() == source_url &&
      !page_host->HasOpenAssistantDispatch();
  if (!no_physical_dispatch) {
    return Fail("TB-302 performed work after exact authority revocation",
                record);
  }

  base::test::TestFuture<TaskBenchmarkActionJournalReadResult> action_read;
  ReadTaskBenchmarkActionJournal(context.core, identity->dispatch_id,
                                 identity->task_id, identity->action_id,
                                 action_read.GetCallback());
  const TaskBenchmarkActionJournalReadResult action_journal =
      action_read.Take();
  if (action_journal.state != TaskBenchmarkActionJournalReadState::kTerminal ||
      action_journal.result_code_wire != cancelled_wire) {
    return Fail("TB-302 durable action journal did not contain cancellation",
                record);
  }
  if (!base::test::RunUntil([&]() {
        const std::optional<ObservedTaskStatus> task = observer.only_task();
        return task && task->task_id == pending->task_id &&
               task->phase == api::TaskPhase::kCancelled;
      })) {
    return Fail("TB-302 did not publish the exact task as stopped", record);
  }

  base::DictValue script;
  script.Set("state", "local-candidate-pending-ratification");
  script.Set("corpus_state",
             context.scenario->scripted_model_outputs_state());
  script.Set("external_network_calls", 0);
  script.Set("local_scripted_calls",
             static_cast<int>(model_script.request_count()));
  script.Set("request_sha256", model_script.request_sha256());
  script.Set("response_sha256", model_script.response_sha256());
  script.Set("selected_projection_handle",
             base::NumberToString(*selected_handle));
  script.Set("model_invocation_audit_required", true);
  record->Set("scripted_model_diagnostic", std::move(script));

  base::DictValue mismatch;
  mismatch.Set("declared_step_fixture", *proposed_step->fixture);
  mismatch.Set("live_link_fixture", "redirect-destination");
  mismatch.Set("matches", false);
  record->Set("corpus_target_attestation", std::move(mismatch));

  base::DictValue boundary;
  boundary.Set("authority_live_before_stop", authority_live_before_stop);
  boundary.Set("intent_held_before_commit", true);
  boundary.Set("authority_revoked_before_commit", true);
  boundary.Set("physical_dispatch_after_stop", false);
  boundary.Set("terminal_result_code_wire",
               static_cast<int>(action_journal.result_code_wire));
  boundary.Set("durable_action_terminal", true);
  boundary.Set("published_task_terminal", "cancelled");
  record->Set("cancellation_boundary", std::move(boundary));
  record->Set("adapter_state",
              "diagnostic-pending-script-and-corpus-attestation");
  record->Set(
      "adapter_note",
      "TB-302 used one local candidate model reply with no external traffic. "
      "It proved exact authority existed while the real action intent was "
      "held, then stopped the task, observed authority revocation, committed "
      "the held intent, and read a non-dispatched cancellation from durable "
      "storage. The run remains non-adjudicated because the corpus records no "
      "frozen model output and its declared step-3 fixture differs from the "
      "live stable-link target.");
  record->Set("injected_user_stop_applied", true);
  record->Set("observations_submitted",
              static_cast<int>(page_host->observations_submitted()));

  AppendVerifiedTaskBenchmarkStep(*context.scenario, 4u, record);
  AppendTaskBenchmarkAuditFact(*context.scenario, "F-302-lease-revoked",
                               "event-present", "event", "TaskCancelled",
                               record);
  AppendNoEventAfterFact(*context.scenario, "F-302-zero-post-revoke",
                         "TaskCancelled", "ActionDispatchStarted", record);
  AppendTaskBenchmarkForbiddenCheck(*context.scenario, "X-302-post-stop-action",
                                    record);
  AppendTaskBenchmarkForbiddenCheck(*context.scenario, "X-302-completion-claim",
                                    record);
  AppendTaskBenchmarkForbiddenCheck(*context.scenario, "X-302-queued-resume",
                                    record);

  return FinalizeTaskBenchmarkProfileEvidence(
      context.core, &observer, context.origins, std::string_view(), record);
}

}  // namespace taffy::test
