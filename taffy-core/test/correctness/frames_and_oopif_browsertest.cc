// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <vector>

#include "taffy/components/intelligence/content/page_intelligence_broker.h"
#include "taffy/common/public/bip_observation.h"
#include "taffy/test/support/canary_leak_scanner.h"
#include "taffy/test/support/taffy_observation_test_base.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/shell/browser/shell.h"
#include "testing/gtest/include/gtest/gtest.h"

// Frames, cross-origin frames and out-of-process iframes.
//
// The frame tree is the place where "the browser owns the topology" stops being
// a slogan. A renderer describes what it can see; the broker assembles
// parentage from Chromium's own frame tree; and a frame is included in an
// observation only when the grant names its origin. Each of those three is a
// separate defence and each is asserted separately here, because a build that
// lost one would still pass a test of the other two.
//
// The corpus supplies the shapes: a same-origin frame, a cross-origin frame
// that is out of process under site isolation, a nested frame two origins deep,
// and a cross-origin frame carrying a canary that must never appear in an
// observation of its parent.

namespace taffy::test {
namespace {

class FramesTest : public TaffyObservationTestBase {
 protected:
  // Every frame in the tree, from Chromium rather than from an observation.
  // The comparison between this and what an observation reports is the whole
  // subject of the suite.
  std::vector<content::RenderFrameHost*> AllFrames() {
    std::vector<content::RenderFrameHost*> frames;
    // VERIFY AT SP-01: RenderFrameHost::ForEachRenderFrameHost takes a
    // base::FunctionRef at recent milestones and a callback at older ones.
    // Upstream file to read: content/public/browser/render_frame_host.h.
    // The same item appears in
    // //taffy/components/intelligence/content/page_intelligence_broker.cc and has the
    // same answer.
    web_contents()->GetPrimaryMainFrame()->ForEachRenderFrameHost(
        [&frames](content::RenderFrameHost* host) { frames.push_back(host); });
    return frames;
  }
};

// A same-origin child frame is admitted by an empty allowlist, because an empty
// allowlist means "the root frame's own origin only" and the child shares it.
IN_PROC_BROWSER_TEST_F(FramesTest, SameOriginChildIsIncluded) {
  const ObservationEnvelope envelope =
      ObserveFixtureWithFrames("iframe-same-origin", {});
  ASSERT_GE(envelope.frames.size(), 2u)
      << "The same-origin frame fixture reported fewer than two frames.";

  size_t included = 0;
  for (const FrameSummary& frame : envelope.frames) {
    if (frame.included) {
      ++included;
    }
    if (!frame.is_main_frame) {
      EXPECT_FALSE(frame.is_cross_origin_to_parent);
      EXPECT_TRUE(frame.parent_frame_id.has_value())
          << "A child frame with no parent identifier cannot be placed in the "
             "tree, and a caller would have to guess.";
    }
  }
  EXPECT_GE(included, 2u);
}

// A cross-origin child is excluded when the grant does not name its origin.
// The exclusion is reported rather than silent: a caller has to be able to tell
// "this page has one frame" from "this page has two and you may see one".
IN_PROC_BROWSER_TEST_F(FramesTest, CrossOriginChildIsExcludedUnlessGranted) {
  const ObservationEnvelope envelope =
      ObserveFixtureWithFrames("iframe-cross-origin", {});

  bool saw_excluded_child = false;
  for (const FrameSummary& frame : envelope.frames) {
    if (frame.is_main_frame) {
      continue;
    }
    if (!frame.included) {
      saw_excluded_child = true;
      EXPECT_TRUE(frame.is_cross_origin_to_parent);
    }
  }
  EXPECT_TRUE(saw_excluded_child)
      << "The cross-origin child was either included without a grant naming "
         "its origin, or omitted from the frame list entirely. Both are wrong "
         "for different reasons: the first is a policy failure, the second "
         "makes the page's shape unknowable.";
}

// The same page with the partner origin granted. The child is now included, and
// the contrast with the case above is what proves the exclusion was a policy
// decision rather than an inability to see the frame at all.
IN_PROC_BROWSER_TEST_F(FramesTest, CrossOriginChildIsIncludedWhenGranted) {
  const ObservationEnvelope envelope =
      ObserveFixtureWithFrames("iframe-cross-origin", {"partner"});

  bool saw_included_cross_origin_child = false;
  for (const FrameSummary& frame : envelope.frames) {
    if (!frame.is_main_frame && frame.included &&
        frame.is_cross_origin_to_parent) {
      saw_included_cross_origin_child = true;
      EXPECT_TRUE(frame.page_epoch.is_valid())
          << "An included frame with no page epoch of its own cannot have a "
             "handle issued against it: each frame has its own epoch, revision "
             "and node namespace.";
      EXPECT_NE(envelope.page_epoch, frame.page_epoch)
          << "A child frame shares the main frame's epoch. Two documents with "
             "one epoch means invalidating one invalidates the other, and a "
             "child navigation would kill the parent's handles.";
    }
  }
  EXPECT_TRUE(saw_included_cross_origin_child);
}

// A nested frame two origins deep. Parentage comes from Chromium's tree, so the
// depth-two frame's parent is the depth-one frame and not the main frame — the
// mistake a renderer-reported tree would make, and the one that would let an
// action authorized against a top-level origin land two origins away.
IN_PROC_BROWSER_TEST_F(FramesTest, NestedFrameParentageComesFromTheBrowser) {
  const ObservationEnvelope envelope =
      ObserveFixtureWithFrames("iframe-nested-oopif", {"partner", "embed"});

  const FrameSummary* main = nullptr;
  for (const FrameSummary& frame : envelope.frames) {
    if (frame.is_main_frame) {
      main = &frame;
      break;
    }
  }
  ASSERT_TRUE(main);
  EXPECT_FALSE(main->parent_frame_id.has_value());

  size_t depth_two_frames = 0;
  for (const FrameSummary& frame : envelope.frames) {
    if (frame.is_main_frame || !frame.parent_frame_id.has_value()) {
      continue;
    }
    if (frame.parent_frame_id.value() != main->frame_id) {
      ++depth_two_frames;
    }
  }
  EXPECT_GE(depth_two_frames, 1u)
      << "Every frame in the nested fixture claims the main frame as its "
         "parent. That is what a renderer-reported tree looks like, and it "
         "flattens a two-origin nesting into a one-origin one.";
}

// Site isolation actually applies. If the cross-origin frame shares a process
// with its parent, every frame-scoping assertion in this directory is an
// assertion about a build that does not isolate, and PAR-SEC-008 has already
// failed elsewhere.
IN_PROC_BROWSER_TEST_F(FramesTest, CrossOriginFramesAreOutOfProcess) {
  ASSERT_TRUE(NavigateToFixture("iframe-cross-origin"));

  content::RenderFrameHost* main = web_contents()->GetPrimaryMainFrame();
  bool saw_out_of_process_child = false;
  for (content::RenderFrameHost* host : AllFrames()) {
    if (host == main) {
      continue;
    }
    if (host->GetProcess() != main->GetProcess()) {
      saw_out_of_process_child = true;
    }
  }
  EXPECT_TRUE(saw_out_of_process_child)
      << "No child frame runs in its own process. Either site isolation is "
         "off in this build, or the corpus fixture is not actually "
         "cross-origin. Both invalidate this whole suite.";
}

// The frame the corpus seeds with a canary. Its content must not appear in an
// observation of the parent, whether or not its origin is granted: the value is
// a credential, and the sensitivity ceiling is below it in either case.
IN_PROC_BROWSER_TEST_F(FramesTest, AGrantedFrameStillWithholdsItsSecret) {
  const ObservationEnvelope envelope =
      ObserveFixtureWithFrames("iframe-cross-origin", {"partner"});

  CanaryLeakScanner scanner = CanaryLeakScanner::ForFixture("frame-payload-partner");
  scanner.AddSink("cross-origin frame observation",
                  client().TranscriptForLeakScan());
  EXPECT_TRUE(scanner.AssertAllSinksClean())
      << "Granting an origin admits its structure, never its secrets. The "
         "grant widens what may be observed; it does not raise the sensitivity "
         "ceiling.";

  // The observation happened at all, so the assertion above is not vacuous.
  EXPECT_GT(envelope.frames.size(), 1u);
}

}  // namespace
}  // namespace taffy::test
