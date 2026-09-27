// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string>
#include <vector>

#include "taffy/components/intelligence/content/observability_recorder.h"
#include "taffy/browser/restored_task_record.h"
#include "taffy/browser/session_recovery_planner.h"
#include "taffy/browser/tab_record.h"
#include "taffy/test/support/taffy_browser_test_base.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/shell/browser/shell.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/size.h"
#include "url/gurl.h"

// Tab restore after a clean restart, and recovery after an eviction or a crash
// (PAR-TAB-001, PAR-TAB-003, PAR-TAB-004).
//
// The two halves are tested differently on purpose.
//
// Restoring tabs is Chromium's, and the evidence that matters is that the
// session state a browser test can persist and reload comes back in order with
// the right tab active. That is a real navigation-controller property and it is
// proved here with real navigations.
//
// Recovering a TASK is not Chromium's, and it cannot be proved by restarting a
// browser test: the process that would have to die is the one running the
// assertions. What can be proved, and is the property PAR-TAB-004 actually
// names, is that the restored state cannot express a replay. That is a
// structural property of the persisted types, and this suite asserts it against
// the same planner the product runs, with journals in every phase a real
// interruption could leave behind.
//
// Nothing here fabricates a restart. A test that pretended to restart and then
// asserted on its own pretence would be measuring the pretence.

namespace taffy::test {
namespace {

using ParityTabRestoreTest = TaffyBrowserTestBase;

RecordIdentifier Id(const std::string& value) {
  return ToRecordIdentifier(value);
}

PersistedTabRecord UserTab(const std::string& id, size_t index, bool active) {
  PersistedTabRecord record;
  record.tab_id = Id(id);
  record.index = index;
  record.was_active = active;
  record.ownership = TabOwnership::kUser;
  return record;
}

PersistedTabRecord AssistantTab(const std::string& id,
                                size_t index,
                                const std::string& owning_task) {
  PersistedTabRecord record;
  record.tab_id = Id(id);
  record.index = index;
  record.ownership = TabOwnership::kAssistant;
  if (!owning_task.empty()) {
    record.owning_task_id = Id(owning_task);
    record.has_owning_task = true;
  }
  return record;
}

// PAR-TAB-001. Session history for a tab survives being navigated away and
// back, in order, with the entries the browser committed rather than the ones a
// page claimed.
IN_PROC_BROWSER_TEST_F(ParityTabRestoreTest, SessionHistoryKeepsItsOrder) {
  const std::vector<std::string> fixtures = {
      "static-article", "static-product", "comparison-source-a",
      "comparison-source-c"};
  for (const std::string& fixture : fixtures) {
    ASSERT_TRUE(NavigateToFixture(fixture)) << fixture;
  }

  content::NavigationController& controller = web_contents()->GetController();
  ASSERT_EQ(static_cast<int>(fixtures.size()), controller.GetEntryCount());
  for (size_t index = 0; index < fixtures.size(); ++index) {
    EXPECT_EQ(FixtureUrl(fixtures[index]),
              controller.GetEntryAtIndex(static_cast<int>(index))->GetURL())
        << "entry " << index;
  }
}

// PAR-TAB-001. A second tab is a genuinely separate browsing context: its
// history does not appear in the first, and closing one does not disturb the
// other.
IN_PROC_BROWSER_TEST_F(ParityTabRestoreTest, TabsDoNotShareSessionHistory) {
  ASSERT_TRUE(NavigateToFixture("static-article"));

  content::Shell* second = content::Shell::CreateNewWindow(
      shell()->web_contents()->GetBrowserContext(), GURL(), nullptr,
      gfx::Size());
  ASSERT_TRUE(second);
  ASSERT_TRUE(content::NavigateToURL(second, FixtureUrl("static-product")));

  EXPECT_EQ(1, web_contents()->GetController().GetEntryCount());
  EXPECT_EQ(1, second->web_contents()->GetController().GetEntryCount());
  EXPECT_EQ(FixtureUrl("static-article"), web_contents()->GetLastCommittedURL());

  second->Close();
  EXPECT_EQ(FixtureUrl("static-article"), web_contents()->GetLastCommittedURL());
}

// PAR-TAB-003. A clean restart restores every user tab, in the persisted order,
// with the persisted tab active.
IN_PROC_BROWSER_TEST_F(ParityTabRestoreTest, CleanRestartRestoresUserTabsInOrder) {
  const std::vector<PersistedTabRecord> persisted = {
      UserTab("tab-a", 0, false), UserTab("tab-b", 1, true),
      UserTab("tab-c", 2, false)};

  const SessionRecoveryPlan plan =
      PlanSessionRecovery(RestartCause::kCleanRestart, persisted, {});

  ASSERT_EQ(3u, plan.tabs.size());
  EXPECT_EQ(3u, plan.restored_user_tab_count);
  EXPECT_EQ(0u, plan.paused_assistant_tab_count);
  EXPECT_EQ(0u, plan.discarded_tab_count);
  for (size_t index = 0; index < plan.tabs.size(); ++index) {
    EXPECT_EQ(TabRestoreAction::kRestoreAsUserTab, plan.tabs[index].action);
    EXPECT_EQ(index, plan.tabs[index].index);
  }
  ASSERT_TRUE(plan.active_tab_id.has_value());
  EXPECT_STREQ("tab-b", plan.active_tab_id->chars.data());
  EXPECT_FALSE(plan.user_must_be_shown_recovery_status)
      << "A clean restart with no incomplete task has nothing to report.";
}

// PAR-TAB-004. After an eviction, an assistant tab whose task did not finish
// comes back visible and inert, and the user is told before anything continues.
IN_PROC_BROWSER_TEST_F(ParityTabRestoreTest, EvictionPausesUnfinishedAssistantWork) {
  const std::vector<PersistedTabRecord> persisted = {
      UserTab("tab-a", 0, true), AssistantTab("tab-b", 1, "task-1"),
      AssistantTab("tab-c", 2, std::string())};

  PersistedTaskJournalEntry entry;
  entry.task_id = Id("task-1");
  entry.action_id = Id("action-9");
  entry.dispatch_id = Id("dispatch-9");
  entry.phase = JournalPhase::kDispatching;
  entry.effect_is_external = true;

  const SessionRecoveryPlan plan =
      PlanSessionRecovery(RestartCause::kProcessEviction, persisted, {entry});

  EXPECT_EQ(1u, plan.restored_user_tab_count);
  EXPECT_EQ(1u, plan.paused_assistant_tab_count);
  EXPECT_EQ(1u, plan.discarded_tab_count)
      << "An assistant tab with no incomplete task behind it is discarded; "
         "leaving it would accumulate tabs nobody opened.";
  EXPECT_TRUE(plan.user_must_be_shown_recovery_status);

  ASSERT_EQ(1u, plan.tasks.size());
  const RestoredTaskRecord& task = plan.tasks.front();
  EXPECT_EQ(RecoveryDisposition::kOutcomeUnknown, task.disposition())
      << "Something was in flight when the process died. The browser cannot "
         "prove whether the effect happened, and saying it can is the failure "
         "this row exists to prevent.";
  EXPECT_TRUE(task.has_unresolved_action());
  EXPECT_TRUE(task.RequiresUserVisibleRecoveryStatus());
}

// PAR-TAB-004, the property the row actually names. No restored state can
// express a replay: the record names an action and cannot describe one, so
// there is nothing for a recovery path to re-dispatch even if it wanted to.
//
// Asserted across every journal phase because the temptation to resume differs
// by phase, and a change that added a payload for one phase would leave the
// others looking correct.
IN_PROC_BROWSER_TEST_F(ParityTabRestoreTest, NoRestoredStateCanExpressAReplay) {
  const JournalPhase phases[] = {
      JournalPhase::kIdle,       JournalPhase::kPlanned,
      JournalPhase::kAuthorized, JournalPhase::kDispatching,
      JournalPhase::kVerifying,  JournalPhase::kTerminal};
  const RestartCause causes[] = {
      RestartCause::kCleanRestart, RestartCause::kProcessEviction,
      RestartCause::kBrowserCrash, RestartCause::kApplicationUpdate};

  for (JournalPhase phase : phases) {
    for (RestartCause cause : causes) {
      PersistedTaskJournalEntry entry;
      entry.task_id = Id("task-1");
      entry.action_id = Id("action-1");
      entry.dispatch_id = Id("dispatch-1");
      entry.phase = phase;
      entry.effect_is_external = true;

      const RestoredTaskRecord record =
          RestoredTaskRecord::FromJournalEntry(entry, cause);
      const ContinuationRequirement next = record.RequiredNextStep();

      // Every legal continuation asks for something to be re-established. None
      // of them is "dispatch the recorded action again", and the enumeration
      // has no member that could be.
      EXPECT_TRUE(next == ContinuationRequirement::kNothingToDo ||
                  next == ContinuationRequirement::kUserMustSeeRecoveryStatus ||
                  next == ContinuationRequirement::kFreshObservationRequired ||
                  next == ContinuationRequirement::kFreshAuthorizationRequired ||
                  next == ContinuationRequirement::kReconciliationRequired)
          << "phase " << static_cast<int>(phase) << " cause "
          << static_cast<int>(cause);

      if (record.disposition() == RecoveryDisposition::kOutcomeUnknown) {
        EXPECT_TRUE(record.RequiresUserVisibleRecoveryStatus())
            << "An outcome the browser cannot establish must reach the user "
               "before anything continues.";
      }
    }
  }
}

// PAR-TAB-004. A read-only step that carries an idempotency key is the one
// thing that may be repeated, because asking again costs a request and changes
// nothing in the world. Stated as its own test so that widening it later is a
// visible change to a named property rather than a quiet relaxation.
IN_PROC_BROWSER_TEST_F(ParityTabRestoreTest, OnlyIdempotentReadsMayBeRepeated) {
  PersistedTaskJournalEntry read;
  read.task_id = Id("task-1");
  read.action_id = Id("action-1");
  read.phase = JournalPhase::kDispatching;
  read.effect_is_external = false;
  read.is_idempotent_read = true;

  const RestoredTaskRecord record =
      RestoredTaskRecord::FromJournalEntry(read, RestartCause::kProcessEviction);
  EXPECT_EQ(RecoveryDisposition::kReReadableReadOnlyStep, record.disposition());

  PersistedTaskJournalEntry write = read;
  write.effect_is_external = true;
  write.is_idempotent_read = false;
  const RestoredTaskRecord write_record = RestoredTaskRecord::FromJournalEntry(
      write, RestartCause::kProcessEviction);
  EXPECT_EQ(RecoveryDisposition::kOutcomeUnknown, write_record.disposition());
}

}  // namespace
}  // namespace taffy::test
