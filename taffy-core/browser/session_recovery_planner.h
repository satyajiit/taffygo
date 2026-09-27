// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_SESSION_RECOVERY_PLANNER_H_
#define TAFFY_BROWSER_SESSION_RECOVERY_PLANNER_H_

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <vector>

#include "taffy/components/intelligence/content/observability_recorder.h"
#include "taffy/browser/restored_task_record.h"
#include "taffy/browser/tab_record.h"

// What TaffyGo decides when a session comes back
// (PAR-TAB-003 restore after a clean restart, PAR-TAB-004 recovery after OS
// process eviction or crash, REQ-BR-003).
//
// This planner is deliberately small, because most of session restore is not
// TaffyGo's to decide. Chromium's tab persistence owns the URLs, the history
// entries, the scroll offsets and the mechanics of bringing a WebContents
// back; duplicating any of that here would create a second source of truth
// that drifts at the first upstream rebase. What Chromium does not know, and
// what this planner owns, is:
//
//   1. which restored tabs are the user's and which are the assistant's, so
//      that a restart never hands the user a page the assistant opened as
//      though the user had chosen it;
//   2. which tasks were incomplete, and what each of them is allowed to do
//      next — always through a RestoredTaskRecord, which cannot express a
//      replay (restored_task_record.h);
//   3. whether the user has to be shown recovery status before anything
//      continues.
//
// It is a pure function. Given the same persisted state and the same restart
// cause it produces the same plan on any host, which is what lets the recovery
// rules be tested without a device and without a crash.

namespace taffy {

// One persisted tab, in the bounded form the planner consumes. Content-free
// for the same reason RestoredTaskRecord is: a persisted structure that could
// hold a page is a persisted structure something will eventually read as one.
struct PersistedTabRecord {
  RecordIdentifier tab_id;
  size_t index = 0;
  bool was_active = false;
  TabOwnership ownership = TabOwnership::kUnknown;

  // Set when an incomplete task claimed this tab.
  RecordIdentifier owning_task_id;
  bool has_owning_task = false;
};

// What to do with one persisted tab.
enum class TabRestoreAction : uint8_t {
  // The ordinary case. Chromium restores it and the user sees what they left.
  kRestoreAsUserTab = 0,
  // An assistant tab whose task did not finish. It comes back visible and
  // inert: the task attached to it is interrupted, so nothing about the tab
  // may resume on its own.
  kRestoreAsAssistantTabPaused = 1,
  // An assistant tab with no incomplete task behind it. Nothing is lost by
  // letting it go, and leaving it would accumulate tabs nobody opened.
  // Provenance records are not touched by this: they live in the audit engine
  // and a tab is not a record (PAR-TAB-002).
  kDiscardTemporaryTab = 2,
};

struct TabRestoreDirective {
  RecordIdentifier tab_id;
  TabRestoreAction action = TabRestoreAction::kRestoreAsUserTab;
  // Position to restore at, preserving the persisted order (PAR-TAB-003).
  size_t index = 0;
  bool activate = false;
};

struct SessionRecoveryPlan {
  std::vector<TabRestoreDirective> tabs;
  std::vector<RestoredTaskRecord> tasks;

  // The tab that receives focus. Absent when the session had no restorable
  // user tab, in which case the browser opens its ordinary new tab — that is
  // Chromium's behavior and this planner does not replace it.
  std::optional<RecordIdentifier> active_tab_id;

  // True when at least one task was interrupted. The user sees recovery status
  // and controls before anything continues (system architecture section 11.3).
  bool user_must_be_shown_recovery_status = false;

  // Diagnostic counts, so a parity test can assert on the shape of a plan
  // without walking it.
  size_t restored_user_tab_count = 0;
  size_t paused_assistant_tab_count = 0;
  size_t discarded_tab_count = 0;
};

// Pure. `tabs` need not be sorted; the plan is emitted in persisted index
// order regardless.
SessionRecoveryPlan PlanSessionRecovery(
    RestartCause cause,
    const std::vector<PersistedTabRecord>& tabs,
    const std::vector<PersistedTaskJournalEntry>& journal);

}  // namespace taffy

#endif  // TAFFY_BROWSER_SESSION_RECOVERY_PLANNER_H_
