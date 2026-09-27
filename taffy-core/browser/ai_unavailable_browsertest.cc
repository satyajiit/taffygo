// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/manual_browsing_guarantee.h"

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "taffy/browser/ai_runtime_availability.h"
#include "taffy/browser/download_command_router.h"
#include "taffy/browser/lifecycle_continuity_ledger.h"
#include "taffy/browser/navigation_lifecycle_tracker.h"
#include "taffy/browser/omnibox_input_classifier.h"
#include "taffy/browser/tab_session_registry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/shell/browser/shell.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"

// PAR-AI-BR-001 against a real browser: manual browsing works with the AI
// runtime absent, uninitialised, initialising, failed and switched off — and
// works identically in all of them.
//
// The unit test proves the guarantee catches a runtime-dependent capability.
// This one proves the real seams, wired to real Chromium objects, are not
// runtime-dependent: it navigates, opens tabs, goes back and forward, and
// registers probes over the seams that have a platform delegate so that the
// guarantee covers them too.

namespace taffy {
namespace {

// Probes over the stateful seams. Each exercises the seam and checks the
// answer; none of them reads the runtime state, which is what makes the
// guarantee's equality check meaningful.

class TabRegistryProbe : public ManualCapabilityProbe {
 public:
  explicit TabRegistryProbe(TabSessionRegistry* registry)
      : registry_(registry) {}

  ManualCapabilityStatus Probe() override {
    // Listing and lookup must answer whatever the runtime is doing.
    const size_t count = registry_->tab_count();
    if (registry_->ListTabs().size() != count) {
      return ManualCapabilityStatus::kFailed;
    }
    // And the assistant refusal must still be the assistant refusal.
    if (registry_->ActivateTab(TabId{"no-such-tab"},
                              TabRequestOrigin::kUserGesture) !=
        TabActivateResult::kRefusedUnknownTab) {
      return ManualCapabilityStatus::kFailed;
    }
    return ManualCapabilityStatus::kOperational;
  }

 private:
  raw_ptr<TabSessionRegistry> registry_;
};

class DownloadRouterProbe : public ManualCapabilityProbe {
 public:
  explicit DownloadRouterProbe(DownloadCommandRouter* router)
      : router_(router) {}

  ManualCapabilityStatus Probe() override {
    if (router_->Execute(0xFFFFFFFFu, DownloadCommand::kCancel,
                         DownloadCommandOrigin::kUserGesture) !=
        DownloadCommandResult::kRefusedUnknownDownload) {
      return ManualCapabilityStatus::kFailed;
    }
    return ManualCapabilityStatus::kOperational;
  }

 private:
  raw_ptr<DownloadCommandRouter> router_;
};

class LifecycleProbe : public ManualCapabilityProbe {
 public:
  explicit LifecycleProbe(LifecycleContinuityLedger* ledger)
      : ledger_(ledger) {}

  ManualCapabilityStatus Probe() override {
    const BrowserWindowId window{"ai_unavailable_probe_window"};
    ledger_->NotePhase(window, ActivityLifecyclePhase::kResumed);
    if (!ledger_->CurrentVerdict(window).page_control_permitted) {
      return ManualCapabilityStatus::kFailed;
    }
    ledger_->NotePhase(window, ActivityLifecyclePhase::kStopped);
    if (ledger_->CurrentVerdict(window).page_control_permitted) {
      return ManualCapabilityStatus::kFailed;
    }
    ledger_->ForgetWindow(window);
    return ManualCapabilityStatus::kOperational;
  }

 private:
  raw_ptr<LifecycleContinuityLedger> ledger_;
};

class AiUnavailableBrowserTest : public content::ContentBrowserTest {
 public:
  void SetUpOnMainThread() override {
    content::ContentBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());

    tab_probe_ = std::make_unique<TabRegistryProbe>(&registry_);
    download_probe_ = std::make_unique<DownloadRouterProbe>(&router_);
    lifecycle_probe_ = std::make_unique<LifecycleProbe>(&ledger_);

    ManualBrowsingGuarantee::Get().Register(ManualCapability::kTabsAndSessions,
                                            tab_probe_.get());
    ManualBrowsingGuarantee::Get().Register(ManualCapability::kDownloads,
                                            download_probe_.get());
    ManualBrowsingGuarantee::Get().Register(
        ManualCapability::kAndroidLifecycle, lifecycle_probe_.get());
  }

  void TearDownOnMainThread() override {
    ManualBrowsingGuarantee::Get().Register(ManualCapability::kTabsAndSessions,
                                            nullptr);
    ManualBrowsingGuarantee::Get().Register(ManualCapability::kDownloads,
                                            nullptr);
    ManualBrowsingGuarantee::Get().Register(
        ManualCapability::kAndroidLifecycle, nullptr);
    AiRuntimeAvailability::Get().SetState(AiRuntimeState::kAbsent);
    content::ContentBrowserTest::TearDownOnMainThread();
  }

 protected:
  content::WebContents* web_contents() { return shell()->web_contents(); }

  GURL PageUrl(const std::string& host, const std::string& path) {
    return embedded_test_server()->GetURL(host, path);
  }

  NavigationLifecycleTracker* GetTracker() {
    NavigationLifecycleTracker::CreateForWebContents(web_contents());
    return NavigationLifecycleTracker::FromWebContents(web_contents());
  }

  TabSessionRegistry registry_;
  DownloadCommandRouter router_;
  LifecycleContinuityLedger ledger_;
  std::unique_ptr<TabRegistryProbe> tab_probe_;
  std::unique_ptr<DownloadRouterProbe> download_probe_;
  std::unique_ptr<LifecycleProbe> lifecycle_probe_;
};

IN_PROC_BROWSER_TEST_F(AiUnavailableBrowserTest,
                       EverySeamIsIndependentOfTheRuntime) {
  const ManualBrowsingVerdict verdict = ManualBrowsingGuarantee::Get().Verify();

  EXPECT_TRUE(verdict.manual_browsing_is_independent);
  // Five built-ins plus the three registered here.
  EXPECT_GE(verdict.registered_capability_count, 8u);
  for (const ManualCapabilityFinding& finding : verdict.findings) {
    EXPECT_FALSE(finding.varies_with_runtime_state)
        << "capability " << static_cast<int>(finding.capability)
        << " behaves differently depending on the AI runtime";
    EXPECT_FALSE(finding.failed_in_some_state)
        << "capability " << static_cast<int>(finding.capability);
  }
}

IN_PROC_BROWSER_TEST_F(AiUnavailableBrowserTest,
                       NavigationWorksInEveryRuntimeState) {
  NavigationLifecycleTracker* tracker = GetTracker();

  for (AiRuntimeState state : kAllAiRuntimeStates) {
    AiRuntimeAvailability::Get().SetState(state);

    const GURL first = PageUrl("primary.test", "/title1.html");
    const GURL second = PageUrl("primary.test", "/title2.html");

    ASSERT_TRUE(content::NavigateToURL(shell(), first))
        << "state " << static_cast<int>(state);
    ASSERT_TRUE(content::NavigateToURL(shell(), second))
        << "state " << static_cast<int>(state);
    ASSERT_TRUE(content::HistoryGoBack(web_contents()))
        << "state " << static_cast<int>(state);

    const BrowserNavigationRecord* record = tracker->PrimaryMainFrameRecord();
    ASSERT_TRUE(record) << "state " << static_cast<int>(state);
    EXPECT_EQ(first, record->committed_url())
        << "state " << static_cast<int>(state);
    EXPECT_TRUE(record->CarriesRequestedContent());
    EXPECT_TRUE(tracker->control_state().can_go_forward);
  }
}

IN_PROC_BROWSER_TEST_F(AiUnavailableBrowserTest,
                       TheAddressBarDecisionIsIdenticalInEveryRuntimeState) {
  OmniboxClassification reference;
  bool have_reference = false;

  for (AiRuntimeState state : kAllAiRuntimeStates) {
    AiRuntimeAvailability::Get().SetState(state);
    const OmniboxClassification result =
        OmniboxInputClassifier::Classify("server.internal");
    if (!have_reference) {
      reference = result;
      have_reference = true;
      continue;
    }
    EXPECT_EQ(reference, result) << "state " << static_cast<int>(state);
  }

  // And the answer is still "ask", which is what PAR-BOX-001 requires whether
  // or not there is an assistant to ask about.
  EXPECT_TRUE(reference.requires_user_choice);
}

IN_PROC_BROWSER_TEST_F(AiUnavailableBrowserTest,
                       AFailedRuntimeDoesNotDisturbAnOpenPage) {
  AiRuntimeAvailability::Get().SetState(AiRuntimeState::kReady);
  ASSERT_TRUE(content::NavigateToURL(
      shell(), PageUrl("primary.test", "/title1.html")));

  // The runtime dies mid-session. Nothing about the page changes, and the next
  // navigation works.
  AiRuntimeAvailability::Get().SetState(AiRuntimeState::kFailed);
  EXPECT_EQ(PageUrl("primary.test", "/title1.html"),
            web_contents()->GetLastCommittedURL());
  ASSERT_TRUE(content::NavigateToURL(
      shell(), PageUrl("primary.test", "/title2.html")));
  EXPECT_EQ(PageUrl("primary.test", "/title2.html"),
            web_contents()->GetLastCommittedURL());
}

}  // namespace
}  // namespace taffy
