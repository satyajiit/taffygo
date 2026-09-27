// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/test/base/chrome_test_utils.h"
#include "chrome/test/base/platform_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "taffy/browser/core_api/profile_core_api_facade.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_factory.h"
#include "taffy/browser/core_service_manager_task_effect_test_peer.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/test/recovery/comparison_vertical_dispatch_gate.h"
#include "taffy/test/recovery/comparison_vertical_model_endpoint.h"
#include "taffy/test/recovery/core_api_status_observer.h"
#include "taffy/test/recovery/empty_vault_profile_platform_adapter.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace api = core_api::mojom;

std::unique_ptr<net::test_server::HttpResponse> ServeComparison(
    const net::test_server::HttpRequest& request) {
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/html");
  if (request.relative_url == "/orchard") {
    response->set_content(
        "<!doctype html><title>Cedar Phone at Orchard</title><main>"
        "<h1>Cedar Phone at Orchard</h1><p>Price: $349.00</p>"
        "<p>Factory unlocked. Storage: 128 GB.</p></main>");
  } else if (request.relative_url == "/harbor") {
    response->set_content(
        "<!doctype html><title>Cedar Phone at Harbor</title><main>"
        "<h1>Cedar Phone at Harbor</h1><p>Price: $329.00</p>"
        "<p>Factory unlocked. Storage: 128 GB.</p></main>");
  } else {
    return nullptr;
  }
  return response;
}

void ExpectCitedComparison(const test::ObservedWorkspaceStatus& workspace) {
  ASSERT_EQ(workspace.sources.size(), 2u);
  std::map<std::string, std::string> hosts_by_source;
  for (const auto& source : workspace.sources) {
    EXPECT_FALSE(source.source_id.empty());
    EXPECT_TRUE(hosts_by_source.emplace(source.source_id, source.host).second);
    EXPECT_GT(source.fact_count, 0u);
    EXPECT_FALSE(source.excluded);
  }
  std::set<std::string> hosts;
  for (const auto& [id, host] : hosts_by_source) {
    hosts.insert(host);
  }
  EXPECT_EQ(hosts, (std::set<std::string>{"a.test", "b.test"}));
  ASSERT_FALSE(workspace.facts.empty());
  std::set<std::string> priced_hosts;
  for (const auto& fact : workspace.facts) {
    EXPECT_FALSE(fact.fact_id.empty());
    EXPECT_EQ(fact.kind, api::WorkspaceFactKind::kFromPage);
    ASSERT_FALSE(fact.sources.empty());
    for (const auto& citation : fact.sources) {
      const auto source = hosts_by_source.find(citation);
      ASSERT_NE(source, hosts_by_source.end());
      if (fact.value.find("$349.00") != std::string::npos) {
        EXPECT_EQ(source->second, "a.test");
        priced_hosts.insert(source->second);
      }
      if (fact.value.find("$329.00") != std::string::npos) {
        EXPECT_EQ(source->second, "b.test");
        priced_hosts.insert(source->second);
      }
    }
  }
  EXPECT_EQ(priced_hosts, hosts);
}

class ComparisonVerticalBrowserTest : public PlatformBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    PlatformBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    server_.SetSSLConfig(net::EmbeddedTestServer::CERT_TEST_NAMES);
    server_.RegisterRequestHandler(base::BindRepeating(&ServeComparison));
    ASSERT_TRUE(server_.Start());
  }

  net::EmbeddedTestServer server_{net::EmbeddedTestServer::TYPE_HTTPS};
  test::EmptyVaultProfilePlatformAdapter platform_adapter_;
};

IN_PROC_BROWSER_TEST_F(ComparisonVerticalBrowserTest,
                       TwoTabsOverlapBeforeOneGroundedComparison) {
  content::WebContents* const first =
      chrome_test_utils::GetActiveWebContents(this);
  ASSERT_TRUE(first);
  auto* profile = Profile::FromBrowserContext(first->GetBrowserContext());
  auto* core = CoreServiceManagerFactory::GetForProfile(profile);
  ASSERT_TRUE(core);
  core->BindPlatformAdapter(platform_adapter_.BindNewPipeAndPassRemote());
  base::test::TestFuture<bool> prepared;
  core->PrepareForCoreApi(prepared.GetCallback());
  ASSERT_TRUE(prepared.Get());

  // The second source is a real renderer in the same Chrome Profile. Attach
  // the shipping page host; do not replace its broker, authority, or results.
  auto second =
      content::WebContents::Create(content::WebContents::CreateParams(profile));
  ASSERT_TRUE(second);
  TaffyPageIntelligenceHost::AttachIfEligible(second.get());
  ASSERT_TRUE(
      content::NavigateToURL(first, server_.GetURL("a.test", "/orchard")));
  ASSERT_TRUE(content::NavigateToURL(second.get(),
                                     server_.GetURL("b.test", "/harbor")));
  auto* first_host = TaffyPageIntelligenceHost::FromWebContents(first);
  auto* second_host = TaffyPageIntelligenceHost::FromWebContents(second.get());
  ASSERT_TRUE(first_host);
  ASSERT_TRUE(second_host);
  const auto first_document = first_host->BuildDirectObservationContext();
  const auto second_document = second_host->BuildDirectObservationContext();
  ASSERT_TRUE(first_document);
  ASSERT_TRUE(second_document);
  ASSERT_NE(first_document->tab_id, second_document->tab_id);
  const auto first_reads_before = first_host->observations_submitted();
  const auto second_reads_before = second_host->observations_submitted();

  const uint64_t window = core->RegisterTaskSourceWindow();
  ASSERT_NE(window, 0u);
  base::ScopedClosureRunner release_sources(base::BindOnce(
      [](CoreServiceManager* manager, uint64_t token) {
        manager->DeactivateTaskSourceWindow(token);
        manager->UnregisterTaskSourceWindow(token);
      },
      core, window));
  ASSERT_TRUE(core->RegisterTaskSourceTab(window, 81, first));
  ASSERT_TRUE(core->RegisterTaskSourceTab(window, 82, second.get()));
  ASSERT_TRUE(core->SelectTaskSourceTab(window, 81, first));
  ASSERT_TRUE(core->ActivateTaskSourceWindow(window));

  ProfileCoreApiFacade facade(core);
  mojo::Remote<api::TaffyProfileCoreApi> remote;
  mojo::Receiver<api::TaffyProfileCoreApi> receiver(
      &facade, remote.BindNewPipeAndPassReceiver());
  test::CoreApiStatusObserver observer;
  remote->Observe(observer.BindNewPipeAndPassRemote());
  ASSERT_TRUE(base::test::RunUntil([&] { return observer.snapshot_seen(); }));
  test::ComparisonVerticalModelEndpoint endpoint;
  std::vector<api::CustomModelSpecViewPtr> models;
  models.push_back(api::CustomModelSpecView::New("comparison-model",
                                                 "Comparison test model",
                                                 16'384u, 2'048u, true, true));
  base::test::TestFuture<api::CoreApiSubmissionStatus> provider;
  remote->SaveCustomProvider(
      "comparison-test", "Comparison test provider", endpoint.base_url(),
      api::ProviderWireApiView::kOpenAiCompletions, std::nullopt,
      std::move(models), api::DetectedServerViewPtr(), provider.GetCallback());
  ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, provider.Get());
  base::test::TestFuture<api::CoreApiSubmissionStatus> choose;
  remote->SetProviderModelPreference(
      "comparison-test", std::optional<std::string>("comparison-model"),
      api::ThinkingPreferenceView::New(api::ThinkingLevelView::kMedium),
      choose.GetCallback());
  ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, choose.Get());
  auto* broker = CoreServiceManagerTaskEffectTestPeer::GetEffectBroker(*core);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !broker->HasPendingEffects(); }));
  ComparisonVerticalDispatchGate gate(broker);

  auto consent = api::TaskConsentPreview::New();
  consent->source_hosts = {"a.test", "b.test"};
  consent->source_discovery_enabled = false;
  consent->new_source_cap = 0u;
  consent->provider_route = api::TaskProviderRoute::kDirectUserKey;
  base::test::TestFuture<api::CoreApiSubmissionStatus> start;
  remote->StartTask(
      "Compare these two phone offers and identify the lower price",
      api::TaskTemplateId::kCompareProducts, std::nullopt, std::move(consent),
      std::nullopt, start.GetCallback());
  ASSERT_EQ(api::CoreApiSubmissionStatus::kAccepted, start.Get());
  const auto checkpoint = [&] {
    const auto task = observer.only_task();
    const auto workspace = observer.only_workspace();
    testing::Message details;
    details << "held=" << gate.held_count()
            << ", unexpected=" << gate.unexpected_intent_seen()
            << ", intent_refusal=" << gate.refusal_reason()
            << ", model_intents=" << gate.model_intents_seen()
            << ", model_requests=" << endpoint.request_count()
            << ", commit_callbacks=" << gate.commit_callbacks_seen()
            << ", committed=" << gate.committed_count()
            << ", both_pages=" << endpoint.both_pages_seen()
            << ", invalid_model_request=" << endpoint.invalid_request_seen()
            << ", availability=" << static_cast<int>(observer.availability())
            << ", malformed=" << observer.malformed_payload_seen()
            << ", task=" << task.has_value()
            << ", phase=" << (task ? static_cast<int>(task->phase) : -1)
            << ", failure="
            << (task && task->failure_code
                    ? static_cast<int>(*task->failure_code) : -1)
            << ", revision=" << (task ? task->revision : 0u)
            << ", pending_approval=" << (task && task->pending_action_id)
            << ", handover=" << (task && task->waiting_for_handover)
            << ", first_reads=" << first_host->observations_submitted()
            << ", second_reads=" << second_host->observations_submitted()
            << ", sources=" << (workspace ? workspace->sources.size() : 0u)
            << ", facts=" << (workspace ? workspace->facts.size() : 0u);
    return details.GetString();
  };
  ASSERT_TRUE(base::test::RunUntil([&] {
    const auto task = observer.only_task();
    return observer.availability() != api::CoreAvailability::kReady ||
           observer.malformed_payload_seen() || gate.held_count() == 2u ||
           gate.unexpected_intent_seen() ||
           (task && (task->pending_action_id ||
                     task->phase == api::TaskPhase::kFailed));
  })) << checkpoint();
  ASSERT_EQ(api::CoreAvailability::kReady, observer.availability())
      << checkpoint();
  ASSERT_EQ(gate.held_count(), 2u) << checkpoint();
  ASSERT_FALSE(gate.unexpected_intent_seen()) << checkpoint();
  ASSERT_EQ(gate.identities().size(), 2u);
  const auto running = observer.only_task();
  ASSERT_TRUE(running);
  std::set<std::string> admitted_tabs;
  std::set<std::string> actions;
  std::set<std::string> effects;
  for (const auto& intent : gate.identities()) {
    EXPECT_EQ(intent.task_id, running->task_id);
    EXPECT_EQ(intent.generation, core->service_generation());
    EXPECT_FALSE(intent.action_id.empty());
    EXPECT_TRUE(actions.insert(intent.action_id).second);
    EXPECT_TRUE(effects.insert(intent.effect_id).second);
    admitted_tabs.insert(intent.tab_id);
  }
  EXPECT_EQ(admitted_tabs, (std::set<std::string>{first_document->tab_id,
                                                  second_document->tab_id}));
  EXPECT_EQ(gate.model_intents_seen(), 0u);
  EXPECT_EQ(endpoint.request_count(), 0u);
  EXPECT_EQ(gate.commit_callbacks_seen(), 0u);
  EXPECT_EQ(first_host->observations_submitted(), first_reads_before);
  EXPECT_EQ(second_host->observations_submitted(), second_reads_before);

  gate.ReleaseHeldIntents();
  ASSERT_TRUE(base::test::RunUntil([&] {
    const auto task = observer.only_task();
    return observer.availability() != api::CoreAvailability::kReady ||
           observer.malformed_payload_seen() ||
           (task && (task->phase == api::TaskPhase::kCompleted ||
                     task->phase == api::TaskPhase::kPartial ||
                     task->phase == api::TaskPhase::kFailed ||
                     task->phase == api::TaskPhase::kCancelled ||
                     task->phase == api::TaskPhase::kOutcomeUnknown ||
                     task->waiting_for_handover || task->pending_action_id));
  })) << checkpoint();
  ASSERT_EQ(api::CoreAvailability::kReady, observer.availability())
      << checkpoint();
  const auto completed =
      observer.only_task_in_phase(api::TaskPhase::kCompleted);
  ASSERT_TRUE(completed) << checkpoint();
  EXPECT_EQ(gate.commit_callbacks_seen(), 2u);
  EXPECT_EQ(gate.committed_count(), 2u);
  EXPECT_EQ(gate.model_intents_seen(), 1u);
  EXPECT_EQ(endpoint.request_count(), 1u);
  EXPECT_TRUE(endpoint.both_pages_seen());
  EXPECT_FALSE(endpoint.invalid_request_seen());
  EXPECT_FALSE(gate.unexpected_intent_seen());
  EXPECT_GT(first_host->observations_submitted(), first_reads_before);
  EXPECT_GT(second_host->observations_submitted(), second_reads_before);
  const auto workspace = observer.only_workspace();
  ASSERT_TRUE(workspace);
  EXPECT_EQ(completed->workspace_id, workspace->workspace_id);
  EXPECT_EQ(workspace->template_id, api::TaskTemplateId::kCompareProducts);
  ExpectCitedComparison(*workspace);
  EXPECT_FALSE(observer.malformed_payload_seen());
  EXPECT_FALSE(observer.permission_request_seen());
}

}  // namespace
}  // namespace taffy
