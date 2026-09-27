// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_LIFECYCLE_PHASE_H_
#define TAFFY_BROWSER_LIFECYCLE_PHASE_H_

#include <stdint.h>

// The Android lifecycle vocabulary, in the browser process
// (PAR-AND-005 foreground and background transitions; PAR-AND-007 rotation,
// configuration change and process recreation with no tab or task loss and no
// duplicate action).
//
// The browser process cannot observe the Android lifecycle itself: the Activity
// is Java, and the signal arrives through //taffy/app/android. What this
// header does is fix the vocabulary in one place, so the Java side, the Kotlin
// island, the browser-process ledger and the tests all name the same eight
// phases and the same set of configuration changes. The alternative — each
// layer inventing its own — is how "the tab came back but the task did not"
// becomes a bug nobody can localize.
//
// The distinction the whole seam turns on is the last two phases.
// kDestroyedForRecreation and kDestroyedFinal look identical from inside a
// destroy callback and mean opposite things: one is a rotation and the state
// must come back, the other is the user leaving and the state must not.

namespace taffy {

enum class ActivityLifecyclePhase : uint8_t {
  // Nothing has been reported yet. Fails closed: treated as not foreground.
  kUnknown = 0,
  kCreated = 1,
  kStarted = 2,
  // Foreground and interactive. The only phase in which page control is
  // permitted, because the first workflow's live browsing is foreground-bound
  // (system architecture section 11.4).
  kResumed = 3,
  kPaused = 4,
  kStopped = 5,
  // A configuration change or a memory-driven teardown that will be followed
  // by a new instance carrying the saved state.
  kDestroyedForRecreation = 6,
  // The user finished the activity. Nothing comes back.
  kDestroyedFinal = 7,
};

// What changed. A bit field rather than an enumeration of one value, because
// Android delivers several at once — a fold, for instance, changes screen size,
// density and multi-window state in one callback.
enum class ConfigurationChangeKind : uint32_t {
  kNone = 0,
  kRotation = 1 << 0,
  kScreenSize = 1 << 1,
  kDensity = 1 << 2,
  kFontScale = 1 << 3,
  kUiMode = 1 << 4,
  kLocale = 1 << 5,
  kMultiWindow = 1 << 6,
  // A change this vocabulary does not name. Recorded rather than dropped: a
  // configuration change nobody named is still a configuration change.
  kOther = 1 << 7,
};

constexpr ConfigurationChangeKind operator|(ConfigurationChangeKind left,
                                            ConfigurationChangeKind right) {
  return static_cast<ConfigurationChangeKind>(static_cast<uint32_t>(left) |
                                              static_cast<uint32_t>(right));
}

constexpr bool HasConfigurationChange(ConfigurationChangeKind value,
                                      ConfigurationChangeKind flag) {
  return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
}

// Why page control is not available. Never a free-form string: the surface
// that explains it uses a trusted local template keyed by this value, and the
// task runtime records it as its pause reason.
enum class PageControlPauseReason : uint8_t {
  kNone = 0,
  // Not the foreground, interactive activity.
  kNotForeground = 1,
  kDeviceLocked = 2,
  // Between a destroy-for-recreation and the next resume.
  kRecreationInProgress = 3,
  // The activity finished. Nothing is coming back.
  kActivityFinished = 4,
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_LIFECYCLE_PHASE_H_
