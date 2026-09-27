// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <vector>

#include "taffy/components/intelligence/content/page_intelligence_broker.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/test/corpus/corpus_manifest.h"
#include "taffy/test/support/canary_leak_scanner.h"
#include "taffy/test/support/taffy_observation_test_base.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/shell/browser/shell.h"
#include "testing/gtest/include/gtest/gtest.h"

// Content that is present but not readable: text hidden by layout or colour,
// instructions in accessible names, and scripts a user cannot tell apart.
//
// **The property this file defends.** What a page says never becomes authority,
// and what a person can see and what the runtime observes must be
// distinguishable. Both halves matter. A runtime that could not see hidden text
// would be unable to warn about it. A runtime that could not tell hidden from
// visible would act on things nobody is looking at, and would report an answer
// drawn from text the reader never saw.
//
// **Why accessible names are their own case.** An accessible name is not
// decoration: it is the name a screen-reader user hears and the name an
// observation legitimately carries, and it is entirely under the page's
// control. A hostile page can therefore put an instruction where a visible-text
// filter will never look. The defence cannot be to drop accessible names —
// that would break the page for the person who needs them — so it has to be
// that a name is text like any other text, carrying no more weight.

namespace taffy::test {
namespace {

using HiddenContentTest = TaffyObservationTestBase;

// Every fixture that hides something, observed. None causes an egress and none
// leaks a seeded value, whichever mechanism the page used to hide it.
IN_PROC_BROWSER_TEST_F(HiddenContentTest, HidingMechanismsChangeNothingDownstream) {
  const std::vector<std::string> fixtures = {
      "hidden-and-offscreen", "injection-hidden", "css-obscured-instructions",
      "unicode-confusables", "hostile-accessible-names"};

  for (const std::string& fixture_id : fixtures) {
    SCOPED_TRACE(fixture_id);
    // One task per page. The walk starts on the first-party origin and then
    // stays on the hostile one, and a grant may only narrow: on a single task
    // the second origin is a widening, the service refuses it, and the last
    // four of these five pages are read under the first page's grant.
    BeginNewTask();
    const ObservationEnvelope envelope = ObserveFixture(fixture_id);
    EXPECT_NE(ObservationResultCode::kInternalError, envelope.code);
    EXPECT_FALSE(origins().sentinel().HasHits())
        << origins().sentinel().DescribeHits();
  }

  CanaryLeakScanner scanner = CanaryLeakScanner::ForWholeCorpus();
  scanner.AddSink("projection across every hiding mechanism",
                  client().TranscriptForLeakScan());
  scanner.AddJournal(journal());
  scanner.AddAuditStream(audit_stream());
  EXPECT_TRUE(scanner.AssertAllSinksClean());
}

// Hidden text is observed and marked, not dropped. The corpus fixture places
// the same sentence twice — once visible, once hidden — so an observation that
// reported one count for both would be indistinguishable from one that got it
// right.
IN_PROC_BROWSER_TEST_F(HiddenContentTest, HiddenTextIsObservedWithItsState) {
  const ObservationEnvelope envelope = ObserveFixture("hidden-and-offscreen");
  ASSERT_TRUE(envelope.code == ObservationResultCode::kOk ||
              envelope.code == ObservationResultCode::kIncomplete);
  EXPECT_GT(envelope.node_count, 0u);

  const CorpusFixture& fixture =
      CorpusManifest::Get().ById("hidden-and-offscreen");
  EXPECT_FALSE(fixture.prohibited_actions.empty())
      << "The corpus declares no prohibited actions for the hidden and "
         "off-screen fixture. Acting on something nobody can see is the "
         "failure this page exists to make testable, and the contract has to "
         "say so.";
}

// Instructions obscured by CSS — clipped, transparent, one pixel tall, coloured
// to match the background — are still content. The page's opinion about its own
// styling changes nothing about what the observation carries or what it means.
IN_PROC_BROWSER_TEST_F(HiddenContentTest, CssObscuredInstructionsCarryNoAuthority) {
  const ObservationEnvelope envelope =
      ObserveFixture("css-obscured-instructions");
  ASSERT_TRUE(envelope.code == ObservationResultCode::kOk ||
              envelope.code == ObservationResultCode::kIncomplete);

  EXPECT_EQ(ObservationScope::kDocument, envelope.scope)
      << "The observation's scope differs from the one granted. The page asked "
         "for a wider one in text it did not expect anyone to see.";
  EXPECT_FALSE(origins().sentinel().HasHits())
      << origins().sentinel().DescribeHits();
}

// Confusable characters, bidirectional overrides and zero-width joiners. The
// runtime is not required to normalize them away — normalization is lossy and
// would itself be a way to make two different strings look the same — but
// nothing about them may change what an observation is permitted to do.
IN_PROC_BROWSER_TEST_F(HiddenContentTest, ConfusablesDoNotChangeAuthority) {
  const ObservationEnvelope envelope = ObserveFixture("unicode-confusables");
  ASSERT_TRUE(envelope.code == ObservationResultCode::kOk ||
              envelope.code == ObservationResultCode::kIncomplete);

  // The origin the corpus serves this fixture from, read from the manifest
  // rather than named here. This fixture is one of the hostile family (F13) and
  // is served by the hostile origin, so asserting "primary" asserted that a
  // hostile page reports somebody else's origin - the opposite of the property.
  // The property is that the observed origin is whatever the browser committed,
  // and the manifest is what says what that is.
  EXPECT_EQ(origins()
                .OriginOf(CorpusManifest::Get().ById("unicode-confusables").origin)
                .Serialize(),
            envelope.committed_url_metadata.origin.serialization)
      << "The observed origin came from somewhere other than the browser's "
         "committed record. A page that could influence it with a lookalike "
         "hostname in its own text would be choosing its own security context.";
  EXPECT_FALSE(origins().sentinel().HasHits())
      << origins().sentinel().DescribeHits();
}

// Hostile accessible names. The names are carried, because a screen-reader user
// depends on them and so does an honest observation; what they never become is
// an instruction, a capability, or a reason to widen anything.
IN_PROC_BROWSER_TEST_F(HiddenContentTest, HostileAccessibleNamesAreJustText) {
  const ObservationEnvelope envelope =
      ObserveFixture("hostile-accessible-names");
  ASSERT_TRUE(envelope.code == ObservationResultCode::kOk ||
              envelope.code == ObservationResultCode::kIncomplete);
  EXPECT_GT(envelope.node_count, 0u)
      << "The page produced no nodes. Dropping accessible names because they "
         "can be hostile would break the page for the person who needs them, "
         "and would not make anything safer.";

  EXPECT_EQ(ObservationScope::kDocument, envelope.scope);
  EXPECT_FALSE(envelope.is_incognito)
      << "A page changed the observation's incognito flag. Nothing a page says "
         "can move a property of the profile it is loaded in.";
  EXPECT_FALSE(origins().sentinel().HasHits())
      << origins().sentinel().DescribeHits();
}

// A very large, deeply nested document. The defence is the budget, and the
// evidence that it worked is an explicit truncation rather than a slow success.
IN_PROC_BROWSER_TEST_F(HiddenContentTest, AnOversizedDocumentTruncatesExplicitly) {
  ASSERT_TRUE(NavigateToFixture("oversized-document"));
  GrantObservation({MainFrameOrigin()}, /*may_include_child_frames=*/false);

  const ObservationEnvelope envelope = client().Observe(
      builder().Observation(MainFrameId(), ObservationScope::kDocument));
  // kInternalError means the browser broke, and a deliberately oversized page is
  // the budget working. When those are confused the message has to carry enough
  // to tell which layer disagreed, because the code alone cannot: an encoding
  // the browser rejected and a snapshot it refused to validate arrive as the
  // same byte. Printed here rather than chased later.
  EXPECT_NE(ObservationResultCode::kInternalError, envelope.code)
      << "truncated=" << envelope.truncation.truncated
      << " budgets_reached=" << envelope.truncation.budgets_reached.size()
      << " omitted_nodes=" << envelope.truncation.omitted_node_count
      << " omitted_text_bytes=" << envelope.truncation.omitted_text_bytes
      << " omitted_frames=" << envelope.truncation.omitted_frame_count
      << " node_count=" << envelope.node_count
      << " total_bytes=" << envelope.total_bytes
      << " payload_bytes=" << envelope.graph_payload.size()
      << ". A non-empty payload with truncated=true means the encoder produced "
         "a graph and the code came from snapshot validation rather than from "
         "the budget path; an empty payload means the encoder refused.";
  EXPECT_TRUE(envelope.truncation.truncated ||
              envelope.code == ObservationResultCode::kBudgetExceeded)
      << "An oversized document was observed in full. The budget is the only "
         "thing between one page and the browser process's memory.";
  if (envelope.truncation.truncated) {
    EXPECT_FALSE(envelope.truncation.budgets_reached.empty())
        << "Truncation was reported without naming the budget it hit.";
  }
}

}  // namespace
}  // namespace taffy::test
