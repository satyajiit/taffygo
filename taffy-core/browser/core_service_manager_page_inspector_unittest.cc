// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <memory>
#include <utility>

#include "base/containers/span.h"
#include "base/test/bind.h"
#include "base/time/time.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "crypto/sha2.h"
#include "taffy/browser/core_effect_broker.h"
#include "taffy/browser/core_page_observation_broker.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_task_effect_test_peer.h"
#include "taffy/browser/core_service_manager_test_support.h"
#include "taffy/browser/direct_observation_digest.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy {
namespace {

namespace api_mojom = core_api::mojom;

TEST(CoreServiceManagerPageInspectorTest,
     FirstDirectReadFromStoppedProfileStartsBootstrap) {
  content::BrowserTaskEnvironment task_environment;
  auto context = std::make_unique<content::TestBrowserContext>();
  std::unique_ptr<content::WebContents> web_contents =
      content::WebContents::Create(
          content::WebContents::CreateParams(context.get()));

  bool bootstrap_requested = false;
  CoreEffectBroker::BootstrapCallback pending_bootstrap;
  CoreEffectBroker::Handlers handlers;
  handlers.load_bootstrap = base::BindLambdaForTesting(
      [&](uint64_t generation, bool private_profile,
          CoreEffectBroker::BootstrapCallback callback) {
        EXPECT_EQ(1u, generation);
        EXPECT_FALSE(private_profile);
        bootstrap_requested = true;
        pending_bootstrap = std::move(callback);
      });

  auto tools = std::make_unique<ProfileToolSupervisor>(
      1u, ProfileToolSupervisor::PythonPorts::Unsupported(),
      ProfileToolSupervisor::LocalModelPorts::Unsupported(),
      ProfileToolSupervisor::MediaPorts::Unsupported());
  auto observation =
      base::MakeRefCounted<CorePageObservationBroker>(context.get());
  test::QuietManagerTail tail;
  auto manager = tail.MakeManager(
      context.get(), /*storage_broker=*/nullptr, std::move(tools), observation,
      std::make_unique<CoreEffectBroker>(std::move(handlers)));

  api_mojom::PageSnapshotExportAvailability export_terminal =
      api_mojom::PageSnapshotExportAvailability::kAvailable;
  manager->ExportPageSnapshot(
      /*web_contents=*/nullptr, "page-export-no-selection", "selected-page",
      api_mojom::PageSnapshotExportFormat::kMarkdown,
      base::BindLambdaForTesting(
          [&](api_mojom::PageSnapshotExportResultPtr result) {
            ASSERT_TRUE(result);
            export_terminal = result->availability;
            EXPECT_FALSE(result->snapshot_export);
          }));
  EXPECT_EQ(api_mojom::PageSnapshotExportAvailability::kNoSelectedPage,
            export_terminal);
  EXPECT_FALSE(manager->CancelPageSnapshotExport("unknown-export"));

  int completion_count = 0;
  api_mojom::PageInspectorAvailability terminal =
      api_mojom::PageInspectorAvailability::kInvalidResponse;
  manager->ObservePageForInspector(
      web_contents.get(),
      base::BindLambdaForTesting(
          [&](api_mojom::PageInspectorSnapshotResultPtr result) {
            ++completion_count;
            ASSERT_TRUE(result);
            terminal = result->availability;
          }));

  EXPECT_TRUE(bootstrap_requested);
  EXPECT_TRUE(pending_bootstrap);
  EXPECT_EQ(CoreServiceManager::Availability::kStarting,
            manager->availability());
  EXPECT_EQ(0, completion_count);

  // A profile shutdown is still exactly one terminal settlement for the
  // queued read; the important cold-start invariant is that the read was not
  // settled unavailable before the bootstrap attempt above.
  manager->Shutdown();
  EXPECT_EQ(1, completion_count);
  EXPECT_EQ(api_mojom::PageInspectorAvailability::kCoreUnavailable, terminal);
}

TEST(CoreServiceManagerPageInspectorTest,
     DirectPolicyAdmitsExactBinaryDigestAndRejectsChangedOrTruncatedBytes) {
  content::BrowserTaskEnvironment task_environment;
  content::TestBrowserContext context;
  bool bootstrap_requested = false;
  CoreEffectBroker::BootstrapCallback pending_bootstrap;
  CoreEffectBroker::Handlers handlers;
  handlers.load_bootstrap = base::BindLambdaForTesting(
      [&](uint64_t, bool, CoreEffectBroker::BootstrapCallback callback) {
        bootstrap_requested = true;
        pending_bootstrap = std::move(callback);
      });
  auto tools = std::make_unique<ProfileToolSupervisor>(
      1u, ProfileToolSupervisor::PythonPorts::Unsupported(),
      ProfileToolSupervisor::LocalModelPorts::Unsupported(),
      ProfileToolSupervisor::MediaPorts::Unsupported());
  test::QuietManagerTail tail;
  auto manager = tail.MakeManager(
      &context, nullptr, std::move(tools),
      base::MakeRefCounted<CorePageObservationBroker>(&context),
      std::make_unique<CoreEffectBroker>(std::move(handlers)));
  CoreServiceManagerTaskEffectTestPeer::BeginBrowserAuthorityGeneration(
      *manager, "profile-1");
  const auto now = base::TimeTicks::Now();
  const uint64_t millis = now.since_origin().InMilliseconds();
  const auto lease = manager->actor_leases()->IssueDirectObservation(
      "direct-intent-1", TabId{"tab-1"},
      service_mojom::kMaxDirectObservationLeaseMs, now);
  ASSERT_EQ(ActorLeaseResultCode::kIssued, lease.code);
  DirectObservationContext page{.tab_id = "tab-1",
                                .frame_id = "frame-1",
                                .page_epoch = "epoch-1",
                                .origin = "https://example.test",
                                .host = "example.test",
                                .graph_revision = 0u};
  auto request = service_mojom::PolicyEvaluationRequest::New();
  request->operation = service_mojom::OperationEnvelope::New(
      "direct-observation-1", 1u, 0u,
      millis + service_mojom::kMaxDirectObservationDeadlineMs,
      "direct-observation-1");
  request->now_monotonic_ms = millis;
  request->now_utc_ms = base::Time::Now().InMillisecondsSinceUnixEpoch();
  request->principal = service_mojom::PolicyPrincipal::New(
      service_mojom::PolicyPrincipalKind::kAssistant, std::nullopt);
  request->action_class = service_mojom::PolicyActionClass::kObservePage;
  request->operation_kind = service_mojom::TaskActionOperationKind::kDomRead;
  request->proposal_digest = ComputeDirectObservationDigest(
      page, "profile-1", "direct-intent-1", "direct-observation-1",
      "direct-observation-1", 1u, FixedDirectObservationDigestOperands());
  const auto digest =
      crypto::SHA256Hash(base::as_byte_span(request->proposal_digest));
  ASSERT_TRUE(std::any_of(digest.begin(), digest.end(),
                          [](uint8_t byte) { return byte >= 0x80u; }));
  request->canonical_intent_digest.assign(digest.begin(), digest.end());
  request->scope = service_mojom::PolicyCapabilityScope::New();
  request->scope->profile_id = "profile-1";
  request->scope->tab_id = page.tab_id;
  request->scope->frame_id = page.frame_id;
  request->scope->page_epoch = page.page_epoch;
  request->scope->origin = service_mojom::PolicyOrigin::New(
      service_mojom::PolicyOriginKind::kTuple, page.origin, std::nullopt);
  request->data_classes = {service_mojom::BipSensitivity::kNotSensitive};
  request->context_risk = service_mojom::PolicyRiskClass::kLocalRead;
  request->expires_at_monotonic_ms = std::min(
      lease.expires_at_monotonic_ms, request->operation->deadline_monotonic_ms);
  request->authority_subject = service_mojom::AuthoritySubject::New(
      service_mojom::AuthoritySubjectKind::kDirectUserIntent, "direct-intent-1");
  request->actor_lease = service_mojom::ActorLeaseFact::New();
  request->actor_lease->lease_id = lease.lease_id.value;
  request->actor_lease->service_generation = 1u;
  request->actor_lease->profile_id = "profile-1";
  request->actor_lease->tab_id = page.tab_id;
  request->actor_lease->control_mode = service_mojom::TaskControlMode::kUser;
  request->actor_lease->expires_at_monotonic_ms = lease.expires_at_monotonic_ms;
  request->actor_lease->authority_subject = request->authority_subject.Clone();
  request->context =
      service_mojom::PolicyEvaluationContext::kDirectUserObservation;
  request->policy_version = 1u;
  for (int mutation = 0; mutation < 3; ++mutation) {
    auto invalid = request.Clone();
    if (mutation == 0) {
      invalid->canonical_intent_digest.front() ^= 0x80u;
    } else if (mutation == 1) {
      invalid->canonical_intent_digest.pop_back();
    } else {
      invalid->canonical_intent_digest.push_back(0u);
    }
    int callbacks = 0;
    manager->EvaluatePolicy(std::move(invalid), base::BindLambdaForTesting(
        [&](service_mojom::PolicyEvaluationResultPtr result) {
          ++callbacks;
          ASSERT_TRUE(result);
          EXPECT_EQ(service_mojom::PolicyEvaluationStatus::kInvalidRequest,
                    result->status);
          EXPECT_FALSE(result->minted_grant);
        }));
    EXPECT_EQ(1, callbacks);
    EXPECT_FALSE(bootstrap_requested);
  }
  int callbacks = 0;
  manager->EvaluatePolicy(std::move(request), base::BindLambdaForTesting(
      [&](service_mojom::PolicyEvaluationResultPtr result) {
        ++callbacks;
        ASSERT_TRUE(result);
        EXPECT_EQ(service_mojom::PolicyEvaluationStatus::kCoreUnavailable,
                  result->status);
      }));
  // Matching bytes enter the real pending-policy/bootstrap path, without a
  // fabricated grant or renderer response. Shutdown settles that request once.
  EXPECT_TRUE(bootstrap_requested);
  EXPECT_TRUE(pending_bootstrap);
  EXPECT_EQ(0, callbacks);
  manager->Shutdown();
  EXPECT_EQ(1, callbacks);
}

}  // namespace
}  // namespace taffy
