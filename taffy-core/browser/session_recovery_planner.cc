// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/session_recovery_planner.h"

#include <algorithm>
#include <string_view>

namespace taffy {

namespace {

std::string_view AsView(const RecordIdentifier& identifier) {
  return std::string_view(identifier.chars.data());
}

bool SameIdentifier(const RecordIdentifier& left,
                    const RecordIdentifier& right) {
  return AsView(left) == AsView(right);
}

// True when the journal still has something outstanding for this task. Any
// disposition other than "nothing was running" counts, because an interrupted
// task is exactly what keeps its tab from coming back as an ordinary one.
bool TaskIsIncomplete(const std::vector<RestoredTaskRecord>& tasks,
                      const RecordIdentifier& task_id) {
  for (const RestoredTaskRecord& task : tasks) {
    if (SameIdentifier(task.task_id(), task_id)) {
      return task.disposition() != RecoveryDisposition::kNoTaskWasRunning;
    }
  }
  return false;
}

TabRestoreAction ActionFor(const PersistedTabRecord& tab,
                           const std::vector<RestoredTaskRecord>& tasks) {
  switch (tab.ownership) {
    case TabOwnership::kUser:
      return TabRestoreAction::kRestoreAsUserTab;

    // Fail closed. An unattributed tab is restored as the user's, because the
    // rules that protect the user's tabs are the stricter ones and misfiling
    // one of the assistant's as the user's costs a stray tab, while the
    // reverse costs the user a page they were reading.
    case TabOwnership::kUnknown:
      return TabRestoreAction::kRestoreAsUserTab;

    case TabOwnership::kAssistant:
      if (tab.has_owning_task && TaskIsIncomplete(tasks, tab.owning_task_id)) {
        return TabRestoreAction::kRestoreAsAssistantTabPaused;
      }
      return TabRestoreAction::kDiscardTemporaryTab;
  }
}

}  // namespace

SessionRecoveryPlan PlanSessionRecovery(
    RestartCause cause,
    const std::vector<PersistedTabRecord>& tabs,
    const std::vector<PersistedTaskJournalEntry>& journal) {
  SessionRecoveryPlan plan;

  plan.tasks.reserve(journal.size());
  for (const PersistedTaskJournalEntry& entry : journal) {
    RestoredTaskRecord task =
        RestoredTaskRecord::FromJournalEntry(entry, cause);
    if (task.RequiresUserVisibleRecoveryStatus()) {
      plan.user_must_be_shown_recovery_status = true;
    }
    plan.tasks.push_back(task);
  }

  // Persisted order is the restored order (PAR-TAB-003). Sorting a copy keeps
  // the planner pure with respect to its argument.
  std::vector<PersistedTabRecord> ordered = tabs;
  std::stable_sort(ordered.begin(), ordered.end(),
                   [](const PersistedTabRecord& left,
                      const PersistedTabRecord& right) {
                     return left.index < right.index;
                   });

  plan.tabs.reserve(ordered.size());
  std::optional<RecordIdentifier> persisted_active;
  std::optional<RecordIdentifier> first_user_tab;

  for (const PersistedTabRecord& tab : ordered) {
    TabRestoreDirective directive;
    directive.tab_id = tab.tab_id;
    directive.index = tab.index;
    directive.action = ActionFor(tab, plan.tasks);

    switch (directive.action) {
      case TabRestoreAction::kRestoreAsUserTab:
        ++plan.restored_user_tab_count;
        if (!first_user_tab.has_value()) {
          first_user_tab = tab.tab_id;
        }
        if (tab.was_active) {
          persisted_active = tab.tab_id;
        }
        break;
      case TabRestoreAction::kRestoreAsAssistantTabPaused:
        ++plan.paused_assistant_tab_count;
        break;
      case TabRestoreAction::kDiscardTemporaryTab:
        ++plan.discarded_tab_count;
        break;
    }

    plan.tabs.push_back(directive);
  }

  // Focus. The persisted active tab wins, unless it was the assistant's:
  // handing the user a page the assistant opened, as the first thing they see
  // after a crash, is how a browser teaches people not to trust it. Focus then
  // falls to the first restored user tab, and to nothing at all when there is
  // none — at which point Chromium opens its ordinary new tab, which is
  // behavior this planner does not replace.
  plan.active_tab_id =
      persisted_active.has_value() ? persisted_active : first_user_tab;

  if (plan.active_tab_id.has_value()) {
    for (TabRestoreDirective& directive : plan.tabs) {
      directive.activate = SameIdentifier(directive.tab_id, *plan.active_tab_id);
    }
  }

  return plan;
}

}  // namespace taffy
