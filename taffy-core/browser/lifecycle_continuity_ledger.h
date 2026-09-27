// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_LIFECYCLE_CONTINUITY_LEDGER_H_
#define TAFFY_BROWSER_LIFECYCLE_CONTINUITY_LEDGER_H_

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <set>
#include <string>

#include "taffy/browser/lifecycle_phase.h"
#include "taffy/components/intelligence/content/observability_recorder.h"
#include "taffy/common/public/bip_identity.h"

// The browser-process half of PAR-AND-005 and PAR-AND-007.
//
// Two properties, and they are different problems that happen to arrive
// through the same callbacks:
//
//   **No tab or task loss.** Tabs are Chromium's and survive a configuration
//   change on their own. Tasks are not: a task is paused by anything that
//   takes the browser out of the foreground, and the pause has to be reported
//   with a reason the user can read. That is what the verdict is for.
//
//   **No duplicate action.** This is the hard one. A rotation destroys and
//   recreates the Activity, and the recreated instance re-delivers the state
//   it saved. Anything that treats a re-delivery as a fresh instruction runs
//   twice, and the second run is a duplicate side effect nobody asked for —
//   the "tapped once, ordered twice" failure. The ledger prevents it with a
//   generation counter: every recreation increments it, a continuation carries
//   the generation it was created under, and one that carries an older
//   generation is refused rather than run. A continuation admitted once in a
//   generation is never admitted again in that generation either.
//
// The generation approach is chosen over deduplicating by identifier alone
// because the identifier is not enough: a legitimate repeat of the same
// operation in a later generation must be allowed, and a stale re-delivery of
// the same operation must not. Only the generation tells them apart.
//
// UI thread only.

namespace taffy {

struct LifecycleVerdict {
  // True only when the window is the foreground, interactive activity and no
  // recreation is outstanding.
  bool page_control_permitted = false;
  PageControlPauseReason pause_reason = PageControlPauseReason::kNotForeground;

  // True when the browser must have written its restorable state by now. Set
  // for every phase from which the process may not come back.
  bool state_must_be_preserved = false;

  // True between a destroy-for-recreation and the next resume.
  bool recreation_in_progress = false;

  // Increments on every recreation. A continuation created under an earlier
  // value is stale.
  uint64_t generation = 0;

  friend bool operator==(const LifecycleVerdict&,
                         const LifecycleVerdict&) = default;
};

// What a continuation is asking to do, and when it was created. Bounded and
// content-free for the same reason RestoredTaskRecord is: a key that could
// hold an action payload would be a payload something eventually replays.
struct ContinuationKey {
  RecordIdentifier tab_id;
  // Names the operation. It does not describe one, and nothing in this struct
  // could.
  RecordIdentifier operation_id;
  uint64_t generation = 0;
};

enum class ContinuationAdmission : uint8_t {
  // First time in this generation. Run it.
  kFirstAdmission = 0,
  // Already run in this generation. This is the re-delivery case, and refusing
  // it is the whole point of the ledger.
  kAlreadyAdmitted = 1,
  // Created before a recreation. Whatever it was going to do belongs to a
  // browser state that no longer exists.
  kRefusedStaleGeneration = 2,
  // Created claiming a generation that has not happened yet.
  kRefusedFutureGeneration = 3,
  kRefusedInvalidKey = 4,
};

class LifecycleContinuityLedger {
 public:
  LifecycleContinuityLedger();
  LifecycleContinuityLedger(const LifecycleContinuityLedger&) = delete;
  LifecycleContinuityLedger& operator=(const LifecycleContinuityLedger&) =
      delete;
  ~LifecycleContinuityLedger();

  // Reports a phase transition for one window and returns the verdict that
  // follows from it.
  LifecycleVerdict NotePhase(const BrowserWindowId& window_id,
                             ActivityLifecyclePhase phase);

  // Reports a configuration change. On Android a change the activity does not
  // declare produces a destroy and recreate, which arrives separately as
  // phases; a change it does declare arrives only here. Either way the
  // generation advances, because in both cases the UI re-delivers its state.
  LifecycleVerdict NoteConfigurationChange(const BrowserWindowId& window_id,
                                           ConfigurationChangeKind changes);

  // The device lock is process-wide rather than per window.
  void SetDeviceLocked(bool locked);

  LifecycleVerdict CurrentVerdict(const BrowserWindowId& window_id) const;

  uint64_t generation(const BrowserWindowId& window_id) const;

  // The duplicate-action guard. Admitting is a state change: a key admitted
  // once is refused every time afterwards within its generation.
  ContinuationAdmission Admit(const BrowserWindowId& window_id,
                              const ContinuationKey& key);

  // Diagnostic. Bounded by construction; see kMaxAdmittedPerWindow in the
  // implementation.
  size_t admitted_count(const BrowserWindowId& window_id) const;

  void ForgetWindow(const BrowserWindowId& window_id);

 private:
  struct WindowState {
    ActivityLifecyclePhase phase = ActivityLifecyclePhase::kUnknown;
    bool recreation_in_progress = false;
    uint64_t generation = 0;
    ConfigurationChangeKind last_changes = ConfigurationChangeKind::kNone;
    // Admitted operation identifiers for the current generation. Cleared when
    // the generation advances, which is what bounds it in practice.
    std::set<std::string> admitted_operations;
  };

  LifecycleVerdict VerdictFor(const WindowState& state) const;
  WindowState& StateFor(const BrowserWindowId& window_id);

  bool device_locked_ = false;
  std::map<BrowserWindowId, WindowState> windows_;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_LIFECYCLE_CONTINUITY_LEDGER_H_
