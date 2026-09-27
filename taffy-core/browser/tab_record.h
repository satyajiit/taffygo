// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_TAB_RECORD_H_
#define TAFFY_BROWSER_TAB_RECORD_H_

#include <stdint.h>

#include <stddef.h>

#include "taffy/common/public/bip_identity.h"

// What the browser process knows about one tab, in the vocabulary the rest of
// //taffy speaks (PAR-TAB-001 create, list, switch and close;
// PAR-TAB-003 restore after a clean restart).
//
// This is a record, not a tab. Chromium's tab model owns the tab: its
// WebContents, its ordering, its persistence and its restore. This struct is
// the projection the assistant-facing code is allowed to see, and it exists so
// that no code outside TabSessionRegistry holds a WebContents pointer or a
// Chromium tab handle. A pointer that escaped would be a pointer somebody
// eventually dereferences after the tab closed.
//
// Deliberately absent: the page title, the favicon, the page URL. Title and
// favicon are page-controlled bytes and belong to the document, not to a
// browser-owned record; the URL has one authority already, the committed
// navigation record (browser_navigation_record.h), and a second copy here
// would drift the moment a redirect committed between the two writes.

namespace taffy {

// Who the tab belongs to. The distinction is required at M1 by REQ-BR-003 — a
// restored session must distinguish normal tabs, the assistant's tabs, and
// incomplete tasks — and it is what PAR-TAB-002 builds the M4 ownership rules
// on top of.
enum class TabOwnership : uint8_t {
  // Fail closed. An unattributed tab is treated as the user's, because the
  // rules that protect the user's tabs are the stricter ones.
  kUnknown = 0,
  kUser = 1,
  // Opened by the assistant inside an approved task scope. Never a weaker
  // security class than a user tab; only a differently owned one.
  kAssistant = 2,
};

// Why a tab is being closed. The registry refuses some combinations of origin
// and ownership outright, which is why this is a parameter rather than a
// comment at the call site.
enum class TabCloseOrigin : uint8_t {
  kUnknown = 0,
  kUserGesture = 1,
  kAssistantCleanup = 2,
  // The browser is shutting the profile or window down. Ownership does not
  // restrict this one.
  kBrowserTeardown = 3,
};

struct TabRecord {
  TabId tab_id;
  ProfileId profile_id;
  BrowserWindowId window_id;

  // Position within its window, as the tab model reports it. Stored so that
  // ordering survives a restore (PAR-TAB-003); never used to derive identity.
  size_t index = 0;

  bool is_active = false;
  TabOwnership ownership = TabOwnership::kUnknown;

  // Set when an incomplete task claims this tab. Restore uses it to mark the
  // task interrupted rather than resuming it (PAR-TAB-004).
  TaskId owning_task_id;

  friend bool operator==(const TabRecord&, const TabRecord&) = default;

  bool is_valid() const { return tab_id.is_valid(); }
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_TAB_RECORD_H_
