// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef TAFFY_BROWSER_AI_RUNTIME_AVAILABILITY_H_
#define TAFFY_BROWSER_AI_RUNTIME_AVAILABILITY_H_

#include "base/no_destructor.h"
#include "taffy/browser/ai_runtime_state.h"

// Where the browser process records what the AI runtime is doing
// (PAR-AI-BR-001).
//
// It is a single value with a single writer, and it exists so that nothing in
// the browser has to ask the runtime a question in order to answer one. That
// distinction is the whole point: a browser that called into the AI runtime to
// find out whether the AI runtime was available would block manual browsing on
// exactly the component that is broken.
//
// Nothing in this class calls the runtime, waits on it, or holds a pointer to
// it. The runtime pushes its state here; the browser reads it. A missing push
// leaves the value at kAbsent, which is the correct answer for a build where
// the assistant was never wired up.
//
// UI thread only.

namespace taffy {

class AiRuntimeAvailability {
 public:
  // Never null. Safe to call from the UI thread after browser-process startup.
  static AiRuntimeAvailability& Get();

  AiRuntimeAvailability(const AiRuntimeAvailability&) = delete;
  AiRuntimeAvailability& operator=(const AiRuntimeAvailability&) = delete;

  AiRuntimeState state() const { return state_; }
  bool IsAvailable() const { return AiRuntimeIsAvailable(state_); }

  // Called by the runtime's own wiring, and by nothing else.
  void SetState(AiRuntimeState state);

 private:
  friend class base::NoDestructor<AiRuntimeAvailability>;

  AiRuntimeAvailability();
  ~AiRuntimeAvailability();

  AiRuntimeState state_ = AiRuntimeState::kAbsent;
};

}  // namespace taffy

#endif  // TAFFY_BROWSER_AI_RUNTIME_AVAILABILITY_H_
