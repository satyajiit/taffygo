// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_COMPONENTS_FILTERING_BROWSER_FILTERING_RULESET_SERVICE_H_
#define TAFFY_COMPONENTS_FILTERING_BROWSER_FILTERING_RULESET_SERVICE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/callback_list.h"
#include "base/containers/flat_set.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "components/prefs/pref_change_registrar.h"
#include "taffy/components/filtering/core/posture.h"
#include "taffy/components/filtering/core/ruleset.h"

class PrefService;

namespace taffy::filtering {

// One loaded matcher, shared immutably across request threads. A throttle
// holds a reference for the life of one request, so a ruleset swap never
// changes a verdict mid-request and never frees bytes under a reader.
class SharedRuleset : public base::RefCountedThreadSafe<SharedRuleset> {
 public:
  explicit SharedRuleset(std::unique_ptr<FilterRulesetMatcher> matcher);

  const FilterRulesetMatcher& matcher() const { return *matcher_; }

 private:
  friend class base::RefCountedThreadSafe<SharedRuleset>;
  ~SharedRuleset();

  const std::unique_ptr<FilterRulesetMatcher> matcher_;
};

// The profile-scoped lifecycle of the filtering plane: preference-backed
// posture, ruleset compilation off the asset store's bytes, the lifetime
// blocked total, and one fan-out change signal for every live projection.
// Lives on the UI sequence; owned beside the asset plane by the profile's
// core-service manager.
//
// The service reads list bytes through an injected callback rather than the
// asset plane directly, because this component sits below the browser host in
// the layer graph: the host injects a reader over the installed asset, and
// this class owns everything after the bytes arrive.
class FilteringRulesetService {
 public:
  // The displayed site number is a conservative lower bound. Once this many
  // distinct valid hosts have been retained, further identities are not kept.
  static constexpr size_t kWeekSiteLowerBoundCapacity = 1024;

  // Hands back the concatenated rule-set text, or nullopt while the asset is
  // not installed. Invoked on the UI sequence; may answer asynchronously.
  using ListReader = base::RepeatingCallback<void(
      base::OnceCallback<void(std::optional<std::string>)>)>;

  FilteringRulesetService(PrefService* prefs, ListReader list_reader);
  ~FilteringRulesetService();
  FilteringRulesetService(const FilteringRulesetService&) = delete;
  FilteringRulesetService& operator=(const FilteringRulesetService&) = delete;

  // Called when the installed asset set changes and once at construction:
  // reads the list, compiles it on the worker pool, and publishes the matcher
  // when both finish. A failed read leaves the previous matcher standing —
  // rules that were good a minute ago beat no rules at all.
  void ReloadRuleset();

  // The posture snapshot every request decision reads. Built once and replaced
  // only by the preference callbacks below: reconstructing and sorting the
  // exception set on every subresource request makes page-load work depend on
  // the number of stored exceptions. UI-sequence callers must not retain the
  // reference across re-entry.
  const FilteringPosture& posture() const;
  uint64_t posture_revision() const;

  // The current matcher, or null while no ruleset has compiled. Null means
  // filtering stands ready with no rules: nothing is blocked and nothing is
  // claimed.
  scoped_refptr<const SharedRuleset> ruleset() const;

  // The person's commands, from the site sheet and the settings screen. Both
  // write preferences; the registrar refreshes the posture and notifies.
  void SetFilteringEnabled(bool enabled);
  // Returns false for a host this seam refuses to record: empty, over the
  // contract's bound, or carrying a scheme or path where a host belongs.
  bool SetSiteException(std::string_view host, bool allow);

  // One blocked request's worth of accounting. UI sequence; the per-tab count
  // lives with the tab, this is only the lifetime total, flushed to the
  // preference at most every thirty seconds and at teardown — an undercount
  // after a crash is acceptable, a preference write per request is not.
  void NoteBlocked();

  // One blocked request inside the week window. UI sequence; a private tab
  // must not call this. [host] is the document host, recorded once per
  // distinct value until the conservative lower-bound capacity is reached;
  // an empty or unrecordable host still increments the week count. The
  // window is seven days from its start and then resets.
  void NoteWeekBlocked(std::string_view host);

  // The lifetime total, including what has not flushed yet.
  int64_t blocked_total() const;

  // The week-window totals, rolling an existing window first so a stale
  // seven-day span is not reported as current. The site value is deliberately
  // named as a lower bound: after the retained identity capacity is reached,
  // new identities are not stored. Reading never starts a window: zero
  // week_start stays "not counted". Not const: a roll writes preferences.
  int64_t blocked_this_week();
  int minimum_sites_this_week();
  // True only after a week window has started (a judged request, or a
  // roll of a window that already existed). Reading does not mint one.
  bool has_week_window() const;

  // A tab's coalesced count publication, fanned into the one changed signal.
  // The per-tab counter calls this so a projection holding the profile can
  // refresh and re-read every tab's count, without a per-tab subscription
  // whose lifetime it would then have to manage.
  void NoteTabCountPublished();

  // Registers one reason-to-reread signal for posture, ruleset and
  // published-count changes. Every live subscriber is notified. Destroying the
  // returned subscription removes only that registration, including during a
  // notification. A callback may close its own projection, but must not
  // synchronously destroy this profile-scoped service while notification is in
  // progress.
  base::CallbackListSubscription AddChangedCallback(
      base::RepeatingClosure changed);

  base::WeakPtr<FilteringRulesetService> GetWeakPtr();

 private:
  void OnListRead(std::optional<std::string> list_text);
  void OnCompiled(CompiledRuleset compiled);
  void OnPosturePrefChanged();
  void FlushTotal();
  void NotifyChanged();
  void EnsureWeekWindow();
  void MaybeRollWeekWindow();
  void RecordWeekSite(std::string_view host);

  SEQUENCE_CHECKER(sequence_checker_);
  const raw_ptr<PrefService> prefs_;
  const ListReader list_reader_;
  PrefChangeRegistrar pref_registrar_;
  scoped_refptr<const SharedRuleset> ruleset_;
  FilteringPosture posture_;
  uint64_t posture_revision_ = 1;
  base::RepeatingClosureList changed_callbacks_;
  int64_t unflushed_blocked_ = 0;
  int64_t unflushed_week_blocked_ = 0;
  base::flat_set<std::string> week_site_lower_bound_;
  bool week_sites_dirty_ = false;
  base::OneShotTimer flush_timer_;
  bool load_in_flight_ = false;
  bool reload_queued_ = false;
  base::WeakPtrFactory<FilteringRulesetService> weak_factory_{this};
};

}  // namespace taffy::filtering

#endif  // TAFFY_COMPONENTS_FILTERING_BROWSER_FILTERING_RULESET_SERVICE_H_
