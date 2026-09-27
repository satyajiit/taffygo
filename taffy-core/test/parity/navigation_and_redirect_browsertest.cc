// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <vector>

#include "base/json/string_escape.h"
#include "taffy/browser/browser_navigation_record.h"
#include "taffy/browser/navigation_lifecycle_tracker.h"
#include "taffy/test/corpus/corpus_manifest.h"
#include "taffy/test/support/taffy_browser_test_base.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "content/shell/browser/shell.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

// Navigation, history and redirect parity, measured over the fixture corpus.
//
// //taffy/browser already proves the navigation seam against
// synthetic pages, one property per test. This suite is the other half of
// PAR-NAV-001, -002, -003 and -004: the same seam running over the corpus the
// benchmark runs over, with the corpus's own declared transitions as the
// expectation.
//
// That distinction is why the file exists rather than duplicating what is one
// directory up. A seam test proves the code does what it says. A corpus test
// proves the corpus's declared transitions are the transitions that actually
// happen — and when the two disagree, one of them is wrong and neither could
// have found out alone.

namespace taffy::test {
namespace {

using ParityNavigationTest = TaffyBrowserTestBase;

NavigationLifecycleTracker* AttachTracker(content::WebContents* contents) {
  NavigationLifecycleTracker::CreateForWebContents(contents);
  return NavigationLifecycleTracker::FromWebContents(contents);
}

// PAR-NAV-001. Every fixture in the corpus commits at the URL the manifest
// says it is served from, and the browser's record of where the user is comes
// from the browser.
//
// Iterating the corpus rather than naming pages is deliberate: a fixture added
// to the corpus is covered by this test the moment it lands, which is the only
// arrangement under which "the corpus is the authority" survives contact with
// a growing corpus.
IN_PROC_BROWSER_TEST_F(ParityNavigationTest, EveryFixtureCommitsAtItsDeclaredUrl) {
  NavigationLifecycleTracker* tracker = AttachTracker(web_contents());

  size_t visited = 0;
  for (const CorpusFixture& fixture : CorpusManifest::Get().fixtures()) {
    // Only pages. The corpus also declares data files and documents, which are
    // served but are not navigations with a semantic record.
    if (!fixture.url_path.ends_with(".html")) {
      continue;
    }
    const GURL url = FixtureUrl(fixture.id);
    ASSERT_TRUE(content::NavigateToURL(shell(), url))
        << "fixture " << fixture.id << " at " << url;

    const BrowserNavigationRecord* record = tracker->PrimaryMainFrameRecord();
    ASSERT_TRUE(record) << "fixture " << fixture.id;
    EXPECT_EQ(url, record->committed_url()) << "fixture " << fixture.id;
    EXPECT_EQ(url::Origin::Create(url).Serialize(),
              record->final_origin().serialization)
        << "fixture " << fixture.id;
    EXPECT_TRUE(record->is_primary_main_frame()) << "fixture " << fixture.id;
    ++visited;
  }

  EXPECT_GE(visited, 20u)
      << "Fewer corpus pages were visited than the corpus is supposed to "
         "contain. A shrinking corpus is a shrinking test.";
}

// PAR-NAV-001 and PAR-NAV-002. History entries accumulate in order and the
// back, forward and reload controls move between them.
IN_PROC_BROWSER_TEST_F(ParityNavigationTest, HistoryEntriesAndControlsAgree) {
  NavigationLifecycleTracker* tracker = AttachTracker(web_contents());

  const GURL article = FixtureUrl("static-article");
  const GURL product = FixtureUrl("static-product");
  ASSERT_TRUE(content::NavigateToURL(shell(), article));
  ASSERT_TRUE(content::NavigateToURL(shell(), product));

  content::NavigationController& controller = web_contents()->GetController();
  ASSERT_EQ(2, controller.GetEntryCount());
  EXPECT_EQ(article, controller.GetEntryAtIndex(0)->GetURL());
  EXPECT_EQ(product, controller.GetEntryAtIndex(1)->GetURL());

  EXPECT_TRUE(tracker->control_state().can_go_back);
  EXPECT_FALSE(tracker->control_state().can_go_forward);

  ASSERT_TRUE(content::HistoryGoBack(web_contents()));
  EXPECT_EQ(article, tracker->PrimaryMainFrameRecord()->committed_url());
  EXPECT_TRUE(tracker->control_state().can_go_forward);

  ASSERT_TRUE(content::HistoryGoForward(web_contents()));
  EXPECT_EQ(product, tracker->PrimaryMainFrameRecord()->committed_url());

  content::TestNavigationObserver reload_observer(web_contents());
  controller.Reload(content::ReloadType::NORMAL, /*check_for_repost=*/false);
  reload_observer.Wait();
  EXPECT_EQ(product, tracker->PrimaryMainFrameRecord()->committed_url());
}

// PAR-NAV-004. A same-origin redirect chain ends where the corpus says, and the
// record carries the whole chain rather than only its destination.
IN_PROC_BROWSER_TEST_F(ParityNavigationTest, SameOriginRedirectChainIsRecorded) {
  NavigationLifecycleTracker* tracker = AttachTracker(web_contents());

  const GURL start = OriginUrl("primary", "/r/hop1");
  const GURL destination = FixtureUrl("redirect-destination");
  ASSERT_TRUE(content::NavigateToURL(shell(), start, destination));

  const BrowserNavigationRecord* record = tracker->PrimaryMainFrameRecord();
  ASSERT_TRUE(record);
  EXPECT_EQ(destination, record->committed_url());
  EXPECT_FALSE(record->crossed_origin_during_redirect())
      << "The same-origin chain must not be reported as an origin crossing, or "
         "the cross-origin assertion below would be satisfied by any redirect.";
}

// PAR-NAV-004, the half that matters. After a cross-origin redirect the final
// origin is what the browser reports, and the fact that an origin was crossed
// survives into the record — because a permission or model scope decided
// against the first origin has to be rechecked against the last.
IN_PROC_BROWSER_TEST_F(ParityNavigationTest,
                       CrossOriginRedirectReportsTheFinalOrigin) {
  NavigationLifecycleTracker* tracker = AttachTracker(web_contents());

  const GURL start = OriginUrl("primary", "/r/hop1?cross=1");
  const GURL destination = FixtureUrl("redirect-destination-cross-origin");
  ASSERT_TRUE(content::NavigateToURL(shell(), start, destination));

  const BrowserNavigationRecord* record = tracker->PrimaryMainFrameRecord();
  ASSERT_TRUE(record);
  EXPECT_EQ(destination, record->committed_url());
  EXPECT_EQ(origins().OriginOf("partner").Serialize(),
            record->final_origin().serialization);
  EXPECT_TRUE(record->crossed_origin_during_redirect());
}

// PAR-NAV-004. The corpus's open-redirect endpoint refuses to leave its own
// origin. The refusal is the fixture's contract, and a test that did not check
// it would let a future change turn the endpoint into a real open redirect,
// which would make every "final origin" assertion above meaningless.
IN_PROC_BROWSER_TEST_F(ParityNavigationTest, TheOpenRedirectFixtureRefuses) {
  const GURL attempt =
      OriginUrl("primary", "/r/open?to=http://exfil.invalid/steal");
  // The navigation commits: what it commits to is the refusal page, on the
  // origin it started from.
  ASSERT_TRUE(content::NavigateToURL(shell(), attempt));
  EXPECT_EQ(origins().OriginOf("primary"),
            web_contents()->GetPrimaryMainFrame()->GetLastCommittedOrigin());
}

// PAR-NAV-003. A link that opens a new tab produces a second web contents that
// the browser owns, and the opener keeps its own document.
IN_PROC_BROWSER_TEST_F(ParityNavigationTest, PopupOpensASecondTabAndTheOpenerStays) {
  ASSERT_TRUE(NavigateToFixture("popup-opener"));
  const GURL opener_url = web_contents()->GetLastCommittedURL();

  content::ShellAddedObserver new_shell_observer;
  ASSERT_TRUE(content::ExecJs(web_contents(),
                              "document.getElementById('open-partner').click()"));
  content::Shell* popup = new_shell_observer.GetShell();
  ASSERT_TRUE(popup);
  ASSERT_TRUE(content::WaitForLoadStop(popup->web_contents()));

  EXPECT_EQ(opener_url, web_contents()->GetLastCommittedURL())
      << "Opening a popup must not move the opener's document.";
  EXPECT_EQ(origins().OriginOf("partner"),
            popup->web_contents()->GetPrimaryMainFrame()->GetLastCommittedOrigin())
      << "The corpus declares the popup target on the partner origin; a popup "
         "that stayed same-origin would make the tab-ownership assertions "
         "vacuous.";
}

// PAR-WEB-013. The corpus carries non-English and mixed-script content, and it
// decodes to the same text the manifest declares. Encoding failures are silent:
// the page renders, the extraction is wrong, and nothing errors.
IN_PROC_BROWSER_TEST_F(ParityNavigationTest, ContentEncodingSurvivesNavigation) {
  ASSERT_TRUE(NavigateToFixture("international-content"));
  const CorpusFixture& fixture =
      CorpusManifest::Get().ById("international-content");

  for (const CorpusExpectedField& field : fixture.expected_semantic_fields) {
    if (field.field.rfind("text.", 0) != 0) {
      continue;
    }
    const std::string script = "document.body.innerText.includes(" +
                               base::GetQuotedJSONString(field.value) + ")";
    EXPECT_EQ(true, content::EvalJs(web_contents(), script))
        << "The corpus declares " << field.field << " as " << field.value
        << ", and the decoded document does not contain it. A decoding "
           "regression renders fine and extracts wrongly, so this is the only "
           "place it shows up.";
  }
}

}  // namespace
}  // namespace taffy::test
