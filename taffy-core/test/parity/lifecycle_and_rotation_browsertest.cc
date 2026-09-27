// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>

#include "taffy/common/public/bip_identity.h"
#include "taffy/browser/lifecycle_continuity_ledger.h"
#include "taffy/browser/lifecycle_phase.h"
#include "taffy/components/intelligence/content/observability_recorder.h"
#include "taffy/test/support/taffy_browser_test_base.h"
#include "content/public/browser/visibility.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/shell/browser/shell.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

// Foreground and background transitions, rotation, and configuration change
// (PAR-AND-005 and PAR-AND-007).
//
// Two things are being proved and they need different instruments.
//
// **The page keeps working.** A page that is hidden and shown again is the same
// document with the same state, and that is a real web-contents property a
// browser test can drive directly.
//
// **No duplicate action.** This is the hard half and it cannot be driven from a
// browser test at all, because the event that causes it is an Android Activity
// being destroyed and recreated, and there is no Activity here. What can be
// proved — and is the whole of what PAR-AND-007 asks for — is that a
// re-delivered continuation is refused. The ledger is the mechanism, the
// generation counter is the reason it works, and this suite drives it through
// every ordering an Activity could produce.
//
// The device half of PAR-AND-005 and PAR-AND-007 belongs to the instrumentation
// suite in //taffy/app/android, which has an Activity to rotate. This
// file proves the browser-process rule that suite depends on.

namespace taffy::test {
namespace {

class ParityLifecycleTest : public TaffyBrowserTestBase {
 protected:
  BrowserWindowId window_id() const { return BrowserWindowId{"window-1"}; }

  ContinuationKey Key(const std::string& operation, uint64_t generation) const {
    ContinuationKey key;
    key.tab_id = ToRecordIdentifier("tab-1");
    key.operation_id = ToRecordIdentifier(operation);
    key.generation = generation;
    return key;
  }
};

// PAR-AND-005. Hiding and showing a tab does not disturb the document: the same
// page, the same script state, the same committed URL.
IN_PROC_BROWSER_TEST_F(ParityLifecycleTest, HidingAndShowingKeepsTheDocument) {
  ASSERT_TRUE(NavigateToFixture("spa-router"));
  const GURL url = web_contents()->GetLastCommittedURL();

  ASSERT_TRUE(content::ExecJs(web_contents(),
                              "window.taffyLifecycleMarker = 'set-before-hide'"));

  web_contents()->WasHidden();
  EXPECT_EQ(content::Visibility::HIDDEN, web_contents()->GetVisibility());
  web_contents()->WasShown();
  EXPECT_EQ(content::Visibility::VISIBLE, web_contents()->GetVisibility());

  EXPECT_EQ(url, web_contents()->GetLastCommittedURL());
  EXPECT_EQ("set-before-hide",
            content::EvalJs(web_contents(), "window.taffyLifecycleMarker"))
      << "The document was replaced across a visibility change. Every handle "
         "the runtime held would be dead and nothing would have said so.";
}

// PAR-AND-005. Page control is permitted only while the window is the
// foreground, interactive activity, and the pause carries a reason the user can
// be shown rather than a bare false.
IN_PROC_BROWSER_TEST_F(ParityLifecycleTest, PageControlFollowsTheForeground) {
  LifecycleContinuityLedger ledger;

  const LifecycleVerdict resumed =
      ledger.NotePhase(window_id(), ActivityLifecyclePhase::kResumed);
  EXPECT_TRUE(resumed.page_control_permitted);
  EXPECT_FALSE(resumed.recreation_in_progress);

  const LifecycleVerdict paused =
      ledger.NotePhase(window_id(), ActivityLifecyclePhase::kPaused);
  EXPECT_FALSE(paused.page_control_permitted);
  EXPECT_NE(PageControlPauseReason::kNone, paused.pause_reason)
      << "A pause with no reason is a pause the user cannot be told about.";

  const LifecycleVerdict stopped =
      ledger.NotePhase(window_id(), ActivityLifecyclePhase::kStopped);
  EXPECT_FALSE(stopped.page_control_permitted);
  EXPECT_TRUE(stopped.state_must_be_preserved)
      << "Stopped is a phase the process may not come back from. Anything not "
         "written by now is lost, and a tab or a task lost is what PAR-AND-007 "
         "forbids.";
}

// PAR-AND-007. A rotation advances the generation, and a continuation created
// before it is refused rather than run. This is the "tapped once, ordered
// twice" failure, and the generation counter is what tells a stale re-delivery
// from a legitimate repeat.
IN_PROC_BROWSER_TEST_F(ParityLifecycleTest, ARotationRefusesStaleContinuations) {
  LifecycleContinuityLedger ledger;
  ledger.NotePhase(window_id(), ActivityLifecyclePhase::kResumed);

  const uint64_t before = ledger.generation(window_id());
  EXPECT_EQ(ContinuationAdmission::kFirstAdmission,
            ledger.Admit(window_id(), Key("open-link", before)));

  ledger.NoteConfigurationChange(window_id(), ConfigurationChangeKind::kRotation);
  const uint64_t after = ledger.generation(window_id());
  EXPECT_GT(after, before) << "A configuration change must advance the "
                              "generation, or a re-delivery is indistinguishable "
                              "from a fresh instruction.";

  EXPECT_EQ(ContinuationAdmission::kRefusedStaleGeneration,
            ledger.Admit(window_id(), Key("open-link", before)))
      << "The re-delivered continuation was admitted. It belongs to a browser "
         "state that no longer exists, and running it is a duplicate side "
         "effect nobody asked for.";

  // A legitimate repeat in the new generation is allowed, once.
  EXPECT_EQ(ContinuationAdmission::kFirstAdmission,
            ledger.Admit(window_id(), Key("open-link", after)));
  EXPECT_EQ(ContinuationAdmission::kAlreadyAdmitted,
            ledger.Admit(window_id(), Key("open-link", after)));
}

// PAR-AND-007. A destroy-for-recreation and a final destroy look identical from
// inside a destroy callback and mean opposite things. The ledger has to tell
// them apart, because one preserves state and the other must not.
IN_PROC_BROWSER_TEST_F(ParityLifecycleTest, RecreationAndFinalDestroyDiffer) {
  LifecycleContinuityLedger ledger;
  ledger.NotePhase(window_id(), ActivityLifecyclePhase::kResumed);

  const LifecycleVerdict recreating =
      ledger.NotePhase(window_id(), ActivityLifecyclePhase::kDestroyedForRecreation);
  EXPECT_TRUE(recreating.recreation_in_progress);
  EXPECT_TRUE(recreating.state_must_be_preserved);
  EXPECT_FALSE(recreating.page_control_permitted);

  LifecycleContinuityLedger final_ledger;
  final_ledger.NotePhase(window_id(), ActivityLifecyclePhase::kResumed);
  const LifecycleVerdict finished =
      final_ledger.NotePhase(window_id(), ActivityLifecyclePhase::kDestroyedFinal);
  EXPECT_FALSE(finished.recreation_in_progress)
      << "A final destroy is the user leaving. Treating it as a recreation "
         "would preserve state the user ended.";
  EXPECT_FALSE(finished.page_control_permitted);
}

// PAR-AND-007. A folding device delivers several configuration changes in one
// callback. One generation advance is correct; several would refuse
// continuations that were created legitimately between them.
IN_PROC_BROWSER_TEST_F(ParityLifecycleTest, CombinedConfigurationChangeAdvancesOnce) {
  LifecycleContinuityLedger ledger;
  ledger.NotePhase(window_id(), ActivityLifecyclePhase::kResumed);
  const uint64_t before = ledger.generation(window_id());

  const auto combined = static_cast<ConfigurationChangeKind>(
      static_cast<uint32_t>(ConfigurationChangeKind::kScreenSize) |
      static_cast<uint32_t>(ConfigurationChangeKind::kDensity) |
      static_cast<uint32_t>(ConfigurationChangeKind::kMultiWindow));
  ledger.NoteConfigurationChange(window_id(), combined);

  EXPECT_EQ(before + 1, ledger.generation(window_id()));
}

// PAR-AND-005. The device lock is process-wide rather than per window, and it
// suspends page control everywhere. A per-window lock would leave a second
// window acting while the screen is off.
IN_PROC_BROWSER_TEST_F(ParityLifecycleTest, DeviceLockSuspendsEveryWindow) {
  LifecycleContinuityLedger ledger;
  const BrowserWindowId second{"window-2"};
  ledger.NotePhase(window_id(), ActivityLifecyclePhase::kResumed);
  ledger.NotePhase(second, ActivityLifecyclePhase::kResumed);
  ASSERT_TRUE(ledger.CurrentVerdict(window_id()).page_control_permitted);
  ASSERT_TRUE(ledger.CurrentVerdict(second).page_control_permitted);

  ledger.SetDeviceLocked(true);
  EXPECT_FALSE(ledger.CurrentVerdict(window_id()).page_control_permitted);
  EXPECT_FALSE(ledger.CurrentVerdict(second).page_control_permitted);

  ledger.SetDeviceLocked(false);
  EXPECT_TRUE(ledger.CurrentVerdict(window_id()).page_control_permitted);
  EXPECT_TRUE(ledger.CurrentVerdict(second).page_control_permitted);
}

}  // namespace
}  // namespace taffy::test
