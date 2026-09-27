// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/components/filtering/browser/filtering_ruleset_service.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/values.h"
#include "components/prefs/pref_service.h"
#include "taffy/components/filtering/browser/filtering_prefs.h"

namespace taffy::filtering {

namespace {

// The browsing contract's MAX_HOST_BYTES: the longest host this seam records.
constexpr size_t kMaxExceptionHostBytes = 253;

// How long the lifetime total may sit unflushed.
constexpr base::TimeDelta kTotalFlushInterval = base::Seconds(30);

// The week window is seven days from its recorded start, then it resets.
constexpr base::TimeDelta kWeekWindow = base::Days(7);

int64_t TimeToPref(base::Time time) {
  return time.ToDeltaSinceWindowsEpoch().InMicroseconds();
}

base::Time TimeFromPref(int64_t microseconds) {
  return base::Time::FromDeltaSinceWindowsEpoch(
      base::Microseconds(microseconds));
}

bool IsRecordableHost(std::string_view host) {
  if (host.empty() || host.size() > kMaxExceptionHostBytes) {
    return false;
  }
  for (const char byte : host) {
    const bool allowed = (byte >= 'a' && byte <= 'z') ||
                         (byte >= '0' && byte <= '9') || byte == '.' ||
                         byte == '-';
    if (!allowed) {
      return false;
    }
  }
  return host.front() != '.' && host.back() != '.';
}

FilteringPosture ReadPosture(const PrefService& prefs) {
  const base::ListValue& entries = prefs.GetList(kFilteringSiteExceptionsPref);
  std::vector<std::string> hosts;
  hosts.reserve(entries.size());
  for (const base::Value& entry : entries) {
    if (entry.is_string()) {
      hosts.push_back(entry.GetString());
    }
  }
  return FilteringPosture(prefs.GetBoolean(kFilteringEnabledPref),
                          base::flat_set<std::string>(std::move(hosts)));
}

struct WeekSiteLowerBound {
  base::flat_set<std::string> hosts;
  bool needs_rewrite = false;
};

WeekSiteLowerBound ReadWeekSiteLowerBound(const PrefService& prefs) {
  const base::ListValue& entries = prefs.GetList(kFilteringWeekSitesPref);
  WeekSiteLowerBound result;
  for (const base::Value& entry : entries) {
    if (!entry.is_string() || !IsRecordableHost(entry.GetString())) {
      result.needs_rewrite = true;
      continue;
    }
    if (result.hosts.contains(entry.GetString())) {
      result.needs_rewrite = true;
      continue;
    }
    if (result.hosts.size() >=
        FilteringRulesetService::kWeekSiteLowerBoundCapacity) {
      result.needs_rewrite = true;
      continue;
    }
    result.hosts.insert(entry.GetString());
  }
  return result;
}

}  // namespace

SharedRuleset::SharedRuleset(std::unique_ptr<FilterRulesetMatcher> matcher)
    : matcher_(std::move(matcher)) {}

SharedRuleset::~SharedRuleset() = default;

FilteringRulesetService::FilteringRulesetService(PrefService* prefs,
                                                 ListReader list_reader)
    : prefs_(prefs),
      list_reader_(std::move(list_reader)),
      posture_(ReadPosture(*prefs)) {
  WeekSiteLowerBound persisted = ReadWeekSiteLowerBound(*prefs);
  week_site_lower_bound_ = std::move(persisted.hosts);
  week_sites_dirty_ = persisted.needs_rewrite;
  pref_registrar_.Init(prefs_);
  const auto changed = base::BindRepeating(
      &FilteringRulesetService::OnPosturePrefChanged, GetWeakPtr());
  pref_registrar_.Add(kFilteringEnabledPref, changed);
  pref_registrar_.Add(kFilteringSiteExceptionsPref, changed);
  ReloadRuleset();
}

FilteringRulesetService::~FilteringRulesetService() {
  FlushTotal();
}

void FilteringRulesetService::ReloadRuleset() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // One load at a time; a change arriving mid-load runs one more load after,
  // so the published matcher always reflects the newest installed bytes.
  if (load_in_flight_) {
    reload_queued_ = true;
    return;
  }
  load_in_flight_ = true;
  list_reader_.Run(
      base::BindOnce(&FilteringRulesetService::OnListRead, GetWeakPtr()));
}

void FilteringRulesetService::OnListRead(std::optional<std::string> list_text) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!list_text.has_value()) {
    // Not installed yet, or unreadable. The previous matcher — possibly none —
    // stands: rules that were good a minute ago beat no rules at all, and no
    // rules at all blocks nothing and claims nothing.
    load_in_flight_ = false;
    if (std::exchange(reload_queued_, false)) {
      ReloadRuleset();
    }
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&CompileRuleset, std::move(*list_text)),
      base::BindOnce(&FilteringRulesetService::OnCompiled, GetWeakPtr()));
}

void FilteringRulesetService::OnCompiled(CompiledRuleset compiled) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  load_in_flight_ = false;
  auto matcher = FilterRulesetMatcher::Create(std::move(compiled.bytes),
                                              compiled.checksum);
  if (matcher) {
    ruleset_ = base::MakeRefCounted<SharedRuleset>(std::move(matcher));
    NotifyChanged();
  }
  if (std::exchange(reload_queued_, false)) {
    ReloadRuleset();
  }
}

const FilteringPosture& FilteringRulesetService::posture() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return posture_;
}

uint64_t FilteringRulesetService::posture_revision() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return posture_revision_;
}

scoped_refptr<const SharedRuleset> FilteringRulesetService::ruleset() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return ruleset_;
}

void FilteringRulesetService::SetFilteringEnabled(bool enabled) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  prefs_->SetBoolean(kFilteringEnabledPref, enabled);
}

bool FilteringRulesetService::SetSiteException(std::string_view host,
                                               bool allow) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!IsRecordableHost(host)) {
    return false;
  }
  const base::ListValue& current =
      prefs_->GetList(kFilteringSiteExceptionsPref);
  base::ListValue updated;
  bool present = false;
  for (const base::Value& entry : current) {
    if (entry.is_string() && entry.GetString() == host) {
      present = true;
      if (!allow) {
        continue;  // Removing: every other entry is kept.
      }
    }
    updated.Append(entry.Clone());
  }
  if (allow && !present) {
    updated.Append(std::string(host));
  }
  prefs_->SetList(kFilteringSiteExceptionsPref, std::move(updated));
  return true;
}

void FilteringRulesetService::NoteBlocked() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ++unflushed_blocked_;
  if (!flush_timer_.IsRunning()) {
    flush_timer_.Start(
        FROM_HERE, kTotalFlushInterval,
        base::BindOnce(&FilteringRulesetService::FlushTotal, GetWeakPtr()));
  }
}

void FilteringRulesetService::NoteWeekBlocked(std::string_view host) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  EnsureWeekWindow();
  ++unflushed_week_blocked_;
  if (IsRecordableHost(host)) {
    RecordWeekSite(host);
  }
  if (!flush_timer_.IsRunning()) {
    flush_timer_.Start(
        FROM_HERE, kTotalFlushInterval,
        base::BindOnce(&FilteringRulesetService::FlushTotal, GetWeakPtr()));
  }
}

int64_t FilteringRulesetService::blocked_total() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return prefs_->GetInt64(kFilteringBlockedTotalPref) + unflushed_blocked_;
}

int64_t FilteringRulesetService::blocked_this_week() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MaybeRollWeekWindow();
  if (!has_week_window()) {
    return 0;
  }
  return prefs_->GetInt64(kFilteringBlockedThisWeekPref) +
         unflushed_week_blocked_;
}

int FilteringRulesetService::minimum_sites_this_week() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MaybeRollWeekWindow();
  if (!has_week_window()) {
    return 0;
  }
  return static_cast<int>(week_site_lower_bound_.size());
}

bool FilteringRulesetService::has_week_window() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return prefs_->GetInt64(kFilteringWeekStartPref) != 0;
}

void FilteringRulesetService::NoteTabCountPublished() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  NotifyChanged();
}

base::CallbackListSubscription FilteringRulesetService::AddChangedCallback(
    base::RepeatingClosure changed) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  CHECK(!changed.is_null());
  return changed_callbacks_.Add(std::move(changed));
}

base::WeakPtr<FilteringRulesetService> FilteringRulesetService::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

void FilteringRulesetService::OnPosturePrefChanged() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  posture_ = ReadPosture(*prefs_);
  ++posture_revision_;
  NotifyChanged();
}

void FilteringRulesetService::FlushTotal() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (unflushed_blocked_ != 0) {
    prefs_->SetInt64(kFilteringBlockedTotalPref,
                     prefs_->GetInt64(kFilteringBlockedTotalPref) +
                         std::exchange(unflushed_blocked_, 0));
  }
  if (unflushed_week_blocked_ != 0) {
    prefs_->SetInt64(kFilteringBlockedThisWeekPref,
                     prefs_->GetInt64(kFilteringBlockedThisWeekPref) +
                         std::exchange(unflushed_week_blocked_, 0));
  }
  if (week_sites_dirty_) {
    base::ListValue sites;
    sites.reserve(week_site_lower_bound_.size());
    for (const std::string& host : week_site_lower_bound_) {
      sites.Append(host);
    }
    prefs_->SetList(kFilteringWeekSitesPref, std::move(sites));
    week_sites_dirty_ = false;
  }
}

void FilteringRulesetService::EnsureWeekWindow() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  MaybeRollWeekWindow();
  if (prefs_->GetInt64(kFilteringWeekStartPref) == 0) {
    // A zero start owns no window. Clear any orphaned values before minting
    // the next one so a partially written or externally repaired preference
    // cannot become evidence for a week it never belonged to.
    prefs_->SetInt64(kFilteringBlockedThisWeekPref, 0);
    prefs_->SetList(kFilteringWeekSitesPref, base::ListValue());
    unflushed_week_blocked_ = 0;
    week_site_lower_bound_.clear();
    week_sites_dirty_ = false;
    prefs_->SetInt64(kFilteringWeekStartPref, TimeToPref(base::Time::Now()));
  }
}

void FilteringRulesetService::MaybeRollWeekWindow() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const int64_t start_us = prefs_->GetInt64(kFilteringWeekStartPref);
  if (start_us == 0) {
    return;
  }
  const base::Time now = base::Time::Now();
  const base::Time start = TimeFromPref(start_us);
  if (now - start < kWeekWindow) {
    return;
  }
  prefs_->SetInt64(kFilteringWeekStartPref, TimeToPref(now));
  prefs_->SetInt64(kFilteringBlockedThisWeekPref, 0);
  prefs_->SetList(kFilteringWeekSitesPref, base::ListValue());
  unflushed_week_blocked_ = 0;
  week_site_lower_bound_.clear();
  week_sites_dirty_ = false;
}

void FilteringRulesetService::RecordWeekSite(std::string_view host) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (week_site_lower_bound_.size() >= kWeekSiteLowerBoundCapacity) {
    return;
  }
  week_sites_dirty_ =
      week_site_lower_bound_.insert(std::string(host)).second ||
      week_sites_dirty_;
}

void FilteringRulesetService::NotifyChanged() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  changed_callbacks_.Notify();
}

}  // namespace taffy::filtering
