// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_AI_RUNTIME_STATE_H_
#define TAFFY_BROWSER_AI_RUNTIME_STATE_H_

#include <stdint.h>

// Every state the AI runtime can be in, from the browser process's point of
// view (PAR-AI-BR-001, CAP-BR-001 through CAP-BR-005, REQ-BR-001).
//
// The list exists so that "the AI is unavailable" stops being one condition
// and becomes six. Each of them has produced a different bug in some browser:
// a build where the runtime was never compiled in, a first launch before it
// initialised, a device where it failed, a user who switched it off, a quota
// that ran out mid-session. Manual browsing has to be identical in all six,
// and a test that only covers "absent" would miss the two that happen most.

namespace taffy {

enum class AiRuntimeState : uint8_t {
  // Not present in this build at all, or never registered. The state a browser
  // starts in and the state it stays in when the assistant is not part of the
  // product configuration.
  kAbsent = 0,

  // Present and not yet started. The state during startup, and the one where
  // "manual first paint must not wait for AI initialisation" is decided
  // (PAR-PERF-003).
  kUninitialised = 1,

  kInitialising = 2,
  kReady = 3,

  // Started and broken: no provider, no key, no network, or a crash in the
  // core service. Distinct from kAbsent because a failed runtime may still hold
  // resources and may recover.
  kFailed = 4,

  // The user switched it off, or the account has no entitlement left. Not an
  // error, and the browser must not present it as one.
  kDisabled = 5,
};

// True when a task could actually run. Every other predicate about the runtime
// is derived from this one, so there is a single definition of "available"
// rather than five equality tests that eventually disagree.
constexpr bool AiRuntimeIsAvailable(AiRuntimeState state) {
  return state == AiRuntimeState::kReady;
}

// The complete list, for tests that must cover every state rather than the two
// that came to mind. Ordered; kDisabled is the last value.
inline constexpr AiRuntimeState kAllAiRuntimeStates[] = {
    AiRuntimeState::kAbsent,       AiRuntimeState::kUninitialised,
    AiRuntimeState::kInitialising, AiRuntimeState::kReady,
    AiRuntimeState::kFailed,       AiRuntimeState::kDisabled,
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_AI_RUNTIME_STATE_H_
