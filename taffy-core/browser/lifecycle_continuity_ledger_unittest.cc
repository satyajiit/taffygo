// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/lifecycle_continuity_ledger.h"

#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

// PAR-AND-005 (foreground and background transitions) and PAR-AND-007
// (rotation, configuration change and process recreation with no tab or task
// loss and no duplicate action).
//
// The duplicate-action tests are the ones to read first. Everything else here
// is a state table; that one is the property the parity row exists for.

namespace taffy {
namespace {

class LifecycleContinuityLedgerTest : public testing::Test {
 protected:
  ContinuationKey Key(const char* tab, const char* operation,
                      uint64_t generation) {
    ContinuationKey key;
    key.tab_id = ToRecordIdentifier(tab);
    key.operation_id = ToRecordIdentifier(operation);
    key.generation = generation;
    return key;
  }

  void Resume() {
    ledger_.NotePhase(window_, ActivityLifecyclePhase::kCreated);
    ledger_.NotePhase(window_, ActivityLifecyclePhase::kStarted);
    ledger_.NotePhase(window_, ActivityLifecyclePhase::kResumed);
  }

  content::BrowserTaskEnvironment task_environment_;
  const BrowserWindowId window_{"window_1"};
  LifecycleContinuityLedger ledger_;
};

TEST_F(LifecycleContinuityLedgerTest, PageControlNeedsTheForeground) {
  // An unreported window fails closed.
  EXPECT_FALSE(ledger_.CurrentVerdict(window_).page_control_permitted);
  EXPECT_EQ(PageControlPauseReason::kNotForeground,
            ledger_.CurrentVerdict(window_).pause_reason);

  Resume();
  EXPECT_TRUE(ledger_.CurrentVerdict(window_).page_control_permitted);
  EXPECT_EQ(PageControlPauseReason::kNone,
            ledger_.CurrentVerdict(window_).pause_reason);

  ledger_.NotePhase(window_, ActivityLifecyclePhase::kPaused);
  EXPECT_FALSE(ledger_.CurrentVerdict(window_).page_control_permitted);
  EXPECT_EQ(PageControlPauseReason::kNotForeground,
            ledger_.CurrentVerdict(window_).pause_reason);
}

TEST_F(LifecycleContinuityLedgerTest, StateIsPreservedFromThePauseOnward) {
  // Android may kill a paused process without another callback, so the
  // boundary is at pause and not at stop.
  Resume();
  EXPECT_FALSE(ledger_.CurrentVerdict(window_).state_must_be_preserved);

  EXPECT_TRUE(ledger_.NotePhase(window_, ActivityLifecyclePhase::kPaused)
                  .state_must_be_preserved);
  EXPECT_TRUE(ledger_.NotePhase(window_, ActivityLifecyclePhase::kStopped)
                  .state_must_be_preserved);
}

TEST_F(LifecycleContinuityLedgerTest, ALockedDeviceIsReportedAsSuch) {
  Resume();
  ledger_.SetDeviceLocked(true);

  const LifecycleVerdict verdict = ledger_.CurrentVerdict(window_);
  EXPECT_FALSE(verdict.page_control_permitted);
  // The reason a person can act on, not a generic "not available".
  EXPECT_EQ(PageControlPauseReason::kDeviceLocked, verdict.pause_reason);

  ledger_.SetDeviceLocked(false);
  EXPECT_TRUE(ledger_.CurrentVerdict(window_).page_control_permitted);
}

TEST_F(LifecycleContinuityLedgerTest, RecreationPausesUntilTheNextResume) {
  Resume();
  const uint64_t before = ledger_.generation(window_);

  const LifecycleVerdict destroyed = ledger_.NotePhase(
      window_, ActivityLifecyclePhase::kDestroyedForRecreation);
  EXPECT_TRUE(destroyed.recreation_in_progress);
  EXPECT_FALSE(destroyed.page_control_permitted);
  EXPECT_EQ(PageControlPauseReason::kRecreationInProgress,
            destroyed.pause_reason);
  EXPECT_EQ(before + 1, destroyed.generation);

  Resume();
  const LifecycleVerdict resumed = ledger_.CurrentVerdict(window_);
  EXPECT_FALSE(resumed.recreation_in_progress);
  EXPECT_TRUE(resumed.page_control_permitted);
  // The generation does not go back. A continuation created before the
  // rotation stays stale forever.
  EXPECT_EQ(before + 1, resumed.generation);
}

TEST_F(LifecycleContinuityLedgerTest, AFinishedActivitySaysSo) {
  Resume();
  const LifecycleVerdict verdict =
      ledger_.NotePhase(window_, ActivityLifecyclePhase::kDestroyedFinal);

  EXPECT_EQ(PageControlPauseReason::kActivityFinished, verdict.pause_reason);
  EXPECT_TRUE(verdict.state_must_be_preserved);
  EXPECT_FALSE(verdict.page_control_permitted);
}

TEST_F(LifecycleContinuityLedgerTest,
       ADeclaredConfigurationChangeAdvancesTheGenerationToo) {
  // A rotation the manifest declares does not destroy the activity, but the UI
  // still rebuilds and re-delivers. Treating both shapes the same is what
  // stops the answer depending on the manifest.
  Resume();
  const uint64_t before = ledger_.generation(window_);

  const LifecycleVerdict verdict = ledger_.NoteConfigurationChange(
      window_, ConfigurationChangeKind::kRotation |
                   ConfigurationChangeKind::kScreenSize);

  EXPECT_EQ(before + 1, verdict.generation);
  // No destroy happened, so control is still permitted.
  EXPECT_TRUE(verdict.page_control_permitted);
}

TEST_F(LifecycleContinuityLedgerTest, ANoOpConfigurationChangeChangesNothing) {
  Resume();
  const uint64_t before = ledger_.generation(window_);
  ledger_.NoteConfigurationChange(window_, ConfigurationChangeKind::kNone);
  EXPECT_EQ(before, ledger_.generation(window_));
}

TEST_F(LifecycleContinuityLedgerTest, AContinuationRunsOncePerGeneration) {
  Resume();
  const uint64_t generation = ledger_.generation(window_);

  EXPECT_EQ(ContinuationAdmission::kFirstAdmission,
            ledger_.Admit(window_, Key("tab_1", "resume_extraction",
                                       generation)));
  // The re-delivery case. This is the assertion the parity row is about.
  EXPECT_EQ(ContinuationAdmission::kAlreadyAdmitted,
            ledger_.Admit(window_, Key("tab_1", "resume_extraction",
                                       generation)));
}

TEST_F(LifecycleContinuityLedgerTest, AContinuationFromBeforeARotationIsStale) {
  Resume();
  const uint64_t old_generation = ledger_.generation(window_);

  ledger_.NotePhase(window_, ActivityLifecyclePhase::kDestroyedForRecreation);
  Resume();

  // The queued continuation belongs to a browser state that no longer exists.
  // Running it now is exactly the duplicate action PAR-AND-007 forbids.
  EXPECT_EQ(ContinuationAdmission::kRefusedStaleGeneration,
            ledger_.Admit(window_,
                          Key("tab_1", "resume_extraction", old_generation)));

  // And the same operation, created afresh, is allowed. Deduplicating by
  // identifier alone would have refused this one too.
  EXPECT_EQ(ContinuationAdmission::kFirstAdmission,
            ledger_.Admit(window_, Key("tab_1", "resume_extraction",
                                       ledger_.generation(window_))));
}

TEST_F(LifecycleContinuityLedgerTest, AGenerationFromTheFutureIsRefused) {
  Resume();
  EXPECT_EQ(ContinuationAdmission::kRefusedFutureGeneration,
            ledger_.Admit(window_, Key("tab_1", "resume_extraction",
                                       ledger_.generation(window_) + 5)));
}

TEST_F(LifecycleContinuityLedgerTest, TabsDoNotShadowEachOther) {
  Resume();
  const uint64_t generation = ledger_.generation(window_);

  EXPECT_EQ(ContinuationAdmission::kFirstAdmission,
            ledger_.Admit(window_, Key("tab_1", "resume", generation)));
  // Same operation name, different tab. Two tabs running the same named step
  // must not deduplicate each other into one.
  EXPECT_EQ(ContinuationAdmission::kFirstAdmission,
            ledger_.Admit(window_, Key("tab_2", "resume", generation)));
  EXPECT_EQ(2u, ledger_.admitted_count(window_));
}

TEST_F(LifecycleContinuityLedgerTest, AnEmptyKeyIsRefused) {
  Resume();
  const uint64_t generation = ledger_.generation(window_);

  EXPECT_EQ(ContinuationAdmission::kRefusedInvalidKey,
            ledger_.Admit(window_, Key("tab_1", "", generation)));
  EXPECT_EQ(ContinuationAdmission::kRefusedInvalidKey,
            ledger_.Admit(window_, Key("", "resume", generation)));
  EXPECT_EQ(0u, ledger_.admitted_count(window_));
}

TEST_F(LifecycleContinuityLedgerTest, TheAdmittedSetIsClearedByARotation) {
  Resume();
  ASSERT_EQ(ContinuationAdmission::kFirstAdmission,
            ledger_.Admit(window_,
                          Key("tab_1", "resume", ledger_.generation(window_))));
  ASSERT_EQ(1u, ledger_.admitted_count(window_));

  ledger_.NotePhase(window_, ActivityLifecyclePhase::kDestroyedForRecreation);
  EXPECT_EQ(0u, ledger_.admitted_count(window_));
}

TEST_F(LifecycleContinuityLedgerTest, WindowsAreIndependent) {
  const BrowserWindowId other{"window_2"};
  Resume();
  ledger_.NotePhase(other, ActivityLifecyclePhase::kCreated);

  // A foldable running two instances: one foreground, one not.
  EXPECT_TRUE(ledger_.CurrentVerdict(window_).page_control_permitted);
  EXPECT_FALSE(ledger_.CurrentVerdict(other).page_control_permitted);

  ledger_.ForgetWindow(window_);
  EXPECT_FALSE(ledger_.CurrentVerdict(window_).page_control_permitted);
}

TEST_F(LifecycleContinuityLedgerTest, ConfigurationFlagsCombine) {
  const ConfigurationChangeKind fold = ConfigurationChangeKind::kScreenSize |
                                       ConfigurationChangeKind::kDensity |
                                       ConfigurationChangeKind::kMultiWindow;

  EXPECT_TRUE(HasConfigurationChange(fold, ConfigurationChangeKind::kDensity));
  EXPECT_TRUE(
      HasConfigurationChange(fold, ConfigurationChangeKind::kMultiWindow));
  EXPECT_FALSE(HasConfigurationChange(fold, ConfigurationChangeKind::kRotation));
  EXPECT_FALSE(HasConfigurationChange(ConfigurationChangeKind::kNone,
                                      ConfigurationChangeKind::kRotation));
}

}  // namespace
}  // namespace taffy
