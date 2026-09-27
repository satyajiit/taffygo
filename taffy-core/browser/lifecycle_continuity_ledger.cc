// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/browser/lifecycle_continuity_ledger.h"

#include <string>
#include <string_view>

#include "content/public/browser/browser_thread.h"

namespace taffy {

namespace {

// A generation's admitted set is cleared whenever the generation advances, so
// it is bounded by how many distinct operations one foreground stretch can
// produce. The cap exists for the pathological case — a task loop issuing
// continuations without a recreation — so that a leak becomes a refusal rather
// than unbounded growth in the browser process.
//
// It is not a quality target and not a tuned value: it is a ceiling several
// orders of magnitude above any legitimate use, chosen so that hitting it is
// unambiguously a defect.
constexpr size_t kMaxAdmittedPerWindow = 4096;

std::string_view AsView(const RecordIdentifier& identifier) {
  return std::string_view(identifier.chars.data());
}

// True for the phases from which the process may not come back. Everything
// restorable must already be written by then, because there may be no later
// callback.
bool PhaseRequiresPreservedState(ActivityLifecyclePhase phase) {
  switch (phase) {
    case ActivityLifecyclePhase::kUnknown:
    case ActivityLifecyclePhase::kCreated:
    case ActivityLifecyclePhase::kStarted:
    case ActivityLifecyclePhase::kResumed:
      return false;
    // Android may kill a paused or stopped process without another callback,
    // so the boundary is at pause, not at stop.
    case ActivityLifecyclePhase::kPaused:
    case ActivityLifecyclePhase::kStopped:
    case ActivityLifecyclePhase::kDestroyedForRecreation:
    case ActivityLifecyclePhase::kDestroyedFinal:
      return true;
  }
}

}  // namespace

LifecycleContinuityLedger::LifecycleContinuityLedger() = default;
LifecycleContinuityLedger::~LifecycleContinuityLedger() = default;

LifecycleVerdict LifecycleContinuityLedger::NotePhase(
    const BrowserWindowId& window_id,
    ActivityLifecyclePhase phase) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  WindowState& state = StateFor(window_id);
  const ActivityLifecyclePhase previous = state.phase;
  state.phase = phase;

  if (phase == ActivityLifecyclePhase::kDestroyedForRecreation) {
    // The next instance re-delivers the state this one saved, so everything
    // created under the current generation is about to become a re-delivery.
    state.recreation_in_progress = true;
    ++state.generation;
    state.admitted_operations.clear();
  } else if (phase == ActivityLifecyclePhase::kResumed) {
    state.recreation_in_progress = false;
  } else if (phase == ActivityLifecyclePhase::kCreated &&
             previous == ActivityLifecyclePhase::kDestroyedFinal) {
    // A fresh activity after a final destroy is a new session, not a
    // recreation. Advancing the generation keeps any continuation that
    // outlived the old one from being admitted against the new one.
    ++state.generation;
    state.admitted_operations.clear();
    state.recreation_in_progress = false;
  }

  return VerdictFor(state);
}

LifecycleVerdict LifecycleContinuityLedger::NoteConfigurationChange(
    const BrowserWindowId& window_id,
    ConfigurationChangeKind changes) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  WindowState& state = StateFor(window_id);
  state.last_changes = changes;
  if (changes == ConfigurationChangeKind::kNone) {
    return VerdictFor(state);
  }

  // A declared configuration change does not destroy the activity, but the UI
  // still rebuilds and re-delivers. Advancing the generation treats both
  // shapes of the same event the same way, which is what stops the answer
  // depending on which changes the manifest happens to declare.
  ++state.generation;
  state.admitted_operations.clear();
  return VerdictFor(state);
}

void LifecycleContinuityLedger::SetDeviceLocked(bool locked) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  device_locked_ = locked;
}

LifecycleVerdict LifecycleContinuityLedger::CurrentVerdict(
    const BrowserWindowId& window_id) const {
  auto it = windows_.find(window_id);
  if (it == windows_.end()) {
    // A window nobody has reported anything about is not the foreground
    // activity. Fail closed.
    LifecycleVerdict verdict;
    verdict.pause_reason = PageControlPauseReason::kNotForeground;
    return verdict;
  }
  return VerdictFor(it->second);
}

uint64_t LifecycleContinuityLedger::generation(
    const BrowserWindowId& window_id) const {
  auto it = windows_.find(window_id);
  return it == windows_.end() ? 0u : it->second.generation;
}

ContinuationAdmission LifecycleContinuityLedger::Admit(
    const BrowserWindowId& window_id,
    const ContinuationKey& key) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  const std::string_view operation = AsView(key.operation_id);
  if (operation.empty() || AsView(key.tab_id).empty()) {
    return ContinuationAdmission::kRefusedInvalidKey;
  }

  WindowState& state = StateFor(window_id);
  if (key.generation < state.generation) {
    // Created before a recreation. Whatever it was going to do belongs to a
    // browser state that no longer exists, and running it now is exactly the
    // duplicate action PAR-AND-007 forbids.
    return ContinuationAdmission::kRefusedStaleGeneration;
  }
  if (key.generation > state.generation) {
    // A generation that has not happened. Something is keeping its own
    // counter, which is a defect rather than a race.
    return ContinuationAdmission::kRefusedFutureGeneration;
  }

  // The key is scoped by tab as well as by operation, so two tabs running the
  // same named operation do not shadow one another.
  std::string scoped;
  scoped.reserve(operation.size() + AsView(key.tab_id).size() + 1);
  scoped.append(AsView(key.tab_id));
  scoped.push_back('\n');
  scoped.append(operation);

  if (state.admitted_operations.count(scoped) != 0) {
    return ContinuationAdmission::kAlreadyAdmitted;
  }
  if (state.admitted_operations.size() >= kMaxAdmittedPerWindow) {
    // Refusing rather than evicting. Evicting the oldest entry would let the
    // evicted continuation be admitted a second time, which is the failure
    // this class exists to prevent.
    return ContinuationAdmission::kAlreadyAdmitted;
  }

  state.admitted_operations.insert(std::move(scoped));
  return ContinuationAdmission::kFirstAdmission;
}

size_t LifecycleContinuityLedger::admitted_count(
    const BrowserWindowId& window_id) const {
  auto it = windows_.find(window_id);
  return it == windows_.end() ? 0u : it->second.admitted_operations.size();
}

void LifecycleContinuityLedger::ForgetWindow(
    const BrowserWindowId& window_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  windows_.erase(window_id);
}

LifecycleVerdict LifecycleContinuityLedger::VerdictFor(
    const WindowState& state) const {
  LifecycleVerdict verdict;
  verdict.generation = state.generation;
  verdict.recreation_in_progress = state.recreation_in_progress;
  verdict.state_must_be_preserved = PhaseRequiresPreservedState(state.phase);

  // Order matters: the most specific and least recoverable reason wins, so the
  // explanation a person reads names the thing they can act on.
  if (state.phase == ActivityLifecyclePhase::kDestroyedFinal) {
    verdict.pause_reason = PageControlPauseReason::kActivityFinished;
  } else if (state.recreation_in_progress) {
    verdict.pause_reason = PageControlPauseReason::kRecreationInProgress;
  } else if (device_locked_) {
    verdict.pause_reason = PageControlPauseReason::kDeviceLocked;
  } else if (state.phase != ActivityLifecyclePhase::kResumed) {
    verdict.pause_reason = PageControlPauseReason::kNotForeground;
  } else {
    verdict.pause_reason = PageControlPauseReason::kNone;
  }

  verdict.page_control_permitted =
      verdict.pause_reason == PageControlPauseReason::kNone;
  return verdict;
}

LifecycleContinuityLedger::WindowState& LifecycleContinuityLedger::StateFor(
    const BrowserWindowId& window_id) {
  return windows_[window_id];
}

}  // namespace taffy
