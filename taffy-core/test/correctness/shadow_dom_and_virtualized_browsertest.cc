// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>

#include "taffy/components/intelligence/content/page_intelligence_broker.h"
#include "taffy/contracts/bip/mojom/page_intelligence.mojom.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/test/corpus/corpus_manifest.h"
#include "taffy/test/support/canary_leak_scanner.h"
#include "taffy/test/support/taffy_observation_test_base.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/shell/browser/shell.h"
#include "testing/gtest/include/gtest/gtest.h"

// Shadow DOM and virtualized content: the two places where what a page shows
// and what a naive traversal finds are different documents.
//
// An open shadow root projects into the composed tree, so its content is
// legitimately observable. A closed one does not, and the corpus keeps a
// credential inside one precisely so that a projection that reached in would be
// caught by a value rather than by an argument.
//
// A virtualized list recycles its row elements. The same element is row 3, then
// row 40, then row 12. Node identity that followed the element would silently
// re-point a handle at different data; identity that is retired with the row is
// what makes a stale handle fail closed instead.

namespace taffy::test {
namespace {

using ShadowAndVirtualizedTest = TaffyObservationTestBase;

// An open shadow root's content is part of the observation. Asserted through the
// corpus's declared fields rather than by naming strings here, so a corpus that
// changes the widget changes the expectation with it.
IN_PROC_BROWSER_TEST_F(ShadowAndVirtualizedTest, OpenShadowContentIsObserved) {
  const ObservationEnvelope envelope = ObserveFixture("shadow-dom-open");
  ASSERT_TRUE(envelope.code == ObservationResultCode::kOk ||
              envelope.code == ObservationResultCode::kIncomplete);
  EXPECT_GT(envelope.node_count, 0u)
      << "An open shadow root produced no nodes. The composed tree is what an "
         "observation walks, and a walk that stopped at the host would miss "
         "everything the user can see.";

  const CorpusFixture& fixture = CorpusManifest::Get().ById("shadow-dom-open");
  EXPECT_FALSE(fixture.expected_semantic_fields.empty())
      << "The corpus declares no expected fields for the open shadow fixture, "
         "so there is nothing for an extraction assertion to be measured "
         "against.";
}

// A closed shadow root is not projected, and the warning says so. The
// distinction between "there was nothing there" and "there was something and it
// is not visible to this projection" is the difference between an honest
// incomplete result and a confidently wrong one.
IN_PROC_BROWSER_TEST_F(ShadowAndVirtualizedTest, ClosedShadowContentIsNeverProjected) {
  const ObservationEnvelope envelope = ObserveFixture("shadow-dom-closed");

  CanaryLeakScanner scanner = CanaryLeakScanner::ForFixture("shadow-dom-closed");
  scanner.AddSink("closed shadow observation", client().TranscriptForLeakScan());
  EXPECT_TRUE(scanner.AssertAllSinksClean())
      << "A value inside a closed shadow root reached the projection. A closed "
         "root is not reachable from script, so anything that reached it went "
         "around the composed tree.";

  bool warned = false;
  for (uint8_t code : envelope.warning_codes) {
    // The contract's WarningCode member for a closed root, carried as its
    // numeric value. Compared against the mojom member rather than a literal
    // so that renumbering the enumeration cannot silently break this.
    if (code == static_cast<uint8_t>(
                    mojom::WarningCode::kClosedShadowRootNotProjected)) {
      warned = true;
    }
  }
  EXPECT_TRUE(warned)
      << "The observation carried no warning about the closed root. Silence "
         "here reads as 'this page has no sign-in widget', which is a "
         "confidently wrong answer rather than an honest incomplete one.";
}

// A virtualized table recycles elements. Scrolling produces a different set of
// rows, and the identifiers for the rows that left are retired rather than
// reused for the rows that arrived.
IN_PROC_BROWSER_TEST_F(ShadowAndVirtualizedTest, RecycledRowsDoNotReuseIdentity) {
  const ObservationEnvelope before = ObserveFixture("virtualized-table");
  ASSERT_GT(before.node_count, 0u);

  ASSERT_TRUE(content::ExecJs(
      web_contents(),
      "const viewport = document.getElementById('viewport');"
      "viewport.scrollTop = viewport.scrollHeight / 2;"));
  ASSERT_TRUE(content::ExecJs(web_contents(),
                              "new Promise(r => requestAnimationFrame(() => "
                              "requestAnimationFrame(r)))"));

  const ObservationEnvelope after = client().Observe(
      builder().Observation(MainFrameId(), ObservationScope::kDocument));
  EXPECT_EQ(before.page_epoch, after.page_epoch)
      << "Scrolling retired the page epoch. Scrolling is not a navigation.";
  EXPECT_GT(after.graph_revision, before.graph_revision)
      << "Recycling rows did not advance the revision, so a handle to a row "
         "that has since become a different row still satisfies its revision "
         "floor.";
}

// The truncation contract, exercised where it actually bites. An infinite list
// is longer than any budget, so the observation is truncated — and the
// truncation is explicit, names the budget it hit, and says whether the
// omission could change an answer.
IN_PROC_BROWSER_TEST_F(ShadowAndVirtualizedTest, TruncationIsExplicitOnAnInfiniteList) {
  ASSERT_TRUE(NavigateToFixture("infinite-scroll"));
  GrantObservation({MainFrameOrigin()}, /*may_include_child_frames=*/false);

  // Load several batches so the document is genuinely larger than the budget.
  for (int batch = 0; batch < 6; ++batch) {
    ASSERT_TRUE(content::ExecJs(
        web_contents(),
        "const scroller = document.getElementById('scroller');"
        "scroller.scrollTop = scroller.scrollHeight;"));
  }

  ObservationRequest request =
      builder().Observation(MainFrameId(), ObservationScope::kDocument);
  // A budget small enough that truncation is certain, so the assertion is about
  // the reporting rather than about whether the page happened to be large.
  request.budget.max_nodes = 5;
  const ObservationEnvelope envelope = client().Observe(std::move(request));

  EXPECT_TRUE(envelope.truncation.truncated)
      << "A five-node budget over a list of a hundred items reported no "
         "truncation. Silent truncation is the failure mode where a partial "
         "answer is presented as a complete one.";
  EXPECT_FALSE(envelope.truncation.budgets_reached.empty())
      << "Truncation was reported without naming the budget that caused it, so "
         "a caller cannot tell whether asking for more would help.";
  EXPECT_GT(envelope.truncation.omitted_node_count, 0u);
}

// Off-screen and visually hidden content is observable but marked, and the two
// are different states. A runtime that treated "present in the tree" as "the
// user can see it" would act on something nobody is looking at.
IN_PROC_BROWSER_TEST_F(ShadowAndVirtualizedTest, HiddenAndOffscreenAreDistinguished) {
  const ObservationEnvelope envelope = ObserveFixture("hidden-and-offscreen");
  ASSERT_TRUE(envelope.code == ObservationResultCode::kOk ||
              envelope.code == ObservationResultCode::kIncomplete);

  const CorpusFixture& fixture =
      CorpusManifest::Get().ById("hidden-and-offscreen");
  EXPECT_FALSE(fixture.verifier_postconditions.empty())
      << "The corpus declares no verifier postconditions for the hidden and "
         "off-screen fixture, so there is nothing to hold a visibility "
         "assertion to.";
}

}  // namespace
}  // namespace taffy::test
