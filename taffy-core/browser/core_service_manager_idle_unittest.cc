// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memory>
#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/memory/scoped_refptr.h"
#include "base/process/process.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "content/public/browser/service_process_info.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/test_browser_context.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/web_contents_tester.h"
#include "taffy/browser/core_effect_broker.h"
#include "taffy/browser/core_page_observation_broker.h"
#include "taffy/browser/core_service_manager.h"
#include "taffy/browser/core_service_manager_test_support.h"
#include "taffy/browser/taffy_page_intelligence_host.h"
#include "taffy/components/storage/browser/core_storage_broker.h"
#include "taffy/services/tool-runtime/supervisor/profile_tool_supervisor.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace taffy {

namespace service_mojom = core_service::mojom;

// Drives the manager through real state transitions, so the idle deadline is
// maintained by the code under test rather than by the test.
class CoreServiceManagerIdleTestPeer final {
 public:
  static void MakeReady(CoreServiceManager& manager) {
    manager.disconnect_handled_ = false;
    std::ignore = manager.service_.remote().BindNewPipeAndPassReceiver();
    manager.SetAvailability(CoreServiceManager::Availability::kReady);
  }

  static bool IsIdleDeadlineArmed(const CoreServiceManager& manager) {
    return manager.idle_teardown_timer_.IsRunning();
  }

  static bool IsBound(const CoreServiceManager& manager) {
    return manager.service_.remote().is_bound();
  }

  static void StartEntitlementCadence(CoreServiceManager& manager) {
    manager.entitlement_poke_timer_.Start(FROM_HERE, base::Seconds(1),
                                          base::BindRepeating([] {}));
  }

  static bool IsEntitlementCadenceRunning(const CoreServiceManager& manager) {
    return manager.entitlement_poke_timer_.IsRunning();
  }

  static void SetHostTaskEffectWork(CoreServiceManager& manager,
                                    bool in_flight) {
    if (in_flight) {
      manager.pending_host_task_effect_ids_.insert("task-effect-idle-test");
    } else {
      manager.pending_host_task_effect_ids_.clear();
    }
    manager.RefreshIdleTeardown();
  }

  static void SetAnswerStreamWork(CoreServiceManager& manager, bool in_flight) {
    manager.active_task_answer_count_ = in_flight ? 1u : 0u;
    manager.RefreshIdleTeardown();
  }

  static void ObserveProcessLaunch(CoreServiceManager& manager,
                                   const content::ServiceProcessInfo& info) {
    manager.OnServiceLaunched(info);
  }

  static void BeginReplacementLaunch(CoreServiceManager& manager,
                                     base::OnceCallback<void(bool)> prepared) {
    manager.disconnect_handled_ = false;
    manager.launch_in_progress_ = true;
    std::ignore = manager.service_.remote().BindNewPipeAndPassReceiver();
    manager.pending_core_api_preparations_.push_back(std::move(prepared));
    manager.SetAvailability(CoreServiceManager::Availability::kStarting);
  }

  static void ObserveNormalTermination(
      CoreServiceManager& manager,
      const content::ServiceProcessInfo& info) {
    manager.OnServiceTerminatedNormally(info);
  }

  static void ObserveMojoDisconnect(CoreServiceManager& manager,
                                    uint64_t generation) {
    manager.OnMojoDisconnect(generation);
  }
};

class TaffyPageIntelligenceHostTestPeer final {
 public:
  static bool ReplaceObservedLinks(TaffyPageIntelligenceHost& host,
                                   const ObservationEnvelope& observation) {
    return host.observed_links_.Replace(observation);
  }
};

namespace {

constexpr uint64_t kGeneration = 1u;

content::ServiceProcessInfo CoreProcessInfo(uint64_t id) {
  return content::ServiceProcessInfo(
      service_mojom::TaffyCoreService::Name_, /*site=*/std::nullopt,
      content::ServiceProcessId::FromUnsafeValue(id),
      base::Process::Current().Duplicate());
}

std::unique_ptr<CoreServiceManager> MakeManager(
    test::QuietManagerTail& tail,
    content::TestBrowserContext* context) {
  auto tools = std::make_unique<ProfileToolSupervisor>(
      kGeneration, ProfileToolSupervisor::PythonPorts::Unsupported(),
      ProfileToolSupervisor::LocalModelPorts::Unsupported(),
      ProfileToolSupervisor::MediaPorts::Unsupported());
  return tail.MakeManager(
      context, /*storage_broker=*/nullptr, std::move(tools),
      base::MakeRefCounted<CorePageObservationBroker>(context),
      std::make_unique<CoreEffectBroker>(CoreEffectBroker::Handlers{}));
}

class SilentObserver final : public CoreServiceManager::Observer {};

class CoreServiceManagerIdleTest : public testing::Test {
 protected:
  CoreServiceManagerIdleTest()
      : task_environment_(base::test::TaskEnvironment::TimeSource::MOCK_TIME),
        browser_context_(std::make_unique<content::TestBrowserContext>()),
        manager_(MakeManager(tail_, browser_context_.get())) {}

  // One tick past the point where a resting profile must have been released.
  void FastForwardPastTheIdleDelay() {
    task_environment_.FastForwardBy(CoreServiceManager::kIdleTeardownDelay +
                                    base::Milliseconds(1));
  }

  content::BrowserTaskEnvironment task_environment_;
  content::RenderViewHostTestEnabler render_view_host_test_enabler_;
  // Before the manager: the filtering service the tail builds watches the
  // tail's pref service for the manager's whole lifetime.
  test::QuietManagerTail tail_;
  std::unique_ptr<content::TestBrowserContext> browser_context_;
  std::unique_ptr<CoreServiceManager> manager_;
};

TEST_F(CoreServiceManagerIdleTest,
       OneDeadlineRunsOnlyWhileTheServiceIsReadyAndQuiescent) {
  EXPECT_FALSE(CoreServiceManagerIdleTestPeer::IsIdleDeadlineArmed(*manager_));

  CoreServiceManagerIdleTestPeer::MakeReady(*manager_);
  EXPECT_TRUE(CoreServiceManagerIdleTestPeer::IsIdleDeadlineArmed(*manager_));

  FastForwardPastTheIdleDelay();
  EXPECT_FALSE(CoreServiceManagerIdleTestPeer::IsIdleDeadlineArmed(*manager_));
}

TEST_F(CoreServiceManagerIdleTest, ShutdownStopsTheEntitlementCadence) {
  CoreServiceManagerIdleTestPeer::StartEntitlementCadence(*manager_);
  ASSERT_TRUE(
      CoreServiceManagerIdleTestPeer::IsEntitlementCadenceRunning(*manager_));

  manager_->Shutdown();

  EXPECT_FALSE(
      CoreServiceManagerIdleTestPeer::IsEntitlementCadenceRunning(*manager_));
}

TEST_F(CoreServiceManagerIdleTest, IdleReleaseStopsTheEntitlementCadence) {
  CoreServiceManagerIdleTestPeer::MakeReady(*manager_);
  CoreServiceManagerIdleTestPeer::StartEntitlementCadence(*manager_);

  FastForwardPastTheIdleDelay();

  EXPECT_FALSE(
      CoreServiceManagerIdleTestPeer::IsEntitlementCadenceRunning(*manager_));
}

TEST_F(CoreServiceManagerIdleTest, ARestingProfileReleasesItsProcess) {
  CoreServiceManagerIdleTestPeer::MakeReady(*manager_);
  ASSERT_TRUE(CoreServiceManagerIdleTestPeer::IsBound(*manager_));

  FastForwardPastTheIdleDelay();

  EXPECT_FALSE(CoreServiceManagerIdleTestPeer::IsBound(*manager_));
  // kStopped, not kUnavailable or kCircuitOpen: a deliberate release is not a
  // crash and must not spend a restart from the recovery budget.
  EXPECT_EQ(CoreServiceManager::Availability::kStopped,
            manager_->availability());
}

TEST_F(CoreServiceManagerIdleTest, TheProcessIsHeldUntilTheDelayHasElapsed) {
  CoreServiceManagerIdleTestPeer::MakeReady(*manager_);

  task_environment_.FastForwardBy(CoreServiceManager::kIdleTeardownDelay -
                                  base::Milliseconds(1));
  EXPECT_EQ(CoreServiceManager::Availability::kReady, manager_->availability());

  FastForwardPastTheIdleDelay();
  EXPECT_EQ(CoreServiceManager::Availability::kStopped,
            manager_->availability());
}

TEST_F(CoreServiceManagerIdleTest, AWatchedProfileIsNeverReleased) {
  SilentObserver observer;
  manager_->AddObserver(&observer);
  CoreServiceManagerIdleTestPeer::MakeReady(*manager_);

  task_environment_.FastForwardBy(10 * CoreServiceManager::kIdleTeardownDelay);

  EXPECT_EQ(CoreServiceManager::Availability::kReady, manager_->availability());
  EXPECT_TRUE(CoreServiceManagerIdleTestPeer::IsBound(*manager_));
  manager_->RemoveObserver(&observer);
}

TEST_F(CoreServiceManagerIdleTest, AnOpenWindowHoldsTheProfile) {
  CoreServiceManagerIdleTestPeer::MakeReady(*manager_);
  const uint64_t window = manager_->RegisterTaskSourceWindow();

  task_environment_.FastForwardBy(10 * CoreServiceManager::kIdleTeardownDelay);
  EXPECT_EQ(CoreServiceManager::Availability::kReady, manager_->availability());

  manager_->UnregisterTaskSourceWindow(window);
  FastForwardPastTheIdleDelay();
  EXPECT_EQ(CoreServiceManager::Availability::kStopped,
            manager_->availability());
}

TEST_F(CoreServiceManagerIdleTest, ActivityRestartsTheIdleClock) {
  CoreServiceManagerIdleTestPeer::MakeReady(*manager_);

  // Rest almost long enough, then let a surface attach and detach again.
  task_environment_.FastForwardBy(CoreServiceManager::kIdleTeardownDelay -
                                  base::Milliseconds(1));
  SilentObserver observer;
  manager_->AddObserver(&observer);
  task_environment_.FastForwardBy(base::Seconds(1));
  manager_->RemoveObserver(&observer);

  // The clock restarted with the detach, so the old near-expiry buys nothing.
  task_environment_.FastForwardBy(CoreServiceManager::kIdleTeardownDelay -
                                  base::Milliseconds(1));
  EXPECT_EQ(CoreServiceManager::Availability::kReady, manager_->availability());

  FastForwardPastTheIdleDelay();
  EXPECT_EQ(CoreServiceManager::Availability::kStopped,
            manager_->availability());
}

// The rearm is the whole subject here, and it used to be measured against a
// catalog fetch — the one condition of the seven that left with the served
// catalog. Its sibling named the same property and could not tell it from a
// resumed deadline: both end stopped after a full delay, so only the
// minus-one-millisecond pair below distinguishes "a fresh deadline" from
// "the remainder of the old one". The measurement moves onto a condition
// that stays rather than leaving with its subject.
TEST_F(CoreServiceManagerIdleTest,
       AHostTaskEffectStopsAndThenRearmsAFullDeadline) {
  CoreServiceManagerIdleTestPeer::MakeReady(*manager_);
  task_environment_.FastForwardBy(CoreServiceManager::kIdleTeardownDelay -
                                  base::Milliseconds(1));

  CoreServiceManagerIdleTestPeer::SetHostTaskEffectWork(*manager_, true);
  EXPECT_FALSE(CoreServiceManagerIdleTestPeer::IsIdleDeadlineArmed(*manager_));
  task_environment_.FastForwardBy(10 * CoreServiceManager::kIdleTeardownDelay);
  EXPECT_EQ(CoreServiceManager::Availability::kReady, manager_->availability());

  CoreServiceManagerIdleTestPeer::SetHostTaskEffectWork(*manager_, false);
  EXPECT_TRUE(CoreServiceManagerIdleTestPeer::IsIdleDeadlineArmed(*manager_));
  task_environment_.FastForwardBy(CoreServiceManager::kIdleTeardownDelay -
                                  base::Milliseconds(1));
  EXPECT_EQ(CoreServiceManager::Availability::kReady, manager_->availability());
  task_environment_.FastForwardBy(base::Milliseconds(1));
  EXPECT_EQ(CoreServiceManager::Availability::kStopped,
            manager_->availability());
}

TEST_F(CoreServiceManagerIdleTest, AStreamingAnswerHoldsTheCoreUntilTerminal) {
  CoreServiceManagerIdleTestPeer::MakeReady(*manager_);
  CoreServiceManagerIdleTestPeer::SetAnswerStreamWork(*manager_, true);

  task_environment_.FastForwardBy(10 * CoreServiceManager::kIdleTeardownDelay);
  EXPECT_EQ(CoreServiceManager::Availability::kReady, manager_->availability());

  CoreServiceManagerIdleTestPeer::SetAnswerStreamWork(*manager_, false);
  task_environment_.FastForwardBy(CoreServiceManager::kIdleTeardownDelay);
  EXPECT_EQ(CoreServiceManager::Availability::kStopped,
            manager_->availability());
}

TEST_F(CoreServiceManagerIdleTest,
       CoreGenerationRestartRevokesBrowserOnlyObservedLinks) {
  ActorLeaseRegistry leases;
  CapabilityLedger capabilities;
  ValueReferenceVault values;
  auto page = content::WebContentsTester::CreateTestWebContents(
      browser_context_.get(), nullptr);
  content::WebContentsTester::For(page.get())
      ->NavigateAndCommit(GURL("https://source.example/page"));
  TaffyPageIntelligenceHost::AttachWithAuthority(page.get(), &leases,
                                                 &capabilities, &values);
  TaffyPageIntelligenceHost* host =
      TaffyPageIntelligenceHost::FromWebContents(page.get());
  ASSERT_TRUE(host);
  const std::optional<DirectObservationContext> live =
      host->BuildDirectObservationContext();
  ASSERT_TRUE(live);

  ObservationEnvelope observation;
  observation.code = ObservationResultCode::kOk;
  observation.tab_id = TabId{live->tab_id};
  observation.root_frame_id = FrameId{live->frame_id};
  observation.page_epoch = PageEpoch{live->page_epoch};
  observation.graph_revision = live->graph_revision;
  observation.lifecycle_state = DocumentLifecycleState::kActive;
  observation.origin.kind = OriginKind::kTuple;
  observation.origin.serialization = live->origin;
  observation.node_count = 1u;
  observation.encoding = GraphPayloadEncoding::kBipContract;
  observation.transient_observed_links.push_back(TransientObservedLink{
      .node_id = SemanticNodeId{"node-link"},
      .normalized_destination = "https://destination.example/exact?q=1",
  });
  ASSERT_TRUE(TaffyPageIntelligenceHostTestPeer::ReplaceObservedLinks(
      *host, observation));

  CanonicalLinkOpenHandle handle{
      .tab_id = live->tab_id,
      .frame_id = live->frame_id,
      .page_epoch = live->page_epoch,
      .graph_revision = live->graph_revision,
      .node_id = "node-link",
      .expected_origin_is_opaque = false,
      .expected_origin = live->origin,
  };
  ASSERT_TRUE(ResolveTaskObservedLink(browser_context_.get(), handle));
  observation.node_count = 2u;
  observation.transient_observed_links.push_back(TransientObservedLink{
      .node_id = SemanticNodeId{"node-download"},
      .normalized_destination = "https://destination.example/file?q=1",
      .opens_new_tab = true,
      .is_download = true,
  });
  ASSERT_TRUE(TaffyPageIntelligenceHostTestPeer::ReplaceObservedLinks(
      *host, observation));
  auto download = handle;
  download.node_id = "node-download";
  EXPECT_FALSE(ResolveTaskObservedLink(browser_context_.get(), download));
  EXPECT_EQ(ResolveTaskObservedDownload(browser_context_.get(), download),
            "https://destination.example/file?q=1");
  content::TestBrowserContext other_profile;
  EXPECT_FALSE(ResolveTaskObservedDownload(&other_profile, download));

  CoreServiceManagerIdleTestPeer::MakeReady(*manager_);
  const uint64_t lost_generation = manager_->service_generation();
  manager_->TearDownForIdle();

  EXPECT_EQ(manager_->service_generation(), lost_generation + 1u);
  EXPECT_FALSE(ResolveTaskObservedLink(browser_context_.get(), handle));
  EXPECT_FALSE(ResolveTaskObservedDownload(browser_context_.get(), download));
}

TEST_F(CoreServiceManagerIdleTest,
       LateReleasedProcessTerminalCannotTearDownItsReplacement) {
  CoreServiceManagerIdleTestPeer::MakeReady(*manager_);
  content::ServiceProcessInfo first_process = CoreProcessInfo(1u);
  CoreServiceManagerIdleTestPeer::ObserveProcessLaunch(*manager_,
                                                       first_process);
  const uint64_t first_generation = manager_->service_generation();

  manager_->TearDownForIdle();
  ASSERT_EQ(first_generation + 1u, manager_->service_generation());

  int preparation_count = 0;
  bool preparation_result = true;
  CoreServiceManagerIdleTestPeer::BeginReplacementLaunch(
      *manager_, base::BindOnce(
                     [](int* count, bool* result, bool ready) {
                       ++*count;
                       *result = ready;
                     },
                     &preparation_count, &preparation_result));
  ASSERT_EQ(CoreServiceManager::Availability::kStarting,
            manager_->availability());

  // Android may deliver this after the replacement launch has begun but
  // before that new process has reported its own ServiceProcessId.
  CoreServiceManagerIdleTestPeer::ObserveNormalTermination(*manager_,
                                                           first_process);
  EXPECT_EQ(first_generation + 1u, manager_->service_generation());
  EXPECT_EQ(CoreServiceManager::Availability::kStarting,
            manager_->availability());
  EXPECT_TRUE(CoreServiceManagerIdleTestPeer::IsBound(*manager_));
  EXPECT_EQ(0, preparation_count);

  content::ServiceProcessInfo replacement_process = CoreProcessInfo(2u);
  CoreServiceManagerIdleTestPeer::ObserveProcessLaunch(*manager_,
                                                       replacement_process);
  CoreServiceManagerIdleTestPeer::ObserveNormalTermination(*manager_,
                                                           replacement_process);
  EXPECT_EQ(first_generation + 2u, manager_->service_generation());
  EXPECT_FALSE(CoreServiceManagerIdleTestPeer::IsBound(*manager_));
  EXPECT_EQ(1, preparation_count);
  EXPECT_FALSE(preparation_result);
}

TEST_F(CoreServiceManagerIdleTest,
       LateReleasedMojoDisconnectCannotTearDownItsReplacement) {
  CoreServiceManagerIdleTestPeer::MakeReady(*manager_);
  const uint64_t first_generation = manager_->service_generation();
  manager_->TearDownForIdle();
  ASSERT_EQ(first_generation + 1u, manager_->service_generation());

  int preparation_count = 0;
  bool preparation_result = true;
  CoreServiceManagerIdleTestPeer::BeginReplacementLaunch(
      *manager_, base::BindOnce(
                     [](int* count, bool* result, bool ready) {
                       ++*count;
                       *result = ready;
                     },
                     &preparation_count, &preparation_result));

  CoreServiceManagerIdleTestPeer::ObserveMojoDisconnect(*manager_,
                                                        first_generation);
  EXPECT_EQ(first_generation + 1u, manager_->service_generation());
  EXPECT_EQ(CoreServiceManager::Availability::kStarting,
            manager_->availability());
  EXPECT_TRUE(CoreServiceManagerIdleTestPeer::IsBound(*manager_));
  EXPECT_EQ(0, preparation_count);
  EXPECT_EQ(1u, manager_->late_reply_count_for_testing());

  CoreServiceManagerIdleTestPeer::ObserveMojoDisconnect(*manager_,
                                                        first_generation + 1u);
  EXPECT_EQ(first_generation + 2u, manager_->service_generation());
  EXPECT_FALSE(CoreServiceManagerIdleTestPeer::IsBound(*manager_));
  EXPECT_EQ(1, preparation_count);
  EXPECT_FALSE(preparation_result);
}

}  // namespace
}  // namespace taffy
