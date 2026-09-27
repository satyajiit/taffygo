// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>

#include "taffy/components/intelligence/content/page_intelligence_broker.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/common/public/bip_result.h"
#include "taffy/test/support/taffy_observation_test_base.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/shell/browser/shell.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

// The back/forward cache and redirects: two ways a document's identity moves
// without anything obviously ending.
//
// A cached document is frozen rather than destroyed. It comes back with the
// same script state, the same elements, and — if nothing intervened — the same
// node identifiers. That last one is the trap: a handle from before the freeze
// would still name something, and what it names is a document the runtime
// stopped watching. This fork retires the epoch on entry and allocates a new one
// on restore, which is a conservative reading of an open decision and is stated
// as such in the broker's own notes.
//
// A redirect is the other direction. The document that commits is not the one
// that was requested, and every scope decided against the requested origin has
// to be re-decided against the committed one.

namespace taffy::test {
namespace {

class BfcacheAndRedirectTest : public TaffyObservationTestBase {
 protected:
  // Puts the current document into the cache by navigating away, then brings it
  // back. Returns false when the document was not cached, which is a legitimate
  // outcome of a build or a page and must not be reported as a failure of the
  // property under test.
  bool RoundTripThroughTheCache(const GURL& elsewhere) {
    content::RenderFrameHostWrapper original(
        web_contents()->GetPrimaryMainFrame());
    if (!content::NavigateToURL(shell(), elsewhere)) {
      return false;
    }
    if (original.IsDestroyed()) {
      // Not cached. Every assertion in the caller is about a restore that did
      // not happen, so the caller has to know.
      return false;
    }
    return content::HistoryGoBack(web_contents());
  }
};

// Entering the cache retires the epoch. A frozen document is not an actionable
// one, and leaving its epoch alive would leave every handle in it apparently
// usable while nothing was watching the page.
IN_PROC_BROWSER_TEST_F(BfcacheAndRedirectTest, EnteringTheCacheRetiresTheEpoch) {
  const ObservationEnvelope before = ObserveFixture("static-article");
  const PageEpoch original_epoch = before.page_epoch;

  if (!RoundTripThroughTheCache(FixtureUrl("static-product"))) {
    GTEST_SKIP() << "The document was not placed in the back/forward cache in "
                    "this build. The property under test is about a restore, "
                    "and there was none; asserting anyway would be asserting "
                    "about a code path that did not run.";
  }

  ASSERT_TRUE(client().WaitForInvalidationCount(1));
  bool saw_cache_transition = false;
  for (const InvalidationNotice& notice : client().invalidations()) {
    if (notice.reason == InvalidationCode::kBfcacheEntered ||
        notice.reason == InvalidationCode::kBfcacheRestored) {
      saw_cache_transition = true;
      EXPECT_TRUE(notice.retires_page_epoch)
          << "A cache transition left the epoch alive. This fork allocates a "
             "new epoch on restore, so no handle survives the round trip.";
    }
  }
  EXPECT_TRUE(saw_cache_transition)
      << "A document went into the cache and came back and nothing was "
         "reported. Handles issued before the freeze would still look live.";

  const ObservationEnvelope after = client().Observe(
      builder().Observation(MainFrameId(), ObservationScope::kDocument));
  EXPECT_NE(original_epoch, after.page_epoch)
      << "The restored document carries the epoch it had before it was frozen. "
         "A handle from before the freeze is now valid again, against a page "
         "nobody was watching in between.";
}

// A handle from before the freeze is refused after the restore, with the code
// that names the document rather than the graph.
IN_PROC_BROWSER_TEST_F(BfcacheAndRedirectTest, AHandleDoesNotSurviveTheRoundTrip) {
  const ObservationEnvelope before = ObserveFixture("static-article");

  if (!RoundTripThroughTheCache(FixtureUrl("static-product"))) {
    GTEST_SKIP() << "The document was not cached in this build.";
  }

  ObservationRequest request =
      builder().Observation(MainFrameId(), ObservationScope::kDocument);
  request.expected_page_epoch = before.page_epoch;
  const ObservationEnvelope refused = client().Observe(std::move(request));
  EXPECT_EQ(ObservationResultCode::kStalePageEpoch, refused.code);
}

// A cross-origin redirect commits on the final origin, and an observation
// reports that origin. Anything decided against the requested origin has to be
// re-decided, and it cannot be re-decided against an origin nobody reported.
IN_PROC_BROWSER_TEST_F(BfcacheAndRedirectTest, RedirectsReportTheCommittedOrigin) {
  const GURL start = OriginUrl("primary", "/r/hop1?cross=1");
  const GURL destination = FixtureUrl("redirect-destination-cross-origin");
  ASSERT_TRUE(content::NavigateToURL(shell(), start, destination));

  GrantObservation({MainFrameOrigin()}, /*may_include_child_frames=*/false);
  const ObservationEnvelope envelope = client().Observe(
      builder().Observation(MainFrameId(), ObservationScope::kDocument));

  EXPECT_EQ(origins().OriginOf("partner").Serialize(),
            envelope.committed_url_metadata.origin.serialization)
      << "The observation reports the origin that was requested rather than "
         "the one that committed. A grant checked against the first is a grant "
         "checked against the wrong site.";
  EXPECT_EQ(origins().OriginOf("partner").Serialize(),
            envelope.origin.serialization);
}

// A grant for the origin that was requested does not admit an observation of
// the origin that committed. This is the failure a redirect exists to produce
// and the reason the grant is re-checked rather than carried.
IN_PROC_BROWSER_TEST_F(BfcacheAndRedirectTest, AGrantDoesNotFollowARedirect) {
  ASSERT_TRUE(NavigateToFixture("static-article"));
  GrantObservation({MainFrameOrigin()}, /*may_include_child_frames=*/false);

  const GURL start = OriginUrl("primary", "/r/hop1?cross=1");
  const GURL destination = FixtureUrl("redirect-destination-cross-origin");
  ASSERT_TRUE(content::NavigateToURL(shell(), start, destination));

  // The grant still names the primary origin; the document is on the partner
  // origin. The observation must not silently succeed against it.
  const ObservationEnvelope envelope = client().Observe(
      builder().Observation(MainFrameId(), ObservationScope::kDocument));
  EXPECT_NE(ObservationResultCode::kOk, envelope.code)
      << "An observation succeeded on an origin the grant does not name. The "
         "grant was decided against the site the user asked for, and a "
         "redirect changed the site.";
}

// A same-document fragment change is not a redirect and not a new document. It
// advances the revision and leaves the epoch alone, and confusing it with
// either of the two cases above would either kill live handles or keep dead
// ones.
IN_PROC_BROWSER_TEST_F(BfcacheAndRedirectTest, AFragmentChangeIsNeitherOfThose) {
  const ObservationEnvelope before = ObserveFixture("static-article");

  ASSERT_TRUE(content::ExecJs(web_contents(), "location.hash = '#measurements'"));

  const ObservationEnvelope after = client().Observe(
      builder().Observation(MainFrameId(), ObservationScope::kDocument));
  EXPECT_EQ(before.page_epoch, after.page_epoch);
  EXPECT_TRUE(after.committed_url_metadata.has_fragment)
      << "The observation does not report that the URL now has a fragment. A "
         "caller cannot tell 'no fragment' from 'fragment withheld' without "
         "it.";
}

}  // namespace
}  // namespace taffy::test
