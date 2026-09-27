// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/session_recovery_planner.h"

#include <string_view>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

// PAR-TAB-003 (restore after a clean restart) and PAR-TAB-004 (recovery after
// eviction), as a pure function over persisted state.

namespace taffy {
namespace {

std::string_view AsView(const RecordIdentifier& identifier) {
  return std::string_view(identifier.chars.data());
}

PersistedTabRecord UserTab(const char* id, size_t index, bool active) {
  PersistedTabRecord tab;
  tab.tab_id = ToRecordIdentifier(id);
  tab.index = index;
  tab.was_active = active;
  tab.ownership = TabOwnership::kUser;
  return tab;
}

PersistedTabRecord AssistantTab(const char* id,
                                size_t index,
                                bool active,
                                const char* task_id) {
  PersistedTabRecord tab;
  tab.tab_id = ToRecordIdentifier(id);
  tab.index = index;
  tab.was_active = active;
  tab.ownership = TabOwnership::kAssistant;
  if (task_id) {
    tab.owning_task_id = ToRecordIdentifier(task_id);
    tab.has_owning_task = true;
  }
  return tab;
}

PersistedTaskJournalEntry Journal(const char* task_id, JournalPhase phase) {
  PersistedTaskJournalEntry entry;
  entry.task_id = ToRecordIdentifier(task_id);
  entry.action_id = ToRecordIdentifier("action_1");
  entry.phase = phase;
  return entry;
}

TEST(SessionRecoveryPlannerTest, CleanRestartKeepsOrderAndActiveTab) {
  const std::vector<PersistedTabRecord> tabs = {
      UserTab("tab_c", 2, false),
      UserTab("tab_a", 0, false),
      UserTab("tab_b", 1, true),
  };

  SessionRecoveryPlan plan =
      PlanSessionRecovery(RestartCause::kCleanRestart, tabs, {});

  ASSERT_EQ(3u, plan.tabs.size());
  EXPECT_EQ(std::string_view("tab_a"), AsView(plan.tabs[0].tab_id));
  EXPECT_EQ(std::string_view("tab_b"), AsView(plan.tabs[1].tab_id));
  EXPECT_EQ(std::string_view("tab_c"), AsView(plan.tabs[2].tab_id));
  EXPECT_EQ(3u, plan.restored_user_tab_count);

  ASSERT_TRUE(plan.active_tab_id.has_value());
  EXPECT_EQ(std::string_view("tab_b"), AsView(*plan.active_tab_id));
  EXPECT_TRUE(plan.tabs[1].activate);
  EXPECT_FALSE(plan.tabs[0].activate);
  EXPECT_FALSE(plan.user_must_be_shown_recovery_status);
}

TEST(SessionRecoveryPlannerTest, UnattributedTabIsRestoredAsTheUsers) {
  PersistedTabRecord unattributed;
  unattributed.tab_id = ToRecordIdentifier("tab_x");
  unattributed.ownership = TabOwnership::kUnknown;

  SessionRecoveryPlan plan =
      PlanSessionRecovery(RestartCause::kCleanRestart, {unattributed}, {});

  ASSERT_EQ(1u, plan.tabs.size());
  EXPECT_EQ(TabRestoreAction::kRestoreAsUserTab, plan.tabs[0].action);
  EXPECT_EQ(1u, plan.restored_user_tab_count);
}

TEST(SessionRecoveryPlannerTest, AssistantTabNeverTakesFocusAfterRecovery) {
  // The persisted active tab was the assistant's. Handing the user a page the
  // assistant opened, as the first thing they see after a crash, is how a
  // browser teaches people not to trust it.
  const std::vector<PersistedTabRecord> tabs = {
      UserTab("tab_user", 0, false),
      AssistantTab("tab_assistant", 1, true, "task_1"),
  };
  const std::vector<PersistedTaskJournalEntry> journal = {
      Journal("task_1", JournalPhase::kDispatching)};

  SessionRecoveryPlan plan =
      PlanSessionRecovery(RestartCause::kProcessEviction, tabs, journal);

  ASSERT_TRUE(plan.active_tab_id.has_value());
  EXPECT_EQ(std::string_view("tab_user"), AsView(*plan.active_tab_id));
  EXPECT_EQ(TabRestoreAction::kRestoreAsAssistantTabPaused,
            plan.tabs[1].action);
  EXPECT_FALSE(plan.tabs[1].activate);
  EXPECT_EQ(1u, plan.paused_assistant_tab_count);
}

TEST(SessionRecoveryPlannerTest, AssistantTabWithNoLiveTaskIsDiscarded) {
  const std::vector<PersistedTabRecord> tabs = {
      UserTab("tab_user", 0, true),
      AssistantTab("tab_assistant", 1, false, "task_done"),
  };
  const std::vector<PersistedTaskJournalEntry> journal = {
      Journal("task_done", JournalPhase::kTerminal)};

  SessionRecoveryPlan plan =
      PlanSessionRecovery(RestartCause::kCleanRestart, tabs, journal);

  EXPECT_EQ(TabRestoreAction::kDiscardTemporaryTab, plan.tabs[1].action);
  EXPECT_EQ(1u, plan.discarded_tab_count);
  // Discarding a tab is not deleting a record: provenance lives in the audit
  // engine and this plan never touches it (PAR-TAB-002).
  EXPECT_FALSE(plan.user_must_be_shown_recovery_status);
}

TEST(SessionRecoveryPlannerTest, AnInterruptedTaskForcesRecoveryStatus) {
  const std::vector<PersistedTaskJournalEntry> journal = {
      Journal("task_1", JournalPhase::kVerifying)};

  SessionRecoveryPlan plan = PlanSessionRecovery(
      RestartCause::kProcessEviction, {UserTab("tab_a", 0, true)}, journal);

  EXPECT_TRUE(plan.user_must_be_shown_recovery_status);
  ASSERT_EQ(1u, plan.tasks.size());
  EXPECT_EQ(RecoveryDisposition::kOutcomeUnknown, plan.tasks[0].disposition());
  EXPECT_EQ(ContinuationRequirement::kReconciliationRequired,
            plan.tasks[0].RequiredNextStep());
}

TEST(SessionRecoveryPlannerTest, NoUserTabLeavesFocusToTheBrowser) {
  const std::vector<PersistedTabRecord> tabs = {
      AssistantTab("tab_assistant", 0, true, "task_1")};
  const std::vector<PersistedTaskJournalEntry> journal = {
      Journal("task_1", JournalPhase::kPlanned)};

  SessionRecoveryPlan plan =
      PlanSessionRecovery(RestartCause::kBrowserCrash, tabs, journal);

  // Absent, not "the assistant's tab". Chromium opens its ordinary new tab,
  // which this planner does not replace.
  EXPECT_FALSE(plan.active_tab_id.has_value());
  EXPECT_EQ(0u, plan.restored_user_tab_count);
}

TEST(SessionRecoveryPlannerTest, EmptySessionPlansNothing) {
  SessionRecoveryPlan plan =
      PlanSessionRecovery(RestartCause::kCleanRestart, {}, {});

  EXPECT_TRUE(plan.tabs.empty());
  EXPECT_TRUE(plan.tasks.empty());
  EXPECT_FALSE(plan.active_tab_id.has_value());
  EXPECT_FALSE(plan.user_must_be_shown_recovery_status);
}

TEST(SessionRecoveryPlannerTest, ThePlanIsIndependentOfArgumentOrder) {
  // Purity in the sense that matters here: the same session produces the same
  // plan no matter what order the persistence layer hands the tabs back in.
  const std::vector<PersistedTabRecord> forward = {
      UserTab("tab_a", 0, true), UserTab("tab_b", 1, false),
      UserTab("tab_c", 2, false)};
  const std::vector<PersistedTabRecord> reversed = {
      UserTab("tab_c", 2, false), UserTab("tab_b", 1, false),
      UserTab("tab_a", 0, true)};

  SessionRecoveryPlan first =
      PlanSessionRecovery(RestartCause::kCleanRestart, forward, {});
  SessionRecoveryPlan second =
      PlanSessionRecovery(RestartCause::kCleanRestart, reversed, {});

  ASSERT_EQ(first.tabs.size(), second.tabs.size());
  for (size_t i = 0; i < first.tabs.size(); ++i) {
    EXPECT_EQ(AsView(first.tabs[i].tab_id), AsView(second.tabs[i].tab_id));
    EXPECT_EQ(first.tabs[i].activate, second.tabs[i].activate);
  }
}

}  // namespace
}  // namespace taffy
