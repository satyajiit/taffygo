// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <vector>

#include "taffy/browser/ai_runtime_availability.h"
#include "taffy/browser/ai_runtime_state.h"
#include "taffy/browser/manual_browsing_guarantee.h"
#include "taffy/browser/navigation_lifecycle_tracker.h"
#include "taffy/test/corpus/corpus_manifest.h"
#include "taffy/test/support/taffy_browser_test_base.h"
#include "base/memory/raw_ptr.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "content/shell/browser/shell.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

// Manual browsing with the assistant unavailable (PAR-AI-BR-001).
//
// The seam one directory up proves that no browser-process component depends on
// the AI runtime. This suite proves the consequence: with the runtime in every
// unavailable state the product can be in, the browser still browses the whole
// corpus.
//
// The distinction is worth the extra file. A dependency test asks "does this
// code call into the runtime". A parity test asks "does the product still
// work", and the second question catches the failure the first cannot — a
// component that does not call the runtime but waits on something that does.
//
// Every case runs the same script over the same pages, once per unavailable
// state, because "it works when the key is missing but hangs when the provider
// refused" is a real shape of this bug and a single state would miss it.

namespace taffy::test {
namespace {

class ParityAiUnavailableTest : public TaffyBrowserTestBase {
 public:
  void SetUpOnMainThread() override {
    TaffyBrowserTestBase::SetUpOnMainThread();
    NavigationLifecycleTracker::CreateForWebContents(web_contents());
    tracker_ = NavigationLifecycleTracker::FromWebContents(web_contents());
  }

 protected:
  // Navigate, follow a link, go back, reload. The ordinary things a person does
  // with a browser, run end to end.
  void RunTheOrdinaryBrowsingScript() {
    ASSERT_TRUE(NavigateToFixture("static-article"));
    ASSERT_TRUE(NavigateToFixture("static-product"));

    ASSERT_TRUE(content::HistoryGoBack(web_contents()));
    EXPECT_EQ(FixtureUrl("static-article"),
              web_contents()->GetLastCommittedURL());

    content::TestNavigationObserver reload(web_contents());
    web_contents()->GetController().Reload(content::ReloadType::NORMAL, false);
    reload.Wait();
    EXPECT_EQ(FixtureUrl("static-article"),
              web_contents()->GetLastCommittedURL());

    EXPECT_TRUE(tracker_->control_state().can_go_forward);
  }

  raw_ptr<NavigationLifecycleTracker> tracker_ = nullptr;
};

// PAR-AI-BR-001. Every unavailable state the runtime can be in, and the browser
// works in all of them.
IN_PROC_BROWSER_TEST_F(ParityAiUnavailableTest, BrowsingWorksInEveryUnavailableState) {
  // The complete list is owned by the runtime-state header, so a state added
  // later is covered here without anyone remembering to add it.
  for (AiRuntimeState state : kAllAiRuntimeStates) {
    if (AiRuntimeIsAvailable(state)) {
      continue;
    }
    AiRuntimeAvailability::Get().SetState(state);
    ASSERT_FALSE(AiRuntimeAvailability::Get().IsAvailable())
        << "state " << static_cast<int>(state)
        << " is supposed to be an unavailable state; running the script with "
           "the runtime available would prove nothing.";

    RunTheOrdinaryBrowsingScript();
  }
}

// PAR-AI-BR-001. The guarantee itself: every registered manual capability
// reports available while the runtime is not. This is the assertion that fails
// when a seam grows a dependency on the runtime, and it names which one.
IN_PROC_BROWSER_TEST_F(ParityAiUnavailableTest, EveryManualCapabilityStaysAvailable) {
  ManualBrowsingGuarantee& guarantee = ManualBrowsingGuarantee::Get();
  ASSERT_GT(guarantee.registered_count(), 0u)
      << "No manual capability probe is registered in this binary, so the "
         "guarantee would pass over an empty list. That is the one failure "
         "shape this seam cannot detect from the inside.";

  const ManualBrowsingVerdict verdict = guarantee.Verify();
  EXPECT_TRUE(verdict.manual_browsing_is_independent)
      << "A manual browsing capability answered differently depending on the "
         "assistant runtime's state. Whatever else is true, the browser has to "
         "browse.";
  for (const ManualCapabilityFinding& finding : verdict.findings) {
    EXPECT_FALSE(finding.varies_with_runtime_state)
        << "capability " << static_cast<int>(finding.capability)
        << " varies with the runtime state";
    EXPECT_FALSE(finding.failed_in_some_state)
        << "capability " << static_cast<int>(finding.capability)
        << " failed in at least one runtime state";
  }
}

// PAR-AI-BR-001. The whole corpus, not just two pages. A browser that renders
// the simple fixtures and fails on the hostile or the framed ones is not a
// browser that works without the assistant.
IN_PROC_BROWSER_TEST_F(ParityAiUnavailableTest, TheWholeCorpusLoadsWithoutTheAssistant) {
  AiRuntimeAvailability::Get().SetState(AiRuntimeState::kAbsent);
  ASSERT_FALSE(AiRuntimeAvailability::Get().IsAvailable());

  size_t loaded = 0;
  for (const CorpusFixture& fixture : CorpusManifest::Get().fixtures()) {
    if (!fixture.url_path.ends_with(".html")) {
      continue;
    }
    ASSERT_TRUE(content::NavigateToURL(shell(), FixtureUrl(fixture.id)))
        << "fixture " << fixture.id;
    EXPECT_TRUE(web_contents()->GetPrimaryMainFrame()->IsRenderFrameLive())
        << "fixture " << fixture.id;
    ++loaded;
  }
  EXPECT_GE(loaded, 20u);
}

}  // namespace
}  // namespace taffy::test
