// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_TAB_SESSION_REGISTRY_H_
#define TAFFY_BROWSER_TAB_SESSION_REGISTRY_H_

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <set>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "taffy/browser/session_recovery_planner.h"
#include "taffy/browser/tab_record.h"
#include "taffy/common/public/bip_identity.h"
#include "ui/base/window_open_disposition.h"

// The browser-owned tab and session record
// (PAR-TAB-001 create, list, switch and close; PAR-TAB-003 restore after a
// clean restart; PAR-TAB-004 recovery after eviction; PAR-NAV-003 opening a
// link in the current or a new tab).
//
// It is a registry, not a tab model. Chromium's tab model creates, orders,
// persists and destroys tabs; this class records what TaffyGo needs to know
// about them and enforces the rules TaffyGo adds on top. The split is what
// keeps //taffy from reaching into //chrome: every operation that
// actually moves a tab goes out through TabSessionDelegate, which the browser
// layer implements.
//
// Three rules live here, and each one is refused in this class rather than
// checked at a call site:
//
//   1. **A tab identifier is never reused.** Closed identifiers are retired
//      permanently (bip_identity.h). A platform that handed the same
//      identifier back would otherwise let a stale handle name a live tab.
//   2. **The assistant cannot close or steal focus from the user's tab.** The
//      refusal is by ownership and by the origin of the request, not by who
//      called; a compromised renderer and a confused call site get the same
//      answer. This is the M1 half of PAR-TAB-002, whose M4 scope adds
//      provenance retention on cleanup.
//   3. **An unattributed request is refused.** Fail closed is the same rule
//      the download and external-intent seam uses
//      (public/taffy_download_intent.h), for the same reason.
//
// Everything here works with the AI runtime absent: the registry has no
// dependency on it, and TabOwnership::kAssistant simply never appears in a
// session where nothing ever asked for it (PAR-AI-BR-001).
//
// UI thread only.

namespace taffy {

// Who asked. Ownership decides what a tab is; this decides who is asking about
// it, and the pair is what the refusals turn on.
enum class TabRequestOrigin : uint8_t {
  kUnknown = 0,
  kUserGesture = 1,
  kAssistantTask = 2,
  kBrowserTeardown = 3,
  // A restore path replaying persisted state. Never treated as a user gesture.
  kSessionRestore = 4,
};

struct TabOpenRequest {
  ProfileId profile_id;
  BrowserWindowId window_id;
  TabRequestOrigin request_origin = TabRequestOrigin::kUnknown;
  TabOwnership ownership = TabOwnership::kUnknown;

  // The tab the link was activated in, when there is one. PAR-NAV-003.
  TabId opener_tab_id;

  // Only CURRENT_TAB, NEW_FOREGROUND_TAB and NEW_BACKGROUND_TAB are supported
  // at M1; anything else is refused rather than approximated.
  WindowOpenDisposition disposition = WindowOpenDisposition::CURRENT_TAB;

  // Required when request_origin is kAssistantTask.
  TaskId owning_task_id;
};

enum class TabOpenResult : uint8_t {
  kOpened = 0,
  kRefusedNoDelegate = 1,
  kRefusedUnattributedOrigin = 2,
  kRefusedUnsupportedDisposition = 3,
  // An assistant request that names no task has no scope to be inside.
  kRefusedAssistantOutsideTaskScope = 4,
  kPlatformRefused = 5,
};

enum class TabActivateResult : uint8_t {
  kActivated = 0,
  kRefusedNoDelegate = 1,
  kRefusedUnknownTab = 2,
  kRefusedUnattributedOrigin = 3,
  // The assistant may bring its own tab forward and may not take focus away
  // from what the user is reading.
  kRefusedAssistantMayNotStealFocus = 4,
  kPlatformRefused = 5,
  kAlreadyActive = 6,
};

enum class TabCloseResult : uint8_t {
  kClosed = 0,
  kRefusedNoDelegate = 1,
  kRefusedUnknownTab = 2,
  kRefusedUnattributedOrigin = 3,
  kRefusedAssistantMayNotCloseUserTab = 4,
  kPlatformRefused = 5,
};

// Implemented by the browser layer over Chromium's tab model. Every method
// performs; none of them decides. The registry has already refused anything
// that should not reach here.
class TabSessionDelegate {
 public:
  virtual ~TabSessionDelegate() = default;

  // Returns the identifier the platform assigned, or an invalid TabId when the
  // platform refused. The registry never mints an identifier itself: two
  // allocators would eventually issue the same one.
  virtual TabId OpenTab(const TabOpenRequest& request) = 0;

  virtual bool ActivateTab(const TabId& tab_id) = 0;
  virtual bool CloseTab(const TabId& tab_id) = 0;
};

class TabSessionRegistry {
 public:
  TabSessionRegistry();
  TabSessionRegistry(const TabSessionRegistry&) = delete;
  TabSessionRegistry& operator=(const TabSessionRegistry&) = delete;
  ~TabSessionRegistry();

  // Null clears it. With no delegate every mutating call is refused and every
  // query answers from the record, which is what makes the registry safe to
  // consult during startup and during teardown.
  void SetDelegate(TabSessionDelegate* delegate);

  TabOpenResult OpenTab(const TabOpenRequest& request, TabId* opened_tab_id);
  TabActivateResult ActivateTab(const TabId& tab_id,
                                TabRequestOrigin request_origin);
  TabCloseResult CloseTab(const TabId& tab_id, TabCloseOrigin close_origin);

  // In window then index order, so a caller never has to sort and two callers
  // cannot disagree about the order.
  std::vector<TabRecord> ListTabs() const;

  const TabRecord* FindTab(const TabId& tab_id) const;
  const TabId& active_tab_id() const { return active_tab_id_; }
  size_t tab_count() const { return tabs_.size(); }

  // True when this identifier named a tab that has been closed. A retired
  // identifier is never issued again, so a handle carrying one fails closed
  // rather than naming somebody else's tab.
  bool IsRetired(const TabId& tab_id) const;

  // Seeds the registry from a recovery plan (PAR-TAB-003, PAR-TAB-004). The
  // caller has already asked Chromium to restore the tabs; this records what
  // came back and applies the plan's focus decision. Discarded directives are
  // not adopted, and a directive naming a retired identifier is skipped.
  void AdoptRecoveryPlan(const SessionRecoveryPlan& plan,
                         const ProfileId& profile_id,
                         const BrowserWindowId& window_id);

  // For the platform half to report a tab that appeared or went away outside a
  // registry call — the user closed it from the tab switcher, or Chromium
  // discarded it under memory pressure.
  void NoteTabOpenedByPlatform(const TabRecord& record);
  void NoteTabClosedByPlatform(const TabId& tab_id);
  void NoteTabActivatedByPlatform(const TabId& tab_id);

 private:
  void Insert(const TabRecord& record);
  void Retire(const TabId& tab_id);

  raw_ptr<TabSessionDelegate> delegate_ = nullptr;
  std::map<TabId, TabRecord> tabs_;
  std::set<TabId> retired_tab_ids_;
  TabId active_tab_id_;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_TAB_SESSION_REGISTRY_H_
