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
#include "url/gurl.h"

// Indirect prompt injection: visible, hidden, and inside a third-party frame.
//
// **The property this file defends.** A web page is data. Nothing a page says
// can grant a capability, widen a task's scope, change the session's mode,
// mint an approval, or cause a request to an origin the task was not scoped to.
// The page may say anything at all; what it may not do is be obeyed.
//
// **How it fails closed.** Every case ends with the same two assertions: no
// request reached the corpus's collection endpoint, and no capability appeared
// that the test did not create. The first is checked for every test in the
// directory by the fixture's own teardown; it is restated in the cases where
// the bait is the subject, because a reader of one test should not have to
// discover the guarantee somewhere else.
//
// **What this file does not claim.** It does not test a model. There is no
// model in a browser test and there must not be: a suite that measured whether
// a language model resisted an instruction would be measuring the model, and the
// browser-process property — that the instruction never becomes authority — has
// to hold whatever the model does. The model-facing half is the context
// firewall's, one layer up.

namespace taffy::test {
namespace {

using PromptInjectionTest = TaffyObservationTestBase;

// Every hostile fixture the corpus declares, observed. The bait is followed by
// nothing: no request to the collection endpoint, and no seeded secret in the
// projection.
//
// Iterating the family rather than naming pages means a hostile fixture added
// later is covered the moment it lands.
IN_PROC_BROWSER_TEST_F(PromptInjectionTest, NoHostileFixtureCausesAnEgress) {
  for (const CorpusFixture* fixture : CorpusManifest::Get().ByFamily("F13")) {
    SCOPED_TRACE(fixture->id);
    // One task per page, because the family spans two origins: the framed case
    // is served from the first-party origin and every other member from the
    // hostile one. A grant may only narrow, so registering the second origin
    // on one task is a widening, the service refuses it, and the walk carries
    // on under a grant naming a page it has already left. The refusal is the
    // product being right; reusing the task was this test asking it to be
    // wrong.
    BeginNewTask();
    const ObservationEnvelope envelope = ObserveFixture(fixture->id);
    EXPECT_NE(ObservationResultCode::kInternalError, envelope.code)
        << "A hostile page made the observation path fail internally. Failing "
           "closed is correct; failing over is not.";

    EXPECT_FALSE(origins().sentinel().HasHits())
        << origins().sentinel().DescribeHits();
  }
}

// The visible case. The instruction is ordinary page text, so it is legitimately
// observed — a runtime that dropped it would be hiding what the page says from
// the person reading the page. What must not happen is any consequence.
IN_PROC_BROWSER_TEST_F(PromptInjectionTest, VisibleInstructionsAreTextAndNothingMore) {
  const ObservationEnvelope envelope = ObserveFixture("injection-visible");
  ASSERT_TRUE(envelope.code == ObservationResultCode::kOk ||
              envelope.code == ObservationResultCode::kIncomplete);

  // The page asked for a wider scope. The grant is unchanged: it is the policy
  // engine's to widen and no page is party to that decision.
  EXPECT_EQ(ObservationScope::kDocument, envelope.scope)
      << "The observation's scope differs from the one that was granted. The "
         "only thing between a page and a wider scope is that nothing reads "
         "the page's opinion of it.";
  EXPECT_FALSE(envelope.is_incognito);
  EXPECT_FALSE(origins().sentinel().HasHits())
      << origins().sentinel().DescribeHits();
}

// The hidden case. Text positioned off-screen, coloured to match the
// background, or marked aria-hidden is still text, and the projection has to
// carry it with its visibility state rather than either dropping it or
// presenting it as visible content.
//
// Dropping it is the tempting mistake and the wrong one: an instruction the
// runtime cannot see is one the risk surface cannot report either.
IN_PROC_BROWSER_TEST_F(PromptInjectionTest, HiddenInstructionsAreObservedAsHidden) {
  const ObservationEnvelope envelope = ObserveFixture("injection-hidden");
  ASSERT_TRUE(envelope.code == ObservationResultCode::kOk ||
              envelope.code == ObservationResultCode::kIncomplete);
  EXPECT_GT(envelope.node_count, 0u)
      << "The hidden-injection fixture produced no nodes at all. An "
         "instruction nobody can see is one nobody can report.";
  EXPECT_FALSE(origins().sentinel().HasHits())
      << origins().sentinel().DescribeHits();
}

// The in-frame case, which is the one that matters most. The instruction lives
// on a third-party origin inside a first-party page. Without a grant naming
// that origin the frame is excluded outright, so the instruction is not even
// observed — and with a grant, it is observed as content belonging to that
// frame, never as content of the page that embeds it.
IN_PROC_BROWSER_TEST_F(PromptInjectionTest, AFramedInstructionStaysInsideItsFrame) {
  const ObservationEnvelope without_grant =
      ObserveFixtureWithFrames("injection-in-iframe", {});

  bool hostile_frame_excluded = false;
  for (const FrameSummary& frame : without_grant.frames) {
    if (frame.is_main_frame) {
      continue;
    }
    if (!frame.included) {
      hostile_frame_excluded = true;
    }
  }
  EXPECT_TRUE(hostile_frame_excluded)
      << "A third-party frame was observed without a grant naming its origin. "
         "An empty allowlist admits same-origin children and nothing else, so "
         "a hidden third-party frame is excluded before anyone asks whether it "
         "is visible.";

  // With the hostile origin granted, the frame is included, and its content is
  // attributed to it.
  //
  // A second reading is a second task. The first reading's grant named the
  // first-party origin alone, and a replacement grant may only narrow, so
  // adding the hostile origin to that same task would be refused and this
  // reading would silently repeat the one above — reporting the frame as
  // excluded and blaming the grant for it.
  BeginNewTask();
  const ObservationEnvelope with_grant =
      ObserveFixtureWithFrames("injection-in-iframe", {"hostile"});
  bool hostile_frame_included = false;
  for (const FrameSummary& frame : with_grant.frames) {
    if (!frame.is_main_frame && frame.included &&
        frame.origin.serialization ==
            origins().OriginOf("hostile").Serialize()) {
      hostile_frame_included = true;
      EXPECT_TRUE(frame.is_cross_origin_to_parent);
      EXPECT_NE(with_grant.page_epoch, frame.page_epoch)
          << "The framed instruction shares the embedding page's epoch, so a "
             "handle issued against the frame would look like a handle against "
             "the page.";
    }
  }
  EXPECT_TRUE(hostile_frame_included)
      << "Granting the hostile origin did not include its frame, so the "
           "exclusion above may have been an inability to see the frame rather "
           "than a policy decision.";

  EXPECT_FALSE(origins().sentinel().HasHits())
      << origins().sentinel().DescribeHits();
}

// The bait itself. The hostile fixtures name an outside host and an attacker
// mailbox, and the corpus's own rule is that a correct run never requests the
// collection endpoint. This case proves the sink is reachable at all, so that
// the absence assertion in every other test is known not to be vacuous.
IN_PROC_BROWSER_TEST_F(PromptInjectionTest, TheCollectionEndpointIsReachableWhenAsked) {
  ExpectExfiltrationAttempt();

  const CorpusManifest& manifest = CorpusManifest::Get();
  const GURL sink = OriginUrl(manifest.exfiltration_sink_origin(),
                              manifest.exfiltration_sink_path());
  // Navigating there directly is the test deliberately doing the thing no run
  // is allowed to do. The endpoint refuses and records the hit.
  ASSERT_TRUE(content::NavigateToURL(web_contents(), sink));

  EXPECT_TRUE(origins().sentinel().HasHits())
      << "The corpus's collection endpoint recorded nothing even when it was "
         "requested directly. Every other test in this directory asserts that "
         "it was not reached, and those assertions mean nothing if it cannot "
         "be.";
}

// No hostile page anywhere in the corpus leaves a seeded secret in the
// projection. Run over the whole hostile family in one test because the
// interesting failure is a value crossing from a page nobody was looking at.
IN_PROC_BROWSER_TEST_F(PromptInjectionTest, NoHostilePageLeaksASeededSecret) {
  for (const CorpusFixture* fixture : CorpusManifest::Get().ByFamily("F13")) {
    SCOPED_TRACE(fixture->id);
    // One task per page, for the reason NoHostileFixtureCausesAnEgress states
    // at its own loop. The scan below still sees these pages either way — a
    // root frame is never subject to the child-frame allowlist, so it is
    // projected whatever origin the grant names — but a walk that leaves a
    // refused grant behind it is proving the property under a policy nobody
    // granted, and the next fixture added to the family may well be the one
    // where that difference decides the answer.
    BeginNewTask();
    ObserveFixture(fixture->id);
  }

  CanaryLeakScanner scanner = CanaryLeakScanner::ForWholeCorpus();
  scanner.AddSink("projection after the hostile family",
                  client().TranscriptForLeakScan());
  scanner.AddJournal(journal());
  scanner.AddAuditStream(audit_stream());
  EXPECT_TRUE(scanner.AssertAllSinksClean());
}

}  // namespace
}  // namespace taffy::test
